;;; Network module tests — currently covers UDP sender-address reporting (issue #16).
;;; TCP/TLS/non-blocking coverage lives on the fix/tcp-connect-ports, feat/tls-sockets,
;;; and feat/socket-nonblocking branches and will merge in alongside this file.

(import (curry network))
(import (curry sync))

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

;;; udp-recv returns (data host port), where host/port identify the sender
(define server (udp-socket))
(udp-bind server 19191)

(define client (udp-socket))
(udp-bind client 19192)

(define payload (string->utf8 "hello"))

(define sem (make-semaphore 0))
(define received #f)

(spawn (lambda ()
         (set! received (udp-recv server 1024))
         (sem-post! sem)))

(udp-send client payload "127.0.0.1" 19191)
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

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
