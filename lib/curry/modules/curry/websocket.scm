;;; (curry websocket) — a plain (no TLS) RFC 6455 WebSocket client, built
;;; entirely on curry's existing SRFI-106 sockets and (curry crypto)'s
;;; sha1/base64 -- no new C code. `ws://` only; `wss://` would need a TLS
;;; stream underneath the handshake/framing here, which SRFI-106 doesn't
;;; provide (see (curry network)'s own tls-connect for that piece, not yet
;;; wired into this module).
;;;
;;; This exists primarily as the transport (curry ros) speaks rosbridge
;;; over, but is a standalone, generally useful client on its own.

(define-library (curry websocket)
  (import (scheme base) (scheme write) (srfi s106 sockets) (curry crypto) (curry sync))
  (export
    ws-connect ws-send! ws-send-binary! ws-recv! ws-close! ws-closed?
    ws?)
  (begin

    ;; A GUID fixed by RFC 6455 itself -- concatenated with the client's
    ;; own Sec-WebSocket-Key before SHA-1/base64, both when generating the
    ;; request and when verifying the server's Sec-WebSocket-Accept reply.
    (define %ws-guid "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")

    (define-record-type <ws>
      (%make-ws socket in out closed? send-mutex)
      ws?
      (socket %ws-socket)
      (in     %ws-in)
      (out    %ws-out)
      (closed? %ws-closed? %ws-set-closed?!)
      (send-mutex %ws-send-mutex))

    (define (ws-closed? ws) (%ws-closed? ws))

    ;; ---- URL parsing: "ws://host[:port][/path]" -> (values host port path)

    (define (%parse-ws-url url)
      (define (fail) (error "ws-connect: not a ws:// URL" url))
      (if (< (string-length url) 5) (fail))
      (if (not (string=? (substring url 0 5) "ws://")) (fail))
      (let* ((rest (substring url 5 (string-length url)))
             (slash (%string-index rest #\/)))
        (let* ((hostport (if slash (substring rest 0 slash) rest))
               (path     (if slash (substring rest slash (string-length rest)) "/"))
               (colon    (%string-index hostport #\:)))
          (if colon
              (values (substring hostport 0 colon)
                      (string->number (substring hostport (+ colon 1) (string-length hostport)))
                      path)
              (values hostport 80 path)))))

    (define (%string-index s ch)
      (let loop ((i 0))
        (cond ((= i (string-length s)) #f)
              ((char=? (string-ref s i) ch) i)
              (else (loop (+ i 1))))))

    ;; ---- Handshake

    (define (%random-key-base64)
      (let ((bv (make-bytevector 16 0)))
        (let loop ((i 0))
          (if (< i 16)
              (begin (bytevector-u8-set! bv i (random-integer 256)) (loop (+ i 1)))))
        (base64-encode bv)))

    (define (%read-header-lines in)
      ;; Reads CRLF-terminated header lines up to (and consuming) the blank
      ;; line that ends an HTTP response, returning the lines seen (status
      ;; line first) with any trailing \r stripped. read-line's own notion
      ;; of "line" already stops at \n, so only \r needs trimming here.
      (let loop ((acc '()))
        (let ((line (read-line in)))
          (cond
            ((eof-object? line) (error "ws-connect: connection closed during handshake"))
            ((or (string=? line "") (string=? line "\r")) (reverse acc))
            (else (loop (cons (%chomp-cr line) acc)))))))

    (define (%chomp-cr s)
      (let ((n (string-length s)))
        (if (and (> n 0) (char=? (string-ref s (- n 1)) #\return))
            (substring s 0 (- n 1))
            s)))

    (define (%header-value lines name)
      ;; Case-insensitive match on "Name: value" lines, per HTTP.
      (let ((lname (%string-downcase name)))
        (let loop ((ls lines))
          (cond
            ((null? ls) #f)
            (else
             (let* ((line (car ls)) (colon (%string-index line #\:)))
               (if (and colon (string=? (%string-downcase (substring line 0 colon)) lname))
                   (%trim-leading-space (substring line (+ colon 1) (string-length line)))
                   (loop (cdr ls)))))))))

    (define (%string-downcase s) (list->string (map char-downcase (string->list s))))

    (define (%trim-leading-space s)
      (let loop ((i 0))
        (if (and (< i (string-length s)) (char=? (string-ref s i) #\space))
            (loop (+ i 1))
            (substring s i (string-length s)))))

    (define (ws-connect url)
      (call-with-values
       (lambda () (%parse-ws-url url))
       (lambda (host port path)
         (let* ((socket (make-client-socket host port))
                (in  (socket-input-port socket))
                (out (socket-output-port socket))
                (key (%random-key-base64)))
           (write-string
            (string-append
             "GET " path " HTTP/1.1\r\n"
             "Host: " host "\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: " key "\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n")
            out)
           (flush-output-port out)
           (let* ((lines (%read-header-lines in))
                  (status (car lines))
                  (accept (%header-value lines "Sec-WebSocket-Accept"))
                  (expect (base64-encode (sha1 (string->utf8 (string-append key %ws-guid))))))
             (if (not (%string-contains status "101"))
                 (error "ws-connect: server did not upgrade" status))
             (if (not (equal? accept expect))
                 (error "ws-connect: Sec-WebSocket-Accept mismatch" accept expect))
             (%make-ws socket in out #f (make-mutex)))))))

    (define (%string-contains s sub)
      (let ((sl (string-length s)) (bl (string-length sub)))
        (let loop ((i 0))
          (cond
            ((> (+ i bl) sl) #f)
            ((string=? (substring s i (+ i bl)) sub) #t)
            (else (loop (+ i 1)))))))

    ;; ---- Framing (RFC 6455 section 5)

    (define (%mask-key) (bytevector (random-integer 256) (random-integer 256)
                                     (random-integer 256) (random-integer 256)))

    (define (%mask! payload key)
      (let ((n (bytevector-length payload)))
        (let loop ((i 0))
          (if (< i n)
              (begin
                (bytevector-u8-set! payload i
                                     (bitwise-xor (bytevector-u8-ref payload i)
                                                  (bytevector-u8-ref key (modulo i 4))))
                (loop (+ i 1)))))))

    (define (%write-be bv start width n)
      (let loop ((i (- width 1)) (n n))
        (if (>= i 0)
            (begin (bytevector-u8-set! bv (+ start i) (bitwise-and n 255))
                   (loop (- i 1) (arithmetic-shift n -8))))))

    ;; Always masked -- every client-to-server frame MUST be masked per
    ;; RFC 6455; curry's own WebSocket client has no server role.
    ;;
    ;; A frame is written as three separate port writes (header, mask key,
    ;; payload); curry ports have no built-in per-write atomicity across
    ;; threads, so two actors calling ws-send!/ws-send-binary!/ws-close!
    ;; concurrently on the same connection (the normal (curry ros) usage
    ;; pattern -- one reader actor plus arbitrarily many publisher actors)
    ;; could otherwise interleave their frame bytes on the wire and
    ;; corrupt the stream for both. with-mutex serializes the whole
    ;; three-write sequence per connection.
    (define (%ws-write-frame! ws opcode payload)
      (with-mutex (%ws-send-mutex ws)
        (lambda ()
          (let* ((out (%ws-out ws))
                 (len (bytevector-length payload))
                 (key (%mask-key))
                 (header
                  (cond
                    ((< len 126) (let ((h (make-bytevector 2 0)))
                                   (bytevector-u8-set! h 0 (bitwise-or #x80 opcode))
                                   (bytevector-u8-set! h 1 (bitwise-or #x80 len))
                                   h))
                    ((< len 65536) (let ((h (make-bytevector 4 0)))
                                     (bytevector-u8-set! h 0 (bitwise-or #x80 opcode))
                                     (bytevector-u8-set! h 1 (bitwise-or #x80 126))
                                     (%write-be h 2 2 len)
                                     h))
                    (else (let ((h (make-bytevector 10 0)))
                            (bytevector-u8-set! h 0 (bitwise-or #x80 opcode))
                            (bytevector-u8-set! h 1 (bitwise-or #x80 127))
                            (%write-be h 2 8 len)
                            h))))
                 (masked (bytevector-copy payload)))
            (%mask! masked key)
            (write-bytevector header out)
            (write-bytevector key out)
            (write-bytevector masked out)
            (flush-output-port out)))))

    (define (ws-send! ws string)
      (if (%ws-closed? ws) (error "ws-send!: connection is closed"))
      (%ws-write-frame! ws #x1 (string->utf8 string)))

    (define (ws-send-binary! ws bv)
      (if (%ws-closed? ws) (error "ws-send-binary!: connection is closed"))
      (%ws-write-frame! ws #x2 bv))

    (define (%read-be in width)
      (let loop ((i 0) (acc 0))
        (if (= i width)
            acc
            (let ((b (read-u8 in)))
              (if (eof-object? b) (error "ws-recv!: connection closed mid-frame"))
              (loop (+ i 1) (+ (arithmetic-shift acc 8) b))))))

    ;; A peer (malicious, or just broken) can claim any length up to 2^63-1
    ;; in the extended-length field -- curry's numeric tower has no
    ;; overflow to catch this the way a fixed-width integer would, so
    ;; without an explicit cap a single crafted frame header can trigger
    ;; an attempted multi-exabyte allocation. 64MB is comfortably larger
    ;; than any single rosbridge JSON message this client is meant to
    ;; handle, while still bounding the worst case to something the
    ;; process can survive raising a clean error over.
    (define %ws-max-frame-len 67108864)

    ;; Returns (values opcode fin? payload-bytevector), or (values 'eof #t #f).
    (define (%read-frame ws)
      (let* ((in (%ws-in ws)) (b0 (read-u8 in)))
        (if (eof-object? b0)
            (values 'eof #t #f)
            (let* ((fin? (not (zero? (bitwise-and b0 #x80))))
                   (opcode (bitwise-and b0 #x0f))
                   (b1 (read-u8 in)))
              (if (eof-object? b1)
                  (values 'eof #t #f)
                  (let* ((masked? (not (zero? (bitwise-and b1 #x80))))
                         (len7 (bitwise-and b1 #x7f))
                         (len (cond ((= len7 126) (%read-be in 2))
                                    ((= len7 127) (%read-be in 8))
                                    (else len7))))
                    (if (> len %ws-max-frame-len)
                        (error "ws-recv!: frame length exceeds maximum" len %ws-max-frame-len)
                        (let* ((key (if masked? (read-bytevector 4 in) #f))
                               (payload (if (> len 0) (read-bytevector len in) (bytevector))))
                          (if (or (eof-object? payload) (and key (eof-object? key)))
                              (values 'eof #t #f)
                              (begin
                                ;; A conformant server never masks; unmask
                                ;; anyway if it did, rather than silently
                                ;; misreading the payload against a spec
                                ;; violation.
                                (if masked? (%mask! payload key))
                                (values opcode fin? payload)))))))))))

    ;; Reassembles fragmented messages (continuation frames), answers
    ;; pings with pongs, and swallows pongs -- all transparently to the
    ;; caller, who only ever sees a complete text/binary message or eof.
    (define (ws-recv! ws)
      (let loop ((first-opcode #f) (chunks '()))
        (call-with-values
         (lambda () (%read-frame ws))
         (lambda (opcode fin? payload)
           (cond
             ((eq? opcode 'eof)
              (%ws-set-closed?! ws #t)
              (eof-object))
             ((= opcode #x9) ; ping -> pong, keep waiting for real data
              (%ws-write-frame! ws #xA payload)
              (loop first-opcode chunks))
             ((= opcode #xA) ; pong -- nothing to do
              (loop first-opcode chunks))
             ((= opcode #x8) ; close
              (if (not (%ws-closed? ws)) (%ws-write-frame! ws #x8 (bytevector)))
              (%ws-set-closed?! ws #t)
              (eof-object))
             (else
              (let* ((msg-opcode (or first-opcode opcode))
                     (all-chunks (cons payload chunks)))
                (if fin?
                    (let ((full (%bytevector-concat-reverse all-chunks)))
                      (if (= msg-opcode #x1) (utf8->string full) full))
                    (loop msg-opcode all-chunks)))))))))

    (define (%bytevector-concat-reverse chunks-rev)
      (let loop ((cs (reverse chunks-rev)) (acc (bytevector)))
        (if (null? cs)
            acc
            (loop (cdr cs) (bytevector-append acc (car cs))))))

    (define (ws-close! ws)
      (if (not (%ws-closed? ws))
          (begin
            (%ws-write-frame! ws #x8 (bytevector))
            (%ws-set-closed?! ws #t)))
      (socket-close (%ws-socket ws)))

  )) ;; end begin, define-library
