;;; (curry zeromq) — ZeroMQ (https://zeromq.org) messaging sockets, via
;;; (curry ffi). libzmq is dlopen'd lazily at runtime (not linked at
;;; build time — no CMake flag beyond the general BUILD_FFI=ON), the
;;; same pattern (curry hdf5)/(curry ncurses)/(curry graphviz) use.
;;; Install: `brew install zeromq` (macOS), `apt install libzmq3-dev`
;;; (Debian/Ubuntu), `dnf install zeromq-devel` (Fedora/RHEL).
;;;
;;; Covers the core messaging primitives: context/socket lifecycle,
;;; bind/connect, send/receive (blocking and non-blocking), the common
;;; socket options (SUBSCRIBE/UNSUBSCRIBE/LINGER/RCVTIMEO/SNDTIMEO/
;;; IDENTITY), and multipart messages (SNDMORE / RCVMORE). Messages are
;;; always plain bytevectors at the primitive level; zmq-send-string!/
;;; zmq-recv-string are thin string<->bytevector convenience wrappers
;;; around that (via string->utf8/utf8->string), not a separate code
;;; path.
;;;
;;; Deliberately not supported (see docs/reference/module-zeromq.md):
;;;
;;; - `zmq_poll` — multiplexing many sockets in one call needs an array
;;;   of `zmq_pollitem_t` structs, which is real added FFI complexity
;;;   (struct-array marshaling) this module's core send/recv/sockopt
;;;   surface didn't otherwise need. A curry program wanting to wait on
;;;   several sockets today should give each its own actor and a
;;;   blocking `zmq-recv` in a loop instead.
;;; - CURVE/PLAIN security mechanisms, `zmq_proxy`, ZMQ_RADIO/ZMQ_DISH
;;;   and other draft-API socket types — outside this module's scope of
;;;   the stable, widely-used core API.

