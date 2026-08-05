;;; (curry base64) tests — RFC 4648 encode/decode, string/bytevector/port forms.

(import (curry base64))

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

;;; =========================================================================
;;; RFC 4648 test vectors (the spec's own worked examples)
;;; =========================================================================

(check "encode empty" (base64-encode-string "") "")
(check "encode f" (base64-encode-string "f") "Zg==")
(check "encode fo" (base64-encode-string "fo") "Zm8=")
(check "encode foo" (base64-encode-string "foo") "Zm9v")
(check "encode foob" (base64-encode-string "foob") "Zm9vYg==")
(check "encode fooba" (base64-encode-string "fooba") "Zm9vYmE=")
(check "encode foobar" (base64-encode-string "foobar") "Zm9vYmFy")

(check "decode empty" (base64-decode-string "") "")
(check "decode Zg==" (base64-decode-string "Zg==") "f")
(check "decode Zm8=" (base64-decode-string "Zm8=") "fo")
(check "decode Zm9v" (base64-decode-string "Zm9v") "foo")
(check "decode Zm9vYg==" (base64-decode-string "Zm9vYg==") "foob")
(check "decode Zm9vYmE=" (base64-decode-string "Zm9vYmE=") "fooba")
(check "decode Zm9vYmFy" (base64-decode-string "Zm9vYmFy") "foobar")

;;; =========================================================================
;;; Round trips
;;; =========================================================================

(define (round-trip-string s) (base64-decode-string (base64-encode-string s)))

(check "round trip ASCII sentence"
       (round-trip-string "The quick brown fox jumps over the lazy dog.")
       "The quick brown fox jumps over the lazy dog.")

(check "round trip multi-byte UTF-8" (round-trip-string "héllo wörld — 日本語") "héllo wörld — 日本語")

(define all-bytes
  (let ((bv (make-bytevector 256)))
    (let loop ((i 0)) (when (< i 256) (bytevector-u8-set! bv i i) (loop (+ i 1))))
    bv))

(check "round trip all 256 byte values" (base64-decode (base64-encode all-bytes)) all-bytes)

;;; =========================================================================
;;; Whitespace tolerance and invalid input
;;; =========================================================================

(check "decode tolerates embedded whitespace/newlines"
       (base64-decode-string "Zm9v\nYmFy")
       "foobar")

(check "decode tolerates leading/trailing whitespace"
       (base64-decode-string "  Zm9vYmFy  \n")
       "foobar")

(check "decode rejects invalid character"
       (guard (e (#t 'raised))
         (base64-decode-string "Zm9v!!!!")
         'not-raised)
       'raised)

;;; =========================================================================
;;; Port-level API directly
;;; =========================================================================

(define (encode-via-ports bv)
  (let ((in (open-input-bytevector bv)) (out (open-output-string)))
    (base64-encode-port in out)
    (get-output-string out)))

(define (decode-via-ports s)
  (let ((in (open-input-string s)) (out (open-output-bytevector)))
    (base64-decode-port in out)
    (get-output-bytevector out)))

(check "base64-encode-port matches base64-encode"
       (encode-via-ports (string->utf8 "streaming test"))
       (base64-encode (string->utf8 "streaming test")))

(check "base64-decode-port matches base64-decode"
       (decode-via-ports "c3RyZWFtaW5nIHRlc3Q=")
       (base64-decode "c3RyZWFtaW5nIHRlc3Q="))

;;; A source longer than one 3-byte group, exercised through the port API
;;; directly, to make sure the loop in base64-encode-port actually iterates
;;; (not just handles a single group).
(define long-bv (make-bytevector 100))
(let loop ((i 0)) (when (< i 100) (bytevector-u8-set! long-bv i (modulo i 251)) (loop (+ i 1))))
(check "encode/decode round trip over many groups (port API)"
       (decode-via-ports (encode-via-ports long-bv))
       long-bv)

;;; =========================================================================

(newline)
(display "Total: ") (display (+ pass fail)) (display " Pass: ") (display pass)
(display " Fail: ") (display fail) (newline)
(if (> fail 0) (exit 1))
