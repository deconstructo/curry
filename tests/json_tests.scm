;;; JSON module tests — encode/decode round-trips and malformed-input safety.

(import (curry json))

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

;;; Basic round-trips
(check "parse object"  (json-parse "{\"a\":1,\"b\":2}") '(("b" . 2) ("a" . 1)))
(check "parse array"   (json-parse "[1,2,3]") #(1 2 3))
(check "parse string"  (json-parse "\"hello\"") "hello")
(check "parse number"  (json-parse "42") 42)
(check "parse true"    (json-parse "true") #t)
(check "parse false"   (json-parse "false") #f)

;;; String escapes
(check "escape: quote"     (json-parse "\"a\\\"b\"") "a\"b")
(check "escape: backslash" (json-parse "\"a\\\\b\"") "a\\b")
(check "escape: newline"   (json-parse "\"a\\nb\"") "a\nb")
(check "unicode escape: valid, ASCII range"
  (json-parse "\"\\u0041\\u0042\"") "AB")

;;; Malformed-input safety — regression coverage for an out-of-bounds read
;;; found in a full-codebase audit: a trailing backslash at the very end
;;; of the input, or a truncated \u escape (fewer than 4 hex digits before
;;; the string ends), previously advanced the parse cursor unconditionally
;;; past the string's NUL terminator. Neither case should crash; both are
;;; malformed JSON, so the exact parsed value isn't the point — surviving
;;; without reading out of bounds is.
(check "malformed: trailing backslash does not crash"
  (json-parse "\"abc\\") "abc")
(check "malformed: truncated unicode escape does not crash"
  (json-parse "\"\\u") "")

;;; Port-level API (json-read/json-write) — generic curry_port_read_byte/
;;; curry_port_write_string based, so these should agree exactly with the
;;; string-based json-parse/json-stringify.

(check "json-read matches json-parse, given a string input port"
  (json-read (open-input-string "{\"a\":1,\"b\":2}"))
  (json-parse "{\"a\":1,\"b\":2}"))

(check "json-read matches json-parse for an array"
  (json-read (open-input-string "[1,2,3]"))
  (json-parse "[1,2,3]"))

(check "json-write matches json-stringify, given a string output port"
  (let ((out (open-output-string)))
    (json-write (list (cons "x" 1) (cons "y" "z")) out)
    (get-output-string out))
  (json-stringify (list (cons "x" 1) (cons "y" "z"))))

(check "json-write on a bytevector output port then json-read round-trips"
  (let ((out (open-output-bytevector)))
    (json-write (list (cons "n" 42)) out)
    (json-read (open-input-bytevector (get-output-bytevector out))))
  (json-parse "{\"n\":42}"))

(check "json-read on a non-port raises"
  (guard (e (#t 'raised)) (json-read "not a port") 'not-raised)
  'raised)

(check "json-write on a non-port raises"
  (guard (e (#t 'raised)) (json-write 5 "not a port") 'not-raised)
  'raised)

;;; File convenience wrappers

;; json-load-file re-parses through json-parse's own alist builder, which
;; (like "parse object" above) prepends key/value pairs while parsing —
;; the round-tripped alist's key order is reversed from the original, same
;; as any other json-parse result. Written already in that expected order
;; rather than re-asserting the reversal is a bug it isn't.
(let* ((path (string-append "/tmp/curry-json-file-roundtrip-test-" (number->string (current-jiffy)) ".json"))
       (doc (list (cons "title" "Example") (cons "nums" (vector 1 2 3)))))
  (json-dump-file doc path)
  (check "json-load-file . json-dump-file round trip"
         (json-load-file path)
         (list (cons "nums" (vector 1 2 3)) (cons "title" "Example")))
  (delete-file path))

(check "json-load-file on a missing path raises"
  (guard (e (#t 'raised)) (json-load-file "/nonexistent/path/does-not-exist.json") 'not-raised)
  'raised)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
