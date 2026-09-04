;;; ros_tests.scm — (curry ros) runtime test against a hand-rolled mock
;;; rosbridge server (WebSocket framing + the rosbridge v2.0 JSON
;;; protocol), independent of (curry websocket)'s own client-side
;;; implementation for the same reason websocket_tests.scm's server is:
;;; proving real wire compatibility, not just self-consistency.

(import (scheme base) (curry network) (curry sync) (curry crypto) (curry json) (curry ros))

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

;;; ---- minimal from-scratch server-side WS handshake + text framing ----
;;; (deliberately independent of lib/curry/modules/curry/websocket.scm --
;;; see websocket_tests.scm for the same rationale)

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
                     "Upgrade: websocket\r\nConnection: Upgrade\r\n"
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

(define (server-send-text! out str)
  (let* ((payload (string->utf8 str)) (len (bytevector-length payload)))
    (write-u8 (bitwise-or #x80 #x1) out)
    (cond ((< len 126) (write-u8 len out))
          (else (write-u8 126 out)
                (write-u8 (bitwise-and (arithmetic-shift len -8) 255) out)
                (write-u8 (bitwise-and len 255) out)))
    (write-bytevector payload out)
    (flush-output-port out)))

;; Reads one client (masked) text frame and returns its decoded string.
(define (server-recv-text! in)
  (let* ((b0 (read-u8 in)) (opcode (bitwise-and b0 #x0f)) (b1 (read-u8 in))
         (len7 (bitwise-and b1 #x7f))
         (len (if (= len7 126) (+ (* 256 (read-u8 in)) (read-u8 in)) len7))
         (key (read-bytevector 4 in))
         (payload (if (> len 0) (read-bytevector len in) (bytevector))))
    (mask! payload key)
    (utf8->string payload)))

;;; ---- the mock rosbridge server: a fixed scripted exchange ----

;; Issue #110: a hardcoded port collides under CI parallel load. 0 asks
;; the OS for an arbitrary free ephemeral port; socket-local-port reads
;; back which one it actually picked.
(define listener (tcp-listen 0))
(define test-port (socket-local-port listener))
(define seen (make-hash-table)) ; label -> decoded JSON message, for later checks
(define server-done (make-semaphore 0))

(define server-thread
  (spawn (lambda ()
           (let* ((conn (tcp-accept listener)) (in (car conn)) (out (cdr conn)))
             (server-accept! in out)

             ;; 1. expect a subscribe op for /topic1
             (hash-table-set! seen "subscribe" (json-parse (server-recv-text! in)))

             ;; 2. publish a message on /topic1 for the client to receive
             (server-send-text! out (json-stringify
                                      (list (cons "op" "publish") (cons "topic" "/topic1")
                                            (cons "msg" (list (cons "data" "hello"))))))

             ;; 3. expect a publish op from the client on /topic2
             (hash-table-set! seen "publish" (json-parse (server-recv-text! in)))

             ;; 4. expect a call_service op, respond with a service_response
             (let* ((req (json-parse (server-recv-text! in)))
                    (id (cdr (assoc "id" req))))
               (hash-table-set! seen "call_service" req)
               (server-send-text! out (json-stringify
                                        (list (cons "op" "service_response")
                                              (cons "id" id)
                                              (cons "service" "/add_two_ints")
                                              (cons "values" (list 5))
                                              (cons "result" #t)))))

             ;; 5. expect an advertise_service op, then act as the caller:
             ;;    send a call_service TO the client and check its reply
             (hash-table-set! seen "advertise_service" (json-parse (server-recv-text! in)))
             (server-send-text! out (json-stringify
                                      (list (cons "op" "call_service") (cons "id" "srv1")
                                            (cons "service" "/echo") (cons "args" '()))))
             (hash-table-set! seen "service_response_from_client" (json-parse (server-recv-text! in)))
             (sem-post! server-done)

             (close-port in) (close-port out)))))

;;; ---- drive the real client module against it ----

(define (%field alist name)
  (let ((p (assoc name alist))) (if p (cdr p) #f)))

(define conn (ros-connect (string-append "ws://127.0.0.1:" (number->string test-port) "/")))

(define topic1-message #f)
(define got-topic1 (make-semaphore 0))

(ros-subscribe! conn "/topic1"
  (lambda (msg) (set! topic1-message msg) (sem-post! got-topic1)))

;; Blocks (no busy loop) until the subscribe callback above has run once.
(sem-wait! got-topic1)
(check "subscribed message delivered to callback" topic1-message (list (cons "data" "hello")))

(ros-publish! conn "/topic2" (list (cons "data" "world")))

;; json-parse decodes JSON arrays as vectors (see (curry json)'s own
;; documented convention), so "values"/"args" round-tripped through the
;; wire come back as vectors even though json-stringify happily accepted
;; plain lists going out.
(call-with-values
 (lambda () (ros-call-service conn "/add_two_ints" (list 2 3)))
 (lambda (ok? values)
   (check "call_service result flag" ok? #t)
   (check "call_service returned values" values (vector 5))))

(ros-advertise-service! conn "/echo" "std_srvs/Trigger"
  (lambda (args) (values #t (list "echoed"))))

;; Blocks until the server actor has received advertise_service, issued
;; its own call_service, and received our reply.
(sem-wait! server-done)

(define (seen-ref label) (hash-table-ref seen label #f))

(check "server saw the subscribe op" (%field (seen-ref "subscribe") "topic") "/topic1")
(check "server saw the publish op" (%field (seen-ref "publish") "msg") (list (cons "data" "world")))
(check "server saw the call_service op"
       (list (%field (seen-ref "call_service") "service") (%field (seen-ref "call_service") "args"))
       (list "/add_two_ints" (vector 2 3)))
(check "server saw the advertise_service op" (%field (seen-ref "advertise_service") "service") "/echo")
(check "client answered the server-initiated call_service correctly"
       (list (%field (seen-ref "service_response_from_client") "result")
             (%field (seen-ref "service_response_from_client") "values"))
       (list #t (vector "echoed")))

(ros-close! conn)
(tcp-close listener)

;;; ---- regression: a dropped connection must wake a no-timeout
;;; ros-call-service instead of hanging it forever ----

(define close-test-listener (tcp-listen 0))
(define close-test-port (socket-local-port close-test-listener))

;; Accepts the WS handshake, then closes immediately without ever
;; answering the call_service the client is about to send.
(define close-test-server
  (spawn (lambda ()
           (let* ((c (tcp-accept close-test-listener)) (in (car c)) (out (cdr c)))
             (server-accept! in out)
             (close-port in) (close-port out)))))

(define close-test-conn
  (ros-connect (string-append "ws://127.0.0.1:" (number->string close-test-port) "/")))
(define close-test-done (make-semaphore 0))
(define close-test-result #f)

(spawn (lambda ()
         (call-with-values
          (lambda () (ros-call-service close-test-conn "/never" (list)))
          (lambda (ok? values)
            (set! close-test-result (list ok? values))
            (sem-post! close-test-done)))))

;; If the connection-loss wakeup were missing, this call would block
;; forever and the whole test process would hang here.
(sem-wait! close-test-done)
(check "ros-call-service returns (values #f '()) when the connection drops mid-call, instead of hanging"
       close-test-result (list #f '()))

(tcp-close close-test-listener)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
