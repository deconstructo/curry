;;; (curry network) tests — TCP socket-as-port round-trip.
;;;
;;; Self-contained: listens on loopback, connects to itself, exercises
;;; read-line/write-string bidirectionally. No external network access
;;; needed. Regression coverage for tcp-connect/tcp-accept actually
;;; returning ports (they used to silently discard the FILE* streams
;;; they created and return a raw fd pair instead).

(import (curry network))
(import (curry sync))
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
;;; § 3  tcp-connect-tls: cert verification (badssl.com — a public
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

      ;; A valid, chain-trusted cert issued for a *different* hostname —
      ;; the specific case SSL_get_verify_result alone does not catch;
      ;; only SSL_set1_host's hostname verification does.
      (check "valid-but-wrong-hostname cert is rejected"
        (guard (e (#t #t)) (tcp-connect-tls "wrong.host.badssl.com" 443) #f)
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
;;; § 4  udp-recv reports sender host/port; dual-stack udp-socket
;;; ════════════════════════════════════════════════════════════

(define server (udp-socket))
(udp-bind server 19191)

(define client2 (udp-socket))
(udp-bind client2 19192)

(define sem (make-semaphore 0))
(define received #f)

(spawn (lambda ()
         (set! received (udp-recv server 1024))
         (sem-post! sem)))

(udp-send client2 (string->utf8 "hello") "127.0.0.1" 19191)
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
;;; Malformed raw socket handle rejection (issue #158)
;;; ════════════════════════════════════════════════════════════
;;;
;;; net_val_to_sock (network_internal.h) unconditionally read
;;; sizeof(sock_t) bytes from a raw socket handle's bytevector with no
;;; length check of its own, and net_is_raw_socket_handle never checked
;;; the cdr was even a bytevector at all -- a curry script constructing
;;; a pair merely SHAPED like a raw socket handle (car the symbol
;;; 'socket) could trigger an out-of-bounds heap read (too-short/empty
;;; bytevector) or a type-confused read (a non-bytevector cdr entirely),
;;; whose garbage result then fed straight into a real syscall as an fd.
;;; Every one of these must now be rejected cleanly at the type-check
;;; level (net_is_raw_socket_handle), before ever reaching
;;; net_val_to_sock, rather than crashing, reading adjacent heap memory,
;;; or silently proceeding with a garbage fd.
(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

(check "socket-local-port rejects a too-short bytevector cdr (was an OOB heap read)"
  (raises? (lambda () (socket-local-port (cons 'socket (make-bytevector 1 5))))) #t)
(check "socket-local-port rejects an empty bytevector cdr (was an OOB heap read)"
  (raises? (lambda () (socket-local-port (cons 'socket (make-bytevector 0))))) #t)
(check "socket-local-port rejects a non-bytevector cdr (was a type-confused read)"
  (raises? (lambda () (socket-local-port (cons 'socket 42)))) #t)
(check "socket-local-port rejects a too-long bytevector cdr"
  (raises? (lambda () (socket-local-port (cons 'socket (make-bytevector 16 0))))) #t)
;; A correctly-sized bytevector holding a garbage fd value is NOT a
;; malformed-handle rejection -- it passes the shape/length check (as it
;; must, since a real socket handle has exactly this shape) and instead
;; fails later at the actual syscall (getsockname: Bad file descriptor),
;; a different and already-correct error path this fix doesn't change.
(check "socket-local-port on a correctly-sized garbage fd still raises (via the syscall, not the shape check)"
  (raises? (lambda () (socket-local-port (cons 'socket (make-bytevector 4 255))))) #t)
;; A genuine socket handle (from tcp-listen) must still work normally --
;; the stricter check must not reject legitimate handles.
(let* ((l (tcp-listen 0)))
  (check "socket-local-port still works on a genuine tcp-listen handle"
    (> (socket-local-port l) 0) #t)
  (tcp-close l))

;;; The above only covered socket-local-port (an SRFI-106 primitive
;;; already routed through net_extract_fd). Independent code review
;;; found the fix was INCOMPLETE: network.c's tcp-accept, tcp-close,
;;; udp-bind, udp-send, and udp-recv each called net_val_to_sock
;;; directly (via the raw #define val_to_sock alias) with NO check at
;;; all, entirely bypassing net_is_raw_socket_handle -- reproduced as an
;;; actual SIGSEGV (not just a clean error) for tcp-close/udp-bind on a
;;; malformed handle. Fixed by routing all five through a new shared
;;; net_checked_val_to_sock helper (network_internal.h) instead of the
;;; raw, unchecked val_to_sock/net_val_to_sock. Every one of these must
;;; now reject a malformed handle cleanly too, not just crash-free but
;;; via guard-catchable error, same as socket-local-port above.
(check "tcp-accept rejects a non-bytevector cdr (was an unchecked type-confused read)"
  (raises? (lambda () (tcp-accept (cons 'socket "x")))) #t)
(check "tcp-close rejects a non-bytevector cdr (was a reproducible SIGSEGV)"
  (raises? (lambda () (tcp-close (cons 'socket 42)))) #t)
(check "udp-bind rejects a non-bytevector cdr (was a reproducible SIGSEGV)"
  (raises? (lambda () (udp-bind (cons 'socket 42) 0))) #t)
(check "udp-send rejects an empty bytevector cdr (was an unchecked OOB read feeding sendto)"
  (raises? (lambda () (udp-send (cons 'socket (make-bytevector 0)) (make-bytevector 1) "127.0.0.1" 9)))
  #t)
(check "udp-recv rejects an empty bytevector cdr (was an unchecked OOB read feeding recvfrom)"
  (raises? (lambda () (udp-recv (cons 'socket (make-bytevector 0)) 10))) #t)
;; Genuine handles must still work normally on all five -- confirmed
;; implicitly by every other test in this file (tcp-accept/tcp-close via
;; § 1/§ 2, udp-bind/udp-send/udp-recv via § 4), all of which already ran
;; successfully above using real tcp-listen/udp-socket handles.

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
