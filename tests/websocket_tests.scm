;;; websocket_tests.scm — (curry websocket) runtime test against a
;;; hand-rolled, independent RFC 6455 server implementation.
;;;
;;; The server side here deliberately does NOT reuse any code from
;;; lib/curry/modules/curry/websocket.scm -- it's a second, from-scratch
;;; implementation of the wire protocol (handshake + framing, server
;;; role: unmasked outgoing frames, unmask incoming ones) built directly
;;; on (curry network)'s raw ports. The point is to prove the client
;;; module is wire-compatible with an independent RFC 6455 peer, the way
;;; it will need to be with a real rosbridge/ROS server, not merely
;;; self-consistent with its own encoder and decoder.

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

(define ws-guid "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")

;;; ---- minimal from-scratch server-side handshake + framing ----

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

(define (server-accept! in out)
  (let* ((lines (read-handshake in))
         (key (header-value lines "Sec-WebSocket-Key"))
         (accept (base64-encode (sha1 (string->utf8 (string-append key ws-guid))))))
    (write-string
     (string-append "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: " accept "\r\n\r\n")
     out)
    (flush-output-port out)))

(define (mask! bv key)
  (let ((n (bytevector-length bv)))
    (let loop ((i 0))
      (if (< i n)
          (begin (bytevector-u8-set! bv i (bitwise-xor (bytevector-u8-ref bv i)
                                                        (bytevector-u8-ref key (modulo i 4))))
                 (loop (+ i 1)))))))

