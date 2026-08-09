;;; ZeroMQ module tests — (curry zeromq)
;;;
;;; libzmq is a runtime-only dependency (dlopen'd, never linked at
;;; build time — see zeromq.scm), so it may not be installed on the
;;; machine running this suite. If the first actual use fails, skip
;;; cleanly rather than failing the whole test run — same convention
;;; (curry hdf5)'s own test file uses.
;;;
;;; Every socket pair here talks over inproc:// (in-process transport),
;;; so this suite needs no external server and no network access.

(import (curry zeromq))

(define zmq-available
  (guard (e (#t #f))
    (let ((ctx (zmq-context))) (zmq-context-destroy! ctx))
    #t))

(if (not zmq-available)
    (begin
      (display "SKIP: libzmq not found on this system — install it to run this suite")
      (newline)
      (display "  macOS:         brew install zeromq") (newline)
      (display "  Debian/Ubuntu: apt install libzmq3-dev") (newline)
      (display "  Fedora/RHEL:   dnf install zeromq-devel") (newline)
      (exit 0))
    (begin

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

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

(define %endpoint-counter 0)
(define (fresh-endpoint)
  (set! %endpoint-counter (+ %endpoint-counter 1))
  (string-append "inproc://curry-zmq-test-" (number->string %endpoint-counter)))

;;; Context / socket lifecycle

(check "zmq-context returns a context" (zmq-context? (zmq-context)) #t)
(check "zmq-socket returns a socket"
  (let ((ctx (zmq-context)))
    (let ((s (zmq-socket ctx 'push)))
      (let ((ok (zmq-socket? s)))
        (zmq-close! s) (zmq-context-destroy! ctx)
        ok)))
  #t)
(check-error "zmq-socket raises on an unknown socket type"
  (lambda () (let ((ctx (zmq-context))) (zmq-socket ctx 'not-a-real-type))))

;;; PUSH/PULL — basic send/receive, string round-trip

(let* ((ctx (zmq-context)) (ep (fresh-endpoint))
       (push (zmq-socket ctx 'push)) (pull (zmq-socket ctx 'pull)))
  (zmq-bind! pull ep)
  (zmq-connect! push ep)
  (zmq-send-string! push "hello world")
  (check "push/pull string round-trip" (zmq-recv-string pull) "hello world")
  (zmq-close! push) (zmq-close! pull) (zmq-context-destroy! ctx))

;;; Bytevector round-trip (not just strings)

(let* ((ctx (zmq-context)) (ep (fresh-endpoint))
       (push (zmq-socket ctx 'push)) (pull (zmq-socket ctx 'pull)))
  (zmq-bind! pull ep)
  (zmq-connect! push ep)
  (zmq-send! push (bytevector 1 2 3 255 0))
  (check "push/pull bytevector round-trip" (zmq-recv pull) (bytevector 1 2 3 255 0))
  (zmq-close! push) (zmq-close! pull) (zmq-context-destroy! ctx))

;;; REQ/REP

(let* ((ctx (zmq-context)) (ep (fresh-endpoint))
       (rep (zmq-socket ctx 'rep)) (req (zmq-socket ctx 'req)))
  (zmq-bind! rep ep)
  (zmq-connect! req ep)
  (zmq-send-string! req "ping")
  (check "req/rep: rep receives the request" (zmq-recv-string rep) "ping")
  (zmq-send-string! rep "pong")
  (check "req/rep: req receives the reply" (zmq-recv-string req) "pong")
  (zmq-close! rep) (zmq-close! req) (zmq-context-destroy! ctx))

;;; PUB/SUB with a subscription filter

(let* ((ctx (zmq-context)) (ep (fresh-endpoint))
       (pub (zmq-socket ctx 'pub)) (sub (zmq-socket ctx 'sub)))
  (zmq-bind! pub ep)
  (zmq-connect! sub ep)
  (zmq-subscribe! sub "")
  ;; Subscriptions propagate asynchronously even over inproc; retry a
  ;; short send/receive loop rather than assume the first send lands.
  (let loop ((i 0))
    (zmq-send-string! pub "hello")
    (let ((r (zmq-recv-string sub 'dontwait)))
      (cond (r (check "pub/sub: subscriber receives a published message" r "hello"))
            ((< i 200) (loop (+ i 1)))
            (else (check "pub/sub: subscriber receives a published message" 'gave-up "hello")))))
  (zmq-close! pub) (zmq-close! sub) (zmq-context-destroy! ctx))

;;; Regression: a blocking zmq-recv (no 'dontwait) on a socket with
;;; RCVTIMEO set used to raise once the timeout elapsed, instead of
;;; returning #f like every other "no message available" case -- EAGAIN
;;; is the exact same errno for both a 'dontwait call with nothing
;;; queued and a blocking call whose RCVTIMEO deadline elapsed, and the
;;; old check only special-cased the former.
(let* ((ctx (zmq-context)) (ep (fresh-endpoint)) (pull (zmq-socket ctx 'pull)))
  (zmq-bind! pull ep)
  (zmq-set-rcvtimeo! pull 100)
  (check "blocking zmq-recv returns #f (not a raise) once RCVTIMEO elapses"
    (zmq-recv pull)
    #f)
  (zmq-close! pull) (zmq-context-destroy! ctx))

;;; Regression: zmq_msg_close used to be skipped on any recv failure
;;; that wasn't EAGAIN (e.g. calling recv on a push/pub socket, which
;;; libzmq itself rejects) -- the error path returned/raised before
;;; ever reaching the cleanup call. Not a crash by itself, but violates
;;; the zmq_msg_init/zmq_msg_close pairing contract; confirm the error
;;; still surfaces cleanly (this doesn't directly observe whether close
;;; ran, just that the call doesn't crash or hang either way).
(let* ((ctx (zmq-context)) (push (zmq-socket ctx 'push)))
  (check-error "zmq-recv on a push socket raises cleanly (recv is unsupported on push)"
    (lambda () (zmq-recv push)))
  (zmq-close! push) (zmq-context-destroy! ctx))

;;; Multipart messages

(let* ((ctx (zmq-context)) (ep (fresh-endpoint))
       (push (zmq-socket ctx 'push)) (pull (zmq-socket ctx 'pull)))
  (zmq-bind! pull ep)
  (zmq-connect! push ep)
  (zmq-send-string! push "part1" 'sndmore)
  (zmq-send-string! push "part2")
  (check "multipart: first frame" (zmq-recv-string pull) "part1")
  (check "multipart: zmq-more? is true after the first frame" (zmq-more? pull) #t)
  (check "multipart: second frame" (zmq-recv-string pull) "part2")
  (check "multipart: zmq-more? is false after the last frame" (zmq-more? pull) #f)
  (zmq-close! push) (zmq-close! pull) (zmq-context-destroy! ctx))

;;; Non-blocking receive

(let* ((ctx (zmq-context)) (ep (fresh-endpoint)) (pull (zmq-socket ctx 'pull)))
  (zmq-bind! pull ep)
  (check "zmq-recv with 'dontwait and no message returns #f" (zmq-recv pull 'dontwait) #f)
  (zmq-close! pull) (zmq-context-destroy! ctx))

;;; Socket options don't raise

(let* ((ctx (zmq-context)) (ep (fresh-endpoint)) (s (zmq-socket ctx 'pull)))
  (zmq-set-linger! s 0)
  (zmq-set-rcvtimeo! s 50)
  (zmq-set-sndtimeo! s 50)
  (zmq-set-identity! s "worker-1")
  (check "socket options can be set without raising" #t #t)
  (zmq-close! s) (zmq-context-destroy! ctx))

;;; zmq-version

(define (every pred lst) (or (null? lst) (and (pred (car lst)) (every pred (cdr lst)))))

(check "zmq-version returns a 3-element list of non-negative integers"
  (let ((v (zmq-version)))
    (and (list? v) (= (length v) 3) (every (lambda (n) (and (integer? n) (>= n 0))) v)))
  #t)

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))

  )) ;; end (if zmq-available ...)
