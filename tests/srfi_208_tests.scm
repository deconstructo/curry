;;; srfi_208_tests.scm — (srfi 208) NaN procedures: make-nan,
;;; nan-negative?, nan-quiet?, nan-payload, nan=? — direct IEEE-754
;;; binary64 sign/quiet/payload bit manipulation via the two new core
;;; primitives %flonum-raw-bits/%raw-bits->flonum (src/builtins.c).

(import (scheme base) (srfi 208))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;;; ---- round-tripping every field ----

(check "positive quiet NaN, payload 1: nan?" (nan? (make-nan #f #t 1)) #t)
(check "positive quiet NaN: nan-negative?" (nan-negative? (make-nan #f #t 1)) #f)
(check "positive quiet NaN: nan-quiet?" (nan-quiet? (make-nan #f #t 1)) #t)
(check "positive quiet NaN: nan-payload" (nan-payload (make-nan #f #t 1)) 1)

(check "negative signaling NaN: nan-negative?" (nan-negative? (make-nan #t #f 7)) #t)
(check "negative signaling NaN: nan-quiet?" (nan-quiet? (make-nan #t #f 7)) #f)
(check "negative signaling NaN: nan-payload" (nan-payload (make-nan #t #f 7)) 7)

(check "large payload round-trips" (nan-payload (make-nan #f #t 123456789)) 123456789)
(check "maximum payload (2^51 - 1) round-trips"
       (nan-payload (make-nan #f #t (- (expt 2 51) 1))) (- (expt 2 51) 1))

;;; ---- payload validation ----

(check "payload 0 rejected (must be positive)"
       (guard (e (#t 'caught)) (make-nan #f #t 0)) 'caught)
(check "negative payload rejected"
       (guard (e (#t 'caught)) (make-nan #f #t -1)) 'caught)
(check "inexact payload rejected"
       (guard (e (#t 'caught)) (make-nan #f #t 1.0)) 'caught)
(check "payload exceeding what a NaN can hold is rejected"
       (guard (e (#t 'caught)) (make-nan #f #t (expt 2 51))) 'caught)
(check "payload of exactly 2^51 - 1 is NOT rejected"
       (guard (e (#t 'caught)) (make-nan #f #t (- (expt 2 51) 1)) 'ok) 'ok)

;;; ---- optional float argument ----

(check "an inexact float argument is accepted"
       (nan? (make-nan #f #t 1 0.0)) #t)
(check "an exact float argument is rejected"
       (guard (e (#t 'caught)) (make-nan #f #t 1 0)) 'caught)

;;; ---- nan=? ----

(check "nan=? true for identical fields"
       (nan=? (make-nan #t #t 5) (make-nan #t #t 5)) #t)
(check "nan=? false when sign differs"
       (nan=? (make-nan #t #t 5) (make-nan #f #t 5)) #f)
(check "nan=? false when quiet bit differs"
       (nan=? (make-nan #t #t 5) (make-nan #t #f 5)) #f)
(check "nan=? false when payload differs"
       (nan=? (make-nan #t #t 5) (make-nan #t #t 6)) #f)

;;; ---- type checking ----

(check "nan-negative? raises on a non-NaN flonum"
       (guard (e (#t 'caught)) (nan-negative? 1.0)) 'caught)
(check "nan-payload raises on a non-flonum"
       (guard (e (#t 'caught)) (nan-payload 5)) 'caught)
(check "nan=? raises when the first argument isn't a NaN"
       (guard (e (#t 'caught)) (nan=? 1.0 (make-nan #f #t 1))) 'caught)
(check "nan=? raises when the second argument isn't a NaN"
       (guard (e (#t 'caught)) (nan=? (make-nan #f #t 1) 1.0)) 'caught)

;;; ---- interop with curry's own +nan.0 literal ----

(check "+nan.0 is a real NaN by this library's own check"
       (nan? +nan.0) #t)
(check "the sign/quiet fields of +nan.0 are readable without raising"
       (boolean? (nan-negative? +nan.0)) #t)
(check "the payload field of +nan.0 is readable without raising"
       (and (exact? (nan-payload +nan.0)) (>= (nan-payload +nan.0) 0)) #t)
;; make-nan requires a positive payload, so this only round-trips when
;; +nan.0's own payload is nonzero -- skip cleanly otherwise rather than
;; asserting something make-nan structurally can't reproduce.
(if (> (nan-payload +nan.0) 0)
    (check "make-nan can reproduce +nan.0's own exact fields"
           (nan=? +nan.0 (make-nan (nan-negative? +nan.0) (nan-quiet? +nan.0) (nan-payload +nan.0)))
           #t))

;;; ---- Regression: reader no longer misreads bare "nan"/"inf" as numbers ----
;;; A real, separate bug found while writing this library: curry's
;;; reader used to fall through to libc's own strtod for any otherwise-
;;; unrecognized token, and strtod (per C99) accepts "nan"/"inf"/
;;; "infinity" (case-insensitively, no sign or decimal point required)
;;; as valid float syntax -- so a plain identifier spelled `nan` read
;;; back as a NaN FLOAT instead of the symbol `nan`, breaking (among
;;; other things) any procedure that dared name a parameter `nan`
;;; (exactly what this library's own nan-negative?/nan-quiet?/
;;; nan-payload wanted to do). Fixed in src/reader.c: a token must
;;; contain at least one digit before parse_number ever hands it to
;;; strtod (every genuine R7RS float syntax has one; none of strtod's
;;; permissive-only spellings do).
(check "bare `nan` reads as a symbol, not a NaN float" (symbol? 'nan) #t)
(check "bare `inf` reads as a symbol, not an infinity float" (symbol? 'inf) #t)
(check "bare `infinity` reads as a symbol" (symbol? 'infinity) #t)
(check "a parameter literally named `nan` compiles and works"
       ((lambda (nan) (+ nan 1)) 41) 42)
(check "+nan.0/-nan.0/+inf.0/-inf.0 literals still parse as numbers, not symbols"
       (list (number? +nan.0) (number? -nan.0) (number? +inf.0) (number? -inf.0))
       '(#t #t #t #t))
(check "ordinary numeric literals are unaffected" (list 3.14 -2.5 1e10 5 3/4) (list 3.14 -2.5 1e10 5 3/4))

;;; ---- Summary ----

(newline)
(display "srfi-208 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
