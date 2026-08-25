;;; Tests for (srfi 106) -- Basic Socket Interface.
;;;
;;; Native primitives live in modules/network/srfi106.c, built on the
;;; same raw-socket-handle representation (curry network) already uses
;;; for tcp-listen/udp-socket (network_internal.h). Covers: real TCP
;;; round trips (both raw socket-send/recv AND port-based socket-input-
;;; port/socket-output-port), UDP via make-client-socket/make-server-
;;; socket with (socket-domain datagram), the six name->constant macros
;;; (address-family/address-info/socket-domain/ip-protocol/message-type/
;;; shutdown-method), socket-merge-flags/socket-purge-flags, and
;;; call-with-socket's close-on-error guarantee.
;;;
;;; shutdown-method gets its own dedicated correctness check (not just a
;;; smoke test) because it's the one flag category that is NOT plain
;;; bitwise-or over raw platform values -- see srfi106.c's own comment
;;; on *shut-rd*/*shut-wr*/*shut-rdwr* for why combining raw POSIX
;;; SHUT_RD/SHUT_WR (0/1 on every platform checked, not independent
;;; bits) the same way address-info/message-type combine their own
;;; genuinely-independent AI_*/MSG_* bits would silently produce the
;;; wrong value.

(import (srfi 106) (scheme base) (scheme write))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-true label result)
  (check label (if result #t #f) #t))

;;; ════════════════════════════════════════════════════════════
;;; § 1  name->constant macros
;;; ════════════════════════════════════════════════════════════

(check "address-family inet"  (address-family inet)  *af-inet*)
(check "address-family inet6" (address-family inet6) *af-inet6*)
(check "address-family unspec" (address-family unspec) *af-unspec*)

(check "socket-domain stream"   (socket-domain stream)   *sock-stream*)
(check "socket-domain datagram" (socket-domain datagram) *sock-dgram*)

(check "ip-protocol tcp" (ip-protocol tcp) *ipproto-tcp*)
(check "ip-protocol udp" (ip-protocol udp) *ipproto-udp*)

(check "address-info single name" (address-info canonname) *ai-canonname*)
(check "address-info combines independent bits"
  (address-info v4mapped all)
  (bitwise-or *ai-v4mapped* *ai-all*))

(check "message-type none is zero" (message-type none) 0)
(check "message-type combines independent bits"
  (message-type peek oob)
  (bitwise-or *msg-peek* *msg-oob*))

;;; The correctness-critical case: read+write must equal *shut-rdwr*
;;; exactly, NOT (bitwise-or *shut-rd* *shut-wr*) computed against raw
;;; platform SHUT_RD/SHUT_WR values, which would silently collide.
(check "shutdown-method read alone"  (shutdown-method read)  *shut-rd*)
(check "shutdown-method write alone" (shutdown-method write) *shut-wr*)
(check "shutdown-method read+write is genuinely *shut-rdwr*, not a collision"
  (shutdown-method read write)
  *shut-rdwr*)
(check "shutdown-method read+write distinct from write alone"
  (not (= (shutdown-method read write) (shutdown-method write)))
  #t)

;;; ════════════════════════════════════════════════════════════
;;; § 2  socket-merge-flags / socket-purge-flags
;;; ════════════════════════════════════════════════════════════

(check "merge-flags ors independent flags"
  (socket-merge-flags *msg-peek* *msg-oob*)
  (bitwise-or *msg-peek* *msg-oob*))
(check "merge-flags of one flag is itself"
  (socket-merge-flags *msg-peek*)
  *msg-peek*)
(check "purge-flags removes exactly the given bits"
  (socket-purge-flags (socket-merge-flags *msg-peek* *msg-oob*) *msg-oob*)
  *msg-peek*)
(check "purge-flags is a no-op when the flag wasn't present"
  (socket-purge-flags *msg-peek* *msg-oob*)
  *msg-peek*)

;;; ════════════════════════════════════════════════════════════
;;; § 3  socket? predicate
;;; ════════════════════════════════════════════════════════════

(check "socket? false for non-socket values" (socket? 42) #f)
(check "socket? false for an ordinary string" (socket? "not a socket") #f)

;;; ════════════════════════════════════════════════════════════
;;; § 4  TCP round trip -- raw socket-send/socket-recv
;;; ════════════════════════════════════════════════════════════

(define tcp-port 18601)
(define srv (make-server-socket tcp-port (address-family inet) (socket-domain stream)))
(check-true "make-server-socket returns a socket" (socket? srv))

(define client (make-client-socket "127.0.0.1" tcp-port
                                    (address-family inet) (socket-domain stream)))
(check-true "make-client-socket returns a socket" (socket? client))

(define conn (socket-accept srv))
(check-true "socket-accept returns a socket" (socket? conn))

(define sent (socket-send client (string->utf8 "hello")))
(check "socket-send returns the byte count sent" sent 5)
(check "socket-recv receives exactly what was sent"
  (utf8->string (socket-recv conn 16))
  "hello")

;;; Reply the other direction on the same connection.
(socket-send conn (string->utf8 "world"))
(check "socket-recv works in both directions on the same connection"
  (utf8->string (socket-recv client 16))
  "world")

;;; ════════════════════════════════════════════════════════════
;;; § 5  TCP round trip -- port-based I/O via socket-input-port/
;;; socket-output-port (must interoperate with ordinary port procedures)
;;; ════════════════════════════════════════════════════════════

(define conn-out (socket-output-port conn))
(write-string "line one\n" conn-out)
(close-port conn-out)

(define client-in (socket-input-port client))
(check "socket-input-port interoperates with read-line"
  (read-line client-in)
  "line one")
(close-port client-in)

;;; The underlying socket handle itself must still be independently
;;; usable after taking a port from it -- socket-input-port/
;;; socket-output-port dup the fd rather than consuming the handle.
(socket-send conn (string->utf8 "still alive"))
(check "the socket handle is still usable after socket-input-port"
  (utf8->string (socket-recv client 32))
  "still alive")

(socket-shutdown conn (shutdown-method read write))
(socket-close conn)
(socket-close client)
(socket-close srv)

;;; ════════════════════════════════════════════════════════════
;;; § 6  UDP round trip -- (socket-domain datagram)
;;; ════════════════════════════════════════════════════════════

(define udp-port 18602)
(define udp-srv (make-server-socket udp-port (address-family inet) (socket-domain datagram)))
(define udp-client (make-client-socket "127.0.0.1" udp-port
                                        (address-family inet) (socket-domain datagram)))
(socket-send udp-client (string->utf8 "udp ping"))
(check "UDP round trip via make-client-socket/make-server-socket"
  (utf8->string (socket-recv udp-srv 32))
  "udp ping")
(socket-close udp-client)
(socket-close udp-srv)

;;; ════════════════════════════════════════════════════════════
;;; § 7  call-with-socket -- closes on normal return AND on error
;;; ════════════════════════════════════════════════════════════

(define cws-port 18603)
(define cws-srv (make-server-socket cws-port (address-family inet) (socket-domain stream)))

(define normal-result
  (call-with-socket
    (make-client-socket "127.0.0.1" cws-port (address-family inet) (socket-domain stream))
    (lambda (s) (socket-send s (string->utf8 "x")) 'normal-return)))
(check "call-with-socket returns the thunk's own value" normal-result 'normal-return)

(define accepted1 (socket-accept cws-srv))
(socket-recv accepted1 16)
(socket-close accepted1)

;;; The socket call-with-socket closed above must now be genuinely
;;; unusable -- socket-send on an already-closed fd raises rather than
;;; silently succeeding.
(define error-raised #f)
(define cws-client2
  (make-client-socket "127.0.0.1" cws-port (address-family inet) (socket-domain stream)))
(guard (e (#t (set! error-raised #t)))
  (call-with-socket cws-client2
    (lambda (s) (error "deliberate error inside call-with-socket"))))
(check "call-with-socket's thunk error propagates" error-raised #t)

;;; And the socket must have been closed by the after-thunk despite the
;;; error -- socket-send on an already-closed fd must now fail, proving
;;; close-on-error actually ran. (NOT a second socket-close: this
;;; codebase's own tcp-close, same convention socket-close follows,
;;; never checks close()'s return value either, so double-close is not
;;; guaranteed to raise -- POSIX close() on an already-closed fd is
;;; EBADF on most platforms but that's not a contract this module makes
;;; anywhere else, so asserting on it here would test an incidental
;;; platform detail instead of the actual guarantee that matters.)
(define send-after-close-raised #f)
(guard (e (#t (set! send-after-close-raised #t)))
  (socket-send cws-client2 (string->utf8 "should fail, socket is closed")))
(check "call-with-socket closed the socket even though the thunk raised"
  send-after-close-raised #t)

(define accepted2 (socket-accept cws-srv))
(socket-close accepted2)
(socket-close cws-srv)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
