;;; srfi_273_tests.scm — (srfi 273) Extensions to Data (Type-)Checking:
;;; the => (predicate ...) return-value-checking extension to
;;; lambda-checked/case-lambda-checked/define-checked, check-impl?,
;;; define-check, declare-checked, define-values-checked.

(import (srfi 273))

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

;;; ---- lambda-checked: backward compatibility (no => clause) ----
;;; Every one of these must behave EXACTLY as plain (srfi 253)'s own
;;; lambda-checked, since (srfi 273) is meant to be a drop-in
;;; replacement import.

(check "lambda-checked no =>: passes"
       ((lambda-checked ((x integer?)) (* x 2)) 5)
       10)
(check "lambda-checked no =>: raises on bad arg"
       (guard (e (#t 'caught)) ((lambda-checked ((x integer?)) (* x 2)) "nope"))
       'caught)
(check "lambda-checked no =>: unchecked formal passes through"
       ((lambda-checked (x) (string-length x)) "anything")
       8)
(check "lambda-checked no =>: rest-arg formals"
       ((lambda-checked (a . rest) (cons a rest)) 1 2 3)
       '(1 2 3))
(check "lambda-checked no =>: fully-variadic formals"
       ((lambda-checked args args) 1 2 3)
       '(1 2 3))

;;; ---- lambda-checked: with => (predicate ...) ----

(check "lambda-checked =>: single return check, passes"
       ((lambda-checked ((x integer?)) => (integer?) (* x 2)) 5)
       10)
(check "lambda-checked =>: single return check, fails"
       (guard (e (#t 'caught)) ((lambda-checked ((x integer?)) => (string?) (* x 2)) 5))
       'caught)
(check "lambda-checked =>: arg check still enforced alongside return check"
       (guard (e (#t 'caught)) ((lambda-checked ((x integer?)) => (integer?) (* x 2)) "nope"))
       'caught)
(check "lambda-checked =>: multiple return values, all pass"
       (call-with-values
         (lambda () ((lambda-checked ((x integer?)) => (integer? string?) (values x "hi")) 5))
         list)
       '(5 "hi"))
(check "lambda-checked =>: multiple return values, second fails"
       (guard (e (#t 'caught))
         (call-with-values
           (lambda () ((lambda-checked () => (integer? string?) (values 1 2)) ))
           list))
       'caught)
(check "lambda-checked =>: wrong return-value count raises"
       (guard (e (#t 'caught))
         (call-with-values (lambda () ((lambda-checked () => (integer? integer?) (values 1))))
                            list))
       'caught)
(check "lambda-checked =>: rest-arg formals with return check"
       ((lambda-checked (a . rest) => (integer?) (+ a (length rest))) 10 1 2 3)
       13)
(check "lambda-checked =>: unchecked arg formal still works alongside =>"
       ((lambda-checked (x) => (integer?) (* x 2)) 5)
       10)

;;; ---- check-impl? ----

(check "check-impl? as an arg check: no check performed"
       ((lambda-checked ((x (check-impl? whatever))) (* x 10)) 3)
       30)
(check "check-impl? as a return check: no check performed"
       ((lambda-checked () => ((check-impl? whatever)) "anything"))
       "anything")
(check "check-impl? mixed with a real return check"
       (call-with-values
         (lambda () ((lambda-checked () => (integer? (check-impl? x)) (values 5 'anything))))
         list)
       '(5 anything))

;; Regression for a bug found by independent review: %clc273-try (the
;; copied SRFI-253 dispatcher case-lambda-checked reuses) predates
;; check-impl? entirely, so a (fname (check-impl? _)) formal in a
;; case-lambda-checked clause's OWN argument-check position was being
;; treated as a literal predicate expression and called as one --
;; unconditionally raising instead of skipping the check as intended.
;; %lc273-args (lambda-checked/define-checked's own formal-peeling
;; macro) already special-cased this correctly; %clc273-try was
;; missing the identical pattern until this was found.
(check "check-impl? in case-lambda-checked's own argument-check position (regression)"
       ((case-lambda-checked (((x (check-impl? integer?))) (list 'got x))) "not an integer, but unchecked")
       '(got "not an integer, but unchecked"))

;; Regression for a second bug found by the same review: check-impl?'s
;; own error message was built from several adjacent string literals
;; passed positionally to `error` rather than one concatenated string
;; -- only the first fragment became the reported message, the rest
;; silently became separate irritants instead of message text.
(check "check-impl?'s own error message is the full sentence, not just the first fragment"
       (guard (e (#t (error-object-message e))) (check-impl? 'x))
       "check-impl?: this is auxiliary syntax, recognized only inside lambda-checked/case-lambda-checked/define-checked/declare-checked's check positions -- it cannot be called directly, and using it inside values-checked is an error per the SRFI text")

;;; ---- define-checked (extended) ----

(define-checked (sq x) => (integer?) (* x x))
(check "define-checked with return check: passes" (sq 4) 16)

(define-checked (bad-sq x) => (string?) (* x x))
(check "define-checked with return check: fails"
       (guard (e (#t 'caught)) (bad-sq 4))
       'caught)

(define-checked (plain-sq x) (* x x))
(check "define-checked no => (backward compat)" (plain-sq 5) 25)

(define-checked forty-two integer? 42)
(check "define-checked value form (unchanged from 253)" forty-two 42)
(check "define-checked value form raises on bad value"
       (guard (e (#t 'caught)) (define-checked bogus string? 42))
       'caught)

;;; ---- case-lambda-checked (extended) ----

(define cl
  (case-lambda-checked
    (() 'zero)
    ((x) => (integer?) (* x 1))
    ((x y) (+ x y))
    ((x y . rest) => (integer?) (apply + x y rest))))

(check "case-lambda-checked: zero-arg clause, no =>" (cl) 'zero)
(check "case-lambda-checked: one-arg clause with => passes" (cl 5) 5)
(check "case-lambda-checked: two-arg clause, no =>" (cl 3 4) 7)
(check "case-lambda-checked: rest-arg clause with => passes" (cl 1 2 3 4) 10)

(define cl-fail (case-lambda-checked ((x) => (string?) x)))
(check "case-lambda-checked: => return check fails"
       (guard (e (#t 'caught)) (cl-fail 5))
       'caught)

;; An 8-clause case-lambda-checked with mixed => and plain clauses must
;; still compile fast -- this is a real regression test for the
;; exponential-macro-expansion-blowup bug SRFI-253's own dispatcher was
;; fixed against (see this file's own header comment and
;; s273/extensions.scm's copy of that fix). A slow/hanging compile here
;; would mean the copy lost the fix; there's no portable way to assert
;; "this finished in under N seconds" against a hard timeout from
;; inside a test script, so this just needs to reach the check below at
;; all within ctest's own default per-test timeout to prove the point.
(define big-cl
  (case-lambda-checked
    ((a b c d) => (integer?) (+ a b c d))
    ((a b c) (+ a b c))
    ((a b) (+ a b))
    ((a) a)
    (() 0)
    ((a b c d e) => (integer?) (+ a b c d e))
    ((a b c d e f) (+ a b c d e f))
    ((a b c d e f g) => (integer?) (+ a b c d e f g))))
(check "case-lambda-checked: 8-clause dispatcher, 4-arg clause" (big-cl 1 2 3 4) 10)
(check "case-lambda-checked: 8-clause dispatcher, 7-arg clause" (big-cl 1 2 3 4 5 6 7) 28)
(check "case-lambda-checked: 8-clause dispatcher, 0-arg clause" (big-cl) 0)

;;; ---- define-check ----

(define-check my-integer? integer?)
(check "define-check: aliased predicate accepts" (my-integer? 5) #t)
(check "define-check: aliased predicate rejects" (my-integer? "x") #f)
(check "define-check: works as a lambda-checked arg check"
       ((lambda-checked ((x my-integer?)) (* x 2)) 5)
       10)

;;; ---- declare-checked ----
;;; A genuine no-op (matches the SRFI's own reference implementation --
;;; see s273/extensions.scm's header comment) -- these just need to
;;; parse and evaluate harmlessly, for every shape the SRFI specifies.

(check "declare-checked: value form is a harmless no-op"
       (begin (declare-checked some-name integer?) 'ok)
       'ok)
(check "declare-checked: procedure pre-declaration with => is a harmless no-op"
       (begin (declare-checked (some-proc x y) => (integer?)) 'ok)
       'ok)
(check "declare-checked: procedure pre-declaration without => is a harmless no-op"
       (begin (declare-checked (another-proc x)) 'ok)
       'ok)

;;; ---- define-values-checked ----

(define-values-checked (dv-a dv-b) (integer? string?) (values 1 "one"))
(check "define-values-checked: binds correctly on success" (list dv-a dv-b) '(1 "one"))

(check "define-values-checked: raises when a value fails its check"
       (guard (e (#t 'caught))
         (define-values-checked (dv-c dv-d) (string? string?) (values 1 "one")))
       'caught)

(check "define-values-checked: raises on wrong value count"
       (guard (e (#t 'caught))
         (define-values-checked (dv-e dv-f dv-g) (integer? integer? integer?) (values 1 2)))
       'caught)

(define-values-checked (dv-h) ((check-impl? whatever)) (values 'anything))
(check "define-values-checked: check-impl? skips checking that value" dv-h 'anything)

;;; ---- Summary ----

(newline)
(display "srfi-273 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