(define-library (curry zeromq)
  (import (scheme base) (curry ffi))
  (export
    zmq-context zmq-context-destroy! zmq-context?
    zmq-socket zmq-socket? zmq-bind! zmq-connect! zmq-close!
    zmq-send! zmq-send-string! zmq-recv zmq-recv-string zmq-more?
    zmq-set-linger! zmq-set-rcvtimeo! zmq-set-sndtimeo! zmq-set-identity!
    zmq-subscribe! zmq-unsubscribe!
    zmq-version)
  (begin

;; ── Library discovery ────────────────────────────────────────────────────────

(define %zmq-candidates
  (list
    "libzmq.dylib"                                   ; macOS, on loader path
    "libzmq.so.5"                                     ; Linux, on loader path
    "/opt/homebrew/lib/libzmq.dylib"                  ; Homebrew, Apple Silicon
    "/usr/local/lib/libzmq.dylib"                     ; Homebrew, Intel Mac
    "/usr/lib/x86_64-linux-gnu/libzmq.so.5"            ; Debian/Ubuntu, x86_64
    "/usr/lib/aarch64-linux-gnu/libzmq.so.5"           ; Debian/Ubuntu, arm64
    "/usr/lib64/libzmq.so.5"))                         ; Fedora/RHEL

(define (%zmq-try-load candidates)
  (let loop ((c candidates))
    (if (null? c)
        #f
        (guard (exn (#t (loop (cdr c))))
          (foreign-load-library (car c))))))

;; Loaded lazily, on first actual use (zmq-context), not at import time —
;; importing this module never requires libzmq to be installed.
(define %zmq-lib #f)
(define %zmq-bound? #f)

(define (%zmq-ensure!)
  (unless %zmq-lib
    (set! %zmq-lib
      (or (%zmq-try-load %zmq-candidates)
          (error "zeromq: could not load libzmq — install it first:
  macOS:           brew install zeromq
  Debian/Ubuntu:   apt install libzmq3-dev
  Fedora/RHEL:     dnf install zeromq-devel"))))
  (unless %zmq-bound? (%zmq-bind-fns!) (set! %zmq-bound? #t)))

;; ── Raw foreign bindings, bound lazily (define-foreign's #:from is
;; evaluated once at its own definition time, so these can't reference
;; %zmq-lib before %zmq-ensure! has populated it — same reason (curry
;; graphviz) defers its own bindings this way). ────────────────────────────────

(define %zmq-ctx-new #f) (define %zmq-ctx-term #f)
(define %zmq-socket #f) (define %zmq-close #f)
(define %zmq-bind #f) (define %zmq-connect #f)
(define %zmq-send #f)
(define %zmq-setsockopt #f) (define %zmq-getsockopt #f)
(define %zmq-msg-init #f) (define %zmq-msg-recv #f)
(define %zmq-msg-size #f) (define %zmq-msg-data #f) (define %zmq-msg-close #f)
(define %zmq-errno #f) (define %zmq-strerror #f)
(define %zmq-version-raw #f)

(define (%zmq-bind-fns!)
  (set! %zmq-ctx-new (let ((fn (%ffi-make-fn %zmq-lib "zmq_ctx_new" 'c-ptr '())))
                        (lambda () (%ffi-call fn '()))))
  (set! %zmq-ctx-term (let ((fn (%ffi-make-fn %zmq-lib "zmq_ctx_term" 'int '(c-ptr))))
                         (lambda (ctx) (%ffi-call fn (list ctx)))))
  (set! %zmq-socket (let ((fn (%ffi-make-fn %zmq-lib "zmq_socket" 'c-ptr '(c-ptr int))))
                       (lambda (ctx type) (%ffi-call fn (list ctx type)))))
  (set! %zmq-close (let ((fn (%ffi-make-fn %zmq-lib "zmq_close" 'int '(c-ptr))))
                      (lambda (s) (%ffi-call fn (list s)))))
  (set! %zmq-bind (let ((fn (%ffi-make-fn %zmq-lib "zmq_bind" 'int '(c-ptr c-string))))
                     (lambda (s ep) (%ffi-call fn (list s ep)))))
  (set! %zmq-connect (let ((fn (%ffi-make-fn %zmq-lib "zmq_connect" 'int '(c-ptr c-string))))
                        (lambda (s ep) (%ffi-call fn (list s ep)))))
  (set! %zmq-send (let ((fn (%ffi-make-fn %zmq-lib "zmq_send" 'int '(c-ptr c-ptr uint64 int))))
                     (lambda (s buf len flags) (%ffi-call fn (list s buf len flags)))))
  (set! %zmq-setsockopt (let ((fn (%ffi-make-fn %zmq-lib "zmq_setsockopt" 'int '(c-ptr int c-ptr uint64))))
                           (lambda (s opt val len) (%ffi-call fn (list s opt val len)))))
  (set! %zmq-getsockopt (let ((fn (%ffi-make-fn %zmq-lib "zmq_getsockopt" 'int '(c-ptr int c-ptr c-ptr))))
                           (lambda (s opt val len-ptr) (%ffi-call fn (list s opt val len-ptr)))))
  (set! %zmq-msg-init (let ((fn (%ffi-make-fn %zmq-lib "zmq_msg_init" 'int '(c-ptr))))
                         (lambda (msg) (%ffi-call fn (list msg)))))
  (set! %zmq-msg-recv (let ((fn (%ffi-make-fn %zmq-lib "zmq_msg_recv" 'int '(c-ptr c-ptr int))))
                         (lambda (msg s flags) (%ffi-call fn (list msg s flags)))))
  (set! %zmq-msg-size (let ((fn (%ffi-make-fn %zmq-lib "zmq_msg_size" 'uint64 '(c-ptr))))
                         (lambda (msg) (%ffi-call fn (list msg)))))
  (set! %zmq-msg-data (let ((fn (%ffi-make-fn %zmq-lib "zmq_msg_data" 'c-ptr '(c-ptr))))
                         (lambda (msg) (%ffi-call fn (list msg)))))
  (set! %zmq-msg-close (let ((fn (%ffi-make-fn %zmq-lib "zmq_msg_close" 'int '(c-ptr))))
                          (lambda (msg) (%ffi-call fn (list msg)))))
  (set! %zmq-errno (let ((fn (%ffi-make-fn %zmq-lib "zmq_errno" 'int '())))
                      (lambda () (%ffi-call fn '()))))
  (set! %zmq-strerror (let ((fn (%ffi-make-fn %zmq-lib "zmq_strerror" 'c-string '(int))))
                         (lambda (errnum) (%ffi-call fn (list errnum)))))
  (set! %zmq-version-raw (let ((fn (%ffi-make-fn %zmq-lib "zmq_version" 'void '(c-ptr c-ptr c-ptr))))
                            (lambda (maj min pat) (%ffi-call fn (list maj min pat))))))

;; ── Constants (from zmq.h; see this module's own header comment for
;; why libzmq isn't linked at build time, so these can't just be #include'd) ──

(define %ZMQ-PAIR 0) (define %ZMQ-PUB 1) (define %ZMQ-SUB 2) (define %ZMQ-REQ 3)
(define %ZMQ-REP 4) (define %ZMQ-DEALER 5) (define %ZMQ-ROUTER 6) (define %ZMQ-PULL 7)
(define %ZMQ-PUSH 8) (define %ZMQ-XPUB 9) (define %ZMQ-XSUB 10) (define %ZMQ-STREAM 11)

(define %ZMQ-ROUTING-ID 5) (define %ZMQ-SUBSCRIBE 6) (define %ZMQ-UNSUBSCRIBE 7)
(define %ZMQ-RCVMORE 13) (define %ZMQ-LINGER 17) (define %ZMQ-RCVTIMEO 27) (define %ZMQ-SNDTIMEO 28)

(define %ZMQ-DONTWAIT 1) (define %ZMQ-SNDMORE 2)

;; zmq_msg_t is an opaque fixed-size struct (64 bytes of internal
;; storage per zmq.h); this module never reads its fields directly,
;; only ever hands its address to zmq_msg_*, so an oversized scratch
;; buffer works just as well as an exactly-sized one and is safer than
;; guessing the exact figure across libzmq versions/platforms.
(define %ZMQ-MSG-T-SCRATCH-SIZE 128)

(define (%socket-type->int type)
  (case type
    ((pair) %ZMQ-PAIR) ((pub) %ZMQ-PUB) ((sub) %ZMQ-SUB) ((req) %ZMQ-REQ)
    ((rep) %ZMQ-REP) ((dealer) %ZMQ-DEALER) ((router) %ZMQ-ROUTER) ((pull) %ZMQ-PULL)
    ((push) %ZMQ-PUSH) ((xpub) %ZMQ-XPUB) ((xsub) %ZMQ-XSUB) ((stream) %ZMQ-STREAM)
    (else (error "zeromq: unknown socket type" type))))

(define (%flags->int flags)
  (let loop ((fs flags) (acc 0))
    (if (null? fs)
        acc
        (loop (cdr fs)
              (+ acc (case (car fs) ((dontwait) %ZMQ-DONTWAIT) ((sndmore) %ZMQ-SNDMORE)
                                     (else (error "zeromq: unknown flag" (car fs)))))))))

(define (%zmq-check who rc)
  (when (< rc 0)
    (error (string-append "zeromq: " who ": " (%zmq-strerror (%zmq-errno))))))

(define (%string-contains? haystack needle)
  (let ((hlen (string-length haystack)) (nlen (string-length needle)))
    (let loop ((i 0))
      (cond ((> (+ i nlen) hlen) #f)
            ((string=? (substring haystack i (+ i nlen)) needle) #t)
            (else (loop (+ i 1)))))))

;; EAGAIN is the specific "no message available right now" errno --
;; both a DONTWAIT call with nothing queued yet AND a plain blocking
;; call whose RCVTIMEO deadline elapsed produce this exact same errno
;; (libzmq gives the caller no other way to tell those two cases
;; apart), so both must be treated the same way here: a normal,
;; expected outcome (return #f), not raised like every other failure.
;; An earlier version of this check instead asked "did the *caller*
;; pass 'dontwait?", which is the wrong question entirely -- it made a
;; plain (zmq-recv sock) call on a socket with RCVTIMEO set raise on
;; every timeout instead of returning #f, defeating the normal
;; "blocking receive with a give-up deadline" idiom RCVTIMEO exists
;; for. errno has no portable numeric value across platforms (35 on
;; macOS/BSD, 11 on Linux) so this matches strerror's text instead --
;; same "match the message, since there's no structured errno
;; introspection API" approach (curry dot-locking)'s own EEXIST check
;; already uses.
(define (%zmq-eagain? errno) (%string-contains? (%zmq-strerror errno) "Resource temporarily unavailable"))

;; ── Contexts and sockets ─────────────────────────────────────────────────────

(define (zmq-context)
  (%zmq-ensure!)
  (let ((ctx (%zmq-ctx-new)))
    (when (cptr-null? ctx) (error "zeromq: zmq_ctx_new failed"))
    ctx))

(define (zmq-context? x) (c-ptr? x))

(define (zmq-context-destroy! ctx) (%zmq-check "zmq_ctx_destroy" (%zmq-ctx-term ctx)))

(define (zmq-socket ctx type)
  (let ((s (%zmq-socket ctx (%socket-type->int type))))
    (when (cptr-null? s) (error "zeromq: zmq_socket failed" type))
    s))

(define (zmq-socket? x) (c-ptr? x))

(define (zmq-bind! sock endpoint) (%zmq-check "zmq_bind" (%zmq-bind sock endpoint)))
(define (zmq-connect! sock endpoint) (%zmq-check "zmq_connect" (%zmq-connect sock endpoint)))
(define (zmq-close! sock) (%zmq-check "zmq_close" (%zmq-close sock)))

;; ── Send / receive ───────────────────────────────────────────────────────────

;; (zmq-send! sock bv)                -> sends bv (a bytevector), blocking
;; (zmq-send! sock bv 'dontwait)       -> non-blocking; raises on EAGAIN too,
;;                                        since a dropped outgoing message is
;;                                        not something to silently ignore
;;                                        the way an empty recv is
;; (zmq-send! sock bv 'sndmore)        -> more frames of this multipart
;;                                        message will follow
(define (zmq-send! sock bv . flags)
  (with-pinned-bytevector bv ptr
    (%zmq-check "zmq_send" (%zmq-send sock ptr (bytevector-length bv) (%flags->int flags)))))

(define (zmq-send-string! sock str . flags) (apply zmq-send! sock (string->utf8 str) flags))

;; (zmq-recv sock)             -> bytevector, blocking (or until RCVTIMEO
;;                                elapses, if set -- see #f case below)
;; (zmq-recv sock 'dontwait)   -> bytevector, or #f if no message is
;;                                available right now
;;
;; Returns #f, rather than raising, whenever the underlying zmq_msg_recv
;; fails with EAGAIN -- covering both an immediate 'dontwait with
;; nothing queued yet AND a plain blocking call whose RCVTIMEO deadline
;; elapsed (see %zmq-eagain?'s own comment for why both cases collapse
;; to the same errno with no way to tell them apart). zmq_msg_close is
;; called on every path out of this procedure, success, EAGAIN, or any
;; other error -- errno is captured immediately after the failing call,
;; before zmq_msg_close (or anything else) can overwrite it.
(define (zmq-recv sock . flags)
  (%zmq-ensure!)
  (let ((msg (make-bytevector %ZMQ-MSG-T-SCRATCH-SIZE 0)))
    (with-pinned-bytevector msg msg-ptr
      (%zmq-check "zmq_msg_init" (%zmq-msg-init msg-ptr))
      (let ((rc (%zmq-msg-recv msg-ptr sock (%flags->int flags))))
        (cond
          ((>= rc 0)
           (let* ((size (%zmq-msg-size msg-ptr))
                  (data (peek-bytes (%zmq-msg-data msg-ptr) size)))
             (%zmq-msg-close msg-ptr)
             data))
          (else
           (let ((errno (%zmq-errno)))
             (%zmq-msg-close msg-ptr)
             (if (%zmq-eagain? errno)
                 #f
                 (error (string-append "zeromq: zmq_msg_recv: " (%zmq-strerror errno)))))))))))

(define (zmq-recv-string sock . flags)
  (let ((bv (apply zmq-recv sock flags)))
    (and bv (utf8->string bv))))

;; True iff more parts of the current multipart message remain to be
;; received (ZMQ_RCVMORE, an int-valued option). zmq_getsockopt's
;; option_len is an in/out size_t*: the caller sets it to the buffer's
;; own size before the call (4, sizeof(int)) and libzmq overwrites it
;; with however many bytes it actually wrote (also 4, here) -- this
;; module never reads that updated value back, only the option's own
;; value buffer, so the write below only needs to establish the INITIAL
;; buffer-size contract. curry has no built-in endian-aware bytevector
;; accessor (see (curry ffi)'s own docs), so this pokes size_t 4 in
;; manually, least-significant byte first -- correct on the little-
;; endian x86_64/arm64 targets curry actually builds for; every other
;; byte of the 8-byte size_t buffer is already 0 from make-bytevector.
(define (zmq-more? sock)
  (%zmq-ensure!)
  (let ((val (make-bytevector 4 0)) (len (make-bytevector 8 0)))
    (bytevector-u8-set! len 0 4)
    (with-pinned-bytevector val val-ptr
      (with-pinned-bytevector len len-ptr
        (%zmq-check "zmq_getsockopt" (%zmq-getsockopt sock %ZMQ-RCVMORE val-ptr len-ptr))
        (not (zero? (bytevector-u8-ref val 0)))))))

;; ── Socket options ───────────────────────────────────────────────────────────

;; Pokes `ms` into a 4-byte little-endian buffer by hand -- same
;; rationale as zmq-more?'s own comment above: no built-in endian-aware
;; bytevector accessor exists, and little-endian is correct for every
;; platform curry actually builds for.
(define (%set-int-opt! who sock opt ms)
  (let ((buf (make-bytevector 4 0)))
    (bytevector-u8-set! buf 0 (bitwise-and ms #xff))
    (bytevector-u8-set! buf 1 (bitwise-and (arithmetic-shift ms -8) #xff))
    (bytevector-u8-set! buf 2 (bitwise-and (arithmetic-shift ms -16) #xff))
    (bytevector-u8-set! buf 3 (bitwise-and (arithmetic-shift ms -24) #xff))
    (with-pinned-bytevector buf ptr
      (%zmq-check who (%zmq-setsockopt sock opt ptr 4)))))

(define (zmq-set-linger! sock ms) (%set-int-opt! "zmq_setsockopt(LINGER)" sock %ZMQ-LINGER ms))
(define (zmq-set-rcvtimeo! sock ms) (%set-int-opt! "zmq_setsockopt(RCVTIMEO)" sock %ZMQ-RCVTIMEO ms))
(define (zmq-set-sndtimeo! sock ms) (%set-int-opt! "zmq_setsockopt(SNDTIMEO)" sock %ZMQ-SNDTIMEO ms))

(define (%bytes-opt v) (if (string? v) (string->utf8 v) v))

(define (zmq-set-identity! sock id)
  (let ((bv (%bytes-opt id)))
    (with-pinned-bytevector bv ptr
      (%zmq-check "zmq_setsockopt(IDENTITY)" (%zmq-setsockopt sock %ZMQ-ROUTING-ID ptr (bytevector-length bv))))))

(define (zmq-subscribe! sock topic)
  (let ((bv (%bytes-opt topic)))
    (with-pinned-bytevector bv ptr
      (%zmq-check "zmq_setsockopt(SUBSCRIBE)" (%zmq-setsockopt sock %ZMQ-SUBSCRIBE ptr (bytevector-length bv))))))

(define (zmq-unsubscribe! sock topic)
  (let ((bv (%bytes-opt topic)))
    (with-pinned-bytevector bv ptr
      (%zmq-check "zmq_setsockopt(UNSUBSCRIBE)" (%zmq-setsockopt sock %ZMQ-UNSUBSCRIBE ptr (bytevector-length bv))))))

;; ── Misc ─────────────────────────────────────────────────────────────────────

;; (zmq-version) -> (list major minor patch)
(define (zmq-version)
  (%zmq-ensure!)
  (let ((maj (make-bytevector 4 0)) (min (make-bytevector 4 0)) (pat (make-bytevector 4 0)))
    (with-pinned-bytevector maj maj-ptr
      (with-pinned-bytevector min min-ptr
        (with-pinned-bytevector pat pat-ptr
          (%zmq-version-raw maj-ptr min-ptr pat-ptr))))
    (list (bytevector-u8-ref maj 0) (bytevector-u8-ref min 0) (bytevector-u8-ref pat 0))))

  )) ;; end begin, define-library
