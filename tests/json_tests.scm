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

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
