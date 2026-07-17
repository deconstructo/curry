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
;;; § 2  tcp-connect-tls: cert verification (badssl.com — a public
;;;      service purpose-built for exactly this kind of test) and a real
;;;      HTTPS request/response round-trip. Skips cleanly if there's no
;;;      network access (DNS/connect failure), rather than failing the
;;;      whole suite on an offline CI runner.
;;; ════════════════════════════════════════════════════════════

(define (network-reachable?)
  (guard (e (#t #f))
    (let ((conn (tcp-connect "example.com" 80)))
      (close-port (car conn)) (close-port (cdr conn))
      #t)))

(if (not (network-reachable?))
    (begin (display "SKIP: no network access — skipping TLS tests") (newline))
    (begin
      (check "self-signed cert is rejected"
        (guard (e (#t #t)) (tcp-connect-tls "self-signed.badssl.com" 443) #f)
        #t)

      (check "valid cert is accepted, HTTPS round-trip works"
        (let* ((conn (tcp-connect-tls "example.com" 443))
               (in (car conn)) (out (cdr conn)))
          (write-string "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n" out)
          (flush-output-port out)
          (let ((status-line (read-line in)))
            (close-port in) (close-port out)
            (and (string? status-line)
                 (>= (string-length status-line) 12)
                 (string=? (substring status-line 0 5) "HTTP/"))))
        #t)))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
