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
;;; § 2  udp-recv reports sender host/port; dual-stack udp-socket
;;; ════════════════════════════════════════════════════════════

(import (curry sync))

(define server (udp-socket))
(udp-bind server 19191)

(define client (udp-socket))
(udp-bind client 19192)

(define sem (make-semaphore 0))
(define received #f)

(spawn (lambda ()
         (set! received (udp-recv server 1024))
         (sem-post! sem)))

(udp-send client (string->utf8 "hello") "127.0.0.1" 19191)
(sem-wait! sem)

(check "udp-recv data"        (utf8->string (car received)) "hello")
(check "udp-recv sender host" (cadr received) "127.0.0.1")
(check "udp-recv sender port" (caddr received) 19192)

;;; udp-send must work on a bare udp-socket (no prior udp-bind) sending to an
;;; IPv4 destination — regression test for the dual-stack V6ONLY fix.
(define unbound-client (udp-socket))
(define received2 #f)
(define sem2 (make-semaphore 0))

(spawn (lambda ()
         (set! received2 (udp-recv server 1024))
         (sem-post! sem2)))

(udp-send unbound-client (string->utf8 "world") "127.0.0.1" 19191)
(sem-wait! sem2)

(check "udp-send from unbound socket" (utf8->string (car received2)) "world")

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
