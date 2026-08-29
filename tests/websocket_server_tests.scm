;;; websocket_server_tests.scm — (curry websocket)'s SERVER role
;;; (ws-listen/ws-accept) tested against a hand-rolled, independent
;;; RFC 6455 CLIENT implementation.
;;;
;;; websocket_tests.scm proves the CLIENT role (ws-connect) is wire-
;;; compatible with an independent server. This file proves the reverse:
;;; the independent peer here plays the client (masks outgoing frames,
;;; expects unmasked incoming ones, does its own handshake key/accept
;;; computation) and deliberately does NOT reuse ws-connect or any
;;; framing code from lib/curry/modules/curry/websocket.scm -- the point
;;; is to prove ws-listen/ws-accept genuinely speak RFC 6455 to any
;;; compliant client, not just to curry's own.

(import (scheme base) (curry network) (curry sync) (curry crypto) (curry websocket))

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

(define test-port 17988)
(define ws-guid "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")

;;; ---- minimal from-scratch client-side handshake + framing ----

(define (chomp-cr s)
  (let ((n (string-length s)))
    (if (and (> n 0) (char=? (string-ref s (- n 1)) #\return)) (substring s 0 (- n 1)) s)))

(define (string-index s ch)
  (let loop ((i 0))
    (cond ((= i (string-length s)) #f)
          ((char=? (string-ref s i) ch) i)
          (else (loop (+ i 1))))))

(define (header-value lines name)
  (let loop ((ls lines))
    (cond
      ((null? ls) #f)
      (else
       (let* ((line (car ls)) (colon (string-index line #\:)))
         (if (and colon (string=? (substring line 0 colon) name))
             (substring line (+ colon 2) (string-length line))
             (loop (cdr ls))))))))

(define (read-handshake in)
  (let loop ((acc '()))
    (let ((line (chomp-cr (read-line in))))
      (if (string=? line "") (reverse acc) (loop (cons line acc))))))

(define (mask! bv key)
  (let ((n (bytevector-length bv)))
    (let loop ((i 0))
      (if (< i n)
          (begin (bytevector-u8-set! bv i (bitwise-xor (bytevector-u8-ref bv i)
                                                        (bytevector-u8-ref key (modulo i 4))))
                 (loop (+ i 1)))))))

;; Client frames MUST be masked.
(define (client-write-frame! out opcode fin? payload)
  (let* ((len (bytevector-length payload))
         (key (bytevector (random-integer 256) (random-integer 256)
                           (random-integer 256) (random-integer 256)))
         (masked (bytevector-copy payload)))
    (mask! masked key)
    (write-u8 (bitwise-or (if fin? #x80 0) opcode) out)
    (cond
      ((< len 126) (write-u8 (bitwise-or #x80 len) out))
      ((< len 65536)
       (write-u8 (bitwise-or #x80 126) out)
       (write-u8 (bitwise-and (arithmetic-shift len -8) 255) out)
       (write-u8 (bitwise-and len 255) out))
      (else (error "client-write-frame!: test client doesn't support >64KB payloads")))
    (write-bytevector key out)
    (write-bytevector masked out)
    (flush-output-port out)))

;; Reads one server frame; server frames MUST NOT be masked -- checked,
;; not just assumed, since ws-listen/ws-accept genuinely never masking
;; is exactly the property this whole file exists to verify.
(define (client-read-frame! in)
  (let* ((b0 (read-u8 in)))
    (if (eof-object? b0)
        (values 'eof #t #f)
        (let* ((fin? (not (zero? (bitwise-and b0 #x80))))
               (opcode (bitwise-and b0 #x0f))
               (b1 (read-u8 in))
               (masked? (not (zero? (bitwise-and b1 #x80))))
               (len7 (bitwise-and b1 #x7f))
               (len (cond ((= len7 126) (+ (* 256 (read-u8 in)) (read-u8 in)))
                          ((= len7 127) (error "client-read-frame!: 64-bit lengths not needed by this test"))
                          (else len7)))
               (payload (if (> len 0) (read-bytevector len in) (bytevector))))
          (set! pass (if masked? pass (+ pass 1)))
          (if masked?
              (begin (display "FAIL: server frame was masked (must never be)") (newline)
                     (set! fail (+ fail 1))))
          (values opcode fin? payload)))))

;;; ---- the real server under test: ws-listen / ws-accept ----

(define listener (ws-listen test-port))

(define server-thread
  (spawn (lambda ()
           (let ((conn (ws-accept listener)))
             (set! pass (if (equal? (ws-path conn) "/live-data") (+ pass 1) pass))
             (if (not (equal? (ws-path conn) "/live-data"))
                 (begin (display "FAIL: ws-path mismatch: ") (display (ws-path conn)) (newline)
                        (set! fail (+ fail 1))))

             ;; echo one message
             (ws-send! conn (ws-recv! conn))

             ;; echo a binary message
             (ws-send-binary! conn (ws-recv! conn))

             ;; expect a close frame; ws-recv! itself replies in kind and
             ;; marks the connection closed, matching the client role's
             ;; own close-handshake behavior in websocket_tests.scm
             (ws-recv! conn)
             (ws-close! conn)))))

;;; ---- drive an independent hand-rolled client against it ----

(define client-socket (make-client-socket "127.0.0.1" test-port))
(define cin (socket-input-port client-socket))
(define cout (socket-output-port client-socket))

(define %key "dGhlIHNhbXBsZSBub25jZQ==") ; fixed test key, same one RFC 6455 itself uses as an example

(write-string
 (string-append "GET /live-data HTTP/1.1\r\n"
                 "Host: 127.0.0.1\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Key: " %key "\r\n"
                 "Sec-WebSocket-Version: 13\r\n\r\n")
 cout)
(flush-output-port cout)

(define %response-lines (read-handshake cin))
(check "server responds 101" (car %response-lines) "HTTP/1.1 101 Switching Protocols")
(define %expect-accept (base64-encode (sha1 (string->utf8 (string-append %key ws-guid)))))
(check "Sec-WebSocket-Accept matches RFC 6455's own worked example"
       (header-value %response-lines "Sec-WebSocket-Accept")
       %expect-accept)
;; RFC 6455 section 1.3 gives this exact key/accept pair as its worked example.
(check "Sec-WebSocket-Accept is the RFC's own documented value"
       %expect-accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

(client-write-frame! cout #x1 #t (string->utf8 "hello server"))
(call-with-values (lambda () (client-read-frame! cin))
  (lambda (opcode fin? payload)
    (check "echo round-trip" (utf8->string payload) "hello server")))

(client-write-frame! cout #x2 #t (bytevector 9 8 7 6))
(call-with-values (lambda () (client-read-frame! cin))
  (lambda (opcode fin? payload)
    (check "binary echo round-trip" payload (bytevector 9 8 7 6))))

(client-write-frame! cout #x8 #t (bytevector))
(call-with-values (lambda () (client-read-frame! cin))
  (lambda (opcode fin? payload)
    (check "server replies to a close frame in kind" opcode #x8)))

(close-port cin) (close-port cout) (socket-close client-socket)
(ws-listener-close! listener)

;;; ---- regressions from independent review ----

;; 1. An unbounded header line/count must not be able to drive ws-accept
;; into unbounded memory growth or a hang -- a client that sends a huge
;; unterminated "line" (or, separately, unboundedly many header lines)
;; must get a clean, bounded-cost rejection instead.
(define cap-listener (ws-listen 17987))
(define cap-done (make-semaphore 0))
(define cap-result #f)

(define cap-acceptor
  (spawn (lambda ()
           (guard (e (#t (set! cap-result (list 'raised (error-object-message e)))))
             (let ((conn (ws-accept cap-listener)))
               (set! cap-result (list 'no-error conn))))
           (sem-post! cap-done))))

(let* ((sock (make-client-socket "127.0.0.1" 17987))
       (out (socket-output-port sock)))
  ;; 20,000 bytes, no newline at all -- well past the per-line cap,
  ;; with the connection kept open so a missing cap would hang forever
  ;; waiting for more input rather than erroring.
  (write-string (make-string 20000 #\a) out)
  (flush-output-port out))

(sem-wait! cap-done)
(check "ws-accept rejects an oversized unterminated header line"
       (car cap-result) 'raised)
(ws-listener-close! cap-listener)

;; 2. RFC 6455 5.1: a server MUST reject an unmasked frame from a
;; client (masking is a security requirement, not wire-format
;; decoration -- see the module's own comment on %ws-protocol-error!).
(define mask-listener (ws-listen 17986))
(define mask-done (make-semaphore 0))
(define mask-result #f)

(define mask-acceptor
  (spawn (lambda ()
           (guard (e (#t (set! mask-result (list 'raised (error-object-message e)))))
             (let ((conn (ws-accept mask-listener)))
               (set! mask-result (list 'no-error (ws-recv! conn)))))
           (sem-post! mask-done))))

(let* ((sock (make-client-socket "127.0.0.1" 17986))
       (in (socket-input-port sock))
       (out (socket-output-port sock))
       (key "dGhlIHNhbXBsZSBub25jZQ=="))
  (write-string
   (string-append "GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
                   "Connection: Upgrade\r\nSec-WebSocket-Key: " key "\r\n"
                   "Sec-WebSocket-Version: 13\r\n\r\n")
   out)
  (flush-output-port out)
  (let loop () (let ((l (read-line in))) (if (not (string=? l "\r")) (loop))))
  ;; a text frame with the mask bit deliberately clear -- a protocol
  ;; violation only a client is allowed to commit (servers must never
  ;; mask; clients must always mask)
  (write-u8 #x81 out)
  (write-u8 5 out)
  (write-bytevector (string->utf8 "hello") out)
  (flush-output-port out))

(sem-wait! mask-done)
(check "ws-accept's connection rejects an unmasked client frame"
       (car mask-result) 'raised)
(ws-listener-close! mask-listener)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
