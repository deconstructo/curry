;;; (curry network) tests — TCP socket-as-port round-trip.
;;;
;;; Self-contained: listens on loopback, connects to itself, exercises
;;; read-line/write-string bidirectionally. No external network access
;;; needed. Regression coverage for tcp-connect/tcp-accept actually
;;; returning ports (they used to silently discard the FILE* streams
;;; they created and return a raw fd pair instead).

(import (curry network))
(import (scheme base))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define test-port 17999)

;;; ════════════════════════════════════════════════════════════
;;; § 1  tcp-listen/tcp-accept/tcp-connect return real ports
;;; ════════════════════════════════════════════════════════════

(define listener (tcp-listen test-port))

(define server-thread
  (spawn (lambda ()
           (let* ((conn (tcp-accept listener))
                  (in (car conn)) (out (cdr conn)))
             (let loop ()
               (let ((line (read-line in)))
                 (unless (eof-object? line)
                   (write-string (string-append "echo:" line) out)
                   (write-string "\n" out)
                   (flush-output-port out)
                   (loop))))
             (close-port in)
             (close-port out)))))

;; No delay needed: tcp-listen's listen() call already puts the socket in
;; a listening state accepting the OS backlog before tcp-accept is ever
;; called — connect() succeeds regardless of whether the accepting actor
;; has reached its accept() call yet.
(define client (tcp-connect "127.0.0.1" test-port))
(define cin (car client))
(define cout (cdr client))

(check "in port is a port"  (port? cin) #t)
(check "out port is a port" (port? cout) #t)

(write-string "hello" cout)
(write-string "\n" cout)
(flush-output-port cout)

(check "echo round-trip" (read-line cin) "echo:hello")

(write-string "second" cout)
(write-string "\n" cout)
(flush-output-port cout)
(check "second round-trip" (read-line cin) "echo:second")

(close-port cin)
(close-port cout)
(tcp-close listener)

;;; ════════════════════════════════════════════════════════════
;;; § 2  socket-ready? / socket-set-nonblocking!
;;;
;;; Scoped-down non-blocking support (see docs/reference/module-network.md):
;;; select()-based readiness check working on both a raw socket handle
;;; (tcp-listen's return) and a port (tcp-accept's/tcp-connect's).
;;; ════════════════════════════════════════════════════════════

(define test-port2 18099)
(define listener2 (tcp-listen test-port2))

(check "listener not ready with no pending connection"
  (socket-ready? listener2) #f)

(spawn (lambda ()
         (define conn (tcp-connect "127.0.0.1" test-port2))
         (write-string "ping\n" (cdr conn))
         (flush-output-port (cdr conn))
         (close-port (car conn)) (close-port (cdr conn))))

;; socket-ready?'s own timeout blocks in select() until ready or the
;; timeout elapses -- waits correctly without a racing busy-loop.
(check "listener ready within timeout after a connection arrives"
  (socket-ready? listener2 2000) #t)

(define accepted2 (tcp-accept listener2))
(define ain (car accepted2))

(check "accepted port ready once data arrives"
  (socket-ready? ain 500) #t)
(check "accepted port data readable"
  (read-line ain) "ping")

(close-port ain)
(close-port (cdr accepted2))

(check "socket-set-nonblocking! does not error"
  (guard (e (#t #f)) (socket-set-nonblocking! listener2) #t)
  #t)

(tcp-close listener2)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