;; Server frames are never masked (only client->server frames are).
(define (server-write-frame! out opcode fin? payload)
  (let* ((len (bytevector-length payload))
         (b0 (bitwise-or (if fin? #x80 0) opcode)))
    (write-u8 b0 out)
    (cond
      ((< len 126) (write-u8 len out))
      ((< len 65536)
       (write-u8 126 out)
       (write-u8 (bitwise-and (arithmetic-shift len -8) 255) out)
       (write-u8 (bitwise-and len 255) out))
      (else (error "server-write-frame!: test server doesn't support >64KB payloads")))
    (write-bytevector payload out)
    (flush-output-port out)))

;; Reads one client frame; client frames are always masked.
(define (server-read-frame! in)
  (let* ((b0 (read-u8 in)))
    (if (eof-object? b0)
        (values 'eof #t #f)
        (let* ((fin? (not (zero? (bitwise-and b0 #x80))))
               (opcode (bitwise-and b0 #x0f))
               (b1 (read-u8 in))
               (len7 (bitwise-and b1 #x7f))
               (len (cond ((= len7 126) (+ (* 256 (read-u8 in)) (read-u8 in)))
                          ((= len7 127) (error "server-read-frame!: 64-bit lengths not needed by this test"))
                          (else len7)))
               (key (read-bytevector 4 in))
               (payload (if (> len 0) (read-bytevector len in) (bytevector))))
          (mask! payload key)
          (values opcode fin? payload)))))

;;; ---- the mock server actor: canned protocol exchange for the test ----

;; Issue #110: a hardcoded port collides under CI parallel load (another
;; process, or a lingering listener from a previous test run, already
;; bound to it). 0 asks the OS for an arbitrary free ephemeral port;
;; socket-local-port reads back which one it actually picked, since that
;; isn't known until after bind() has already happened.
(define listener (tcp-listen 0))
(define test-port (socket-local-port listener))

(define server-thread
  (spawn (lambda ()
           (let* ((conn (tcp-accept listener)) (in (car conn)) (out (cdr conn)))
             (server-accept! in out)

             ;; 1. echo one text message back unchanged
             (call-with-values (lambda () (server-read-frame! in))
               (lambda (opcode fin? payload)
                 (server-write-frame! out #x1 #t payload)))

             ;; 2. send an unsolicited ping, then a real message; expect a
             ;;    pong to have arrived on the wire before the client's
             ;;    ws-recv! call returns the real message
             (server-write-frame! out #x9 #t (string->utf8 "pingdata"))
             (call-with-values (lambda () (server-read-frame! in))
               (lambda (opcode fin? payload)
                 (set! pass (if (and (= opcode #xA) (equal? (utf8->string payload) "pingdata"))
                                 (+ pass 1) pass))
                 (if (not (and (= opcode #xA) (equal? (utf8->string payload) "pingdata")))
                     (begin (display "FAIL: server saw pong echoing ping payload") (newline)
                            (set! fail (+ fail 1))))))
             (server-write-frame! out #x1 #t (string->utf8 "after-ping"))

             ;; 3. send a fragmented text message across two frames
             (server-write-frame! out #x1 #f (string->utf8 "Hello, "))
             (server-write-frame! out #x0 #t (string->utf8 "World!"))

             ;; 4. read a binary message from the client and echo it back
             (call-with-values (lambda () (server-read-frame! in))
               (lambda (opcode fin? payload)
                 (server-write-frame! out #x2 #t payload)))

             ;; 4.5 send a raw frame header claiming an absurd 64-bit
             ;; extended length (~34GB, no actual payload bytes follow) --
             ;; the client must raise a clean error rather than attempt
             ;; the allocation or hang trying to read bytes that were
             ;; never sent. Written directly (not via server-write-frame!,
             ;; which always sends a real matching payload).
             (write-u8 #x81 out)
             (write-u8 127 out)
             (write-bytevector (bytevector 0 0 0 0 8 0 0 0) out)
             (flush-output-port out)

             ;; 5. expect a close frame, reply in kind
             (call-with-values (lambda () (server-read-frame! in))
               (lambda (opcode fin? payload)
                 (if (= opcode #x8) (server-write-frame! out #x8 #t (bytevector)))))

             (close-port in) (close-port out)))))

;;; ---- drive the real client module against it ----

(define ws (ws-connect (string-append "ws://127.0.0.1:" (number->string test-port) "/")))

(check "ws? on a fresh connection" (ws? ws) #t)
(check "not closed right after connect" (ws-closed? ws) #f)

(ws-send! ws "hello")
(check "echo round-trip" (ws-recv! ws) "hello")

(check "ws-recv! transparently answers ping and returns the next real message"
       (ws-recv! ws) "after-ping")

(check "fragmented message reassembly" (ws-recv! ws) "Hello, World!")

(ws-send-binary! ws (bytevector 1 2 3 250 251 252))
(check "binary echo round-trip" (ws-recv! ws) (bytevector 1 2 3 250 251 252))

(check "oversized claimed frame length raises instead of hanging or crashing"
       (guard (e (#t 'raised)) (ws-recv! ws))
       'raised)

(ws-close! ws)
(check "ws-closed? after ws-close!" (ws-closed? ws) #t)

(tcp-close listener)

;;; ---- regression from independent review: RFC 6455 5.1 masking
;;; direction is a MUST-reject, not a tolerate-either-way convenience.
;;; A server-to-client frame with the mask bit set is a protocol
;;; violation the client must reject, not silently unmask and accept.

(define mask-listener (tcp-listen 0))
(define mask-test-port (socket-local-port mask-listener))

(define mask-server-thread
  (spawn (lambda ()
           (let* ((conn (tcp-accept mask-listener)) (in (car conn)) (out (cdr conn)))
             (server-accept! in out)
             ;; a text frame with the mask bit deliberately SET, plus a
             ;; (bogus, but present) mask key -- only a client may ever
             ;; legitimately send a masked frame; a server never should.
             (write-u8 #x81 out)
             (write-u8 (bitwise-or #x80 5) out)
             (write-bytevector (bytevector 1 2 3 4) out) ; mask key
             (write-bytevector (string->utf8 "hello") out)
             (flush-output-port out)
             (close-port in) (close-port out)))))

(define mask-ws (ws-connect (string-append "ws://127.0.0.1:" (number->string mask-test-port) "/")))
(check "ws-recv! rejects a masked frame from a server"
       (guard (e (#t 'raised)) (ws-recv! mask-ws))
       'raised)
(tcp-close mask-listener)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
