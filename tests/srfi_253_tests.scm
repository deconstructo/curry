;;; srfi_253_tests.scm — (srfi 253) Data (Type-)Checking: check-arg,
;;; values-checked, check-case, lambda-checked, case-lambda-checked,
;;; define-checked, define-record-type-checked. Also covers curry's own
;;; extended-type predicates (bignum?, multivector?, quaternion?, etc)
;;; working seamlessly as checkers, since that's the whole point of
;;; "catering to curry's additional types".

(import (srfi 253))

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

;;; ---- check-arg ----

(check "check-arg: passes and returns arg" (check-arg number? 5) 5)
(check "check-arg: raises on failure"
       (guard (e (#t 'caught)) (check-arg number? "x"))
       'caught)
(check "check-arg: optional caller doesn't change success behavior"
       (check-arg string? "hi" 'my-proc)
       "hi")

;;; ---- values-checked ----

(check "values-checked: two predicates, both pass"
       (call-with-values (lambda () (values-checked (integer? string?) 9 "hi")) list)
       '(9 "hi"))
(check "values-checked: coercion-friendly (predicate on the value, not the literal type)"
       (call-with-values (lambda () (values-checked (integer?) 9)) list)
       '(9))
(check "values-checked: raises on the first failing predicate"
       (guard (e (#t 'caught)) (values-checked (string?) 9))
       'caught)
(check "values-checked: each val evaluated exactly once"
       (let ((n 0))
         (call-with-values
           (lambda () (values-checked (integer?) (begin (set! n (+ n 1)) 5)))
           (lambda (v) (list v n))))
       '(5 1))

;;; ---- check-case ----

(check "check-case: first matching predicate wins"
       (check-case 5 (string? 's) (integer? 'i))
       'i)
(check "check-case: else clause for no match"
       (check-case "x" (integer? 'i) (else 'e))
       'e)
(check "check-case: no match, no else -- raises"
       (guard (e (#t 'caught)) (check-case 3.5 (string? 's) (symbol? 'sym)))
       'caught)
(check "check-case: value evaluated exactly once"
       (let ((n 0))
         (check-case (begin (set! n (+ n 1)) 5)
           (string? 'wrong)
           (integer? n)))
       1)

;;; ---- lambda-checked ----

(define add2 (lambda-checked ((x number?) y) (+ x y)))
(check "lambda-checked: checked + unchecked formal, passes" (add2 3 4) 7)
(check "lambda-checked: checked formal fails, raises"
       (guard (e (#t 'caught)) (add2 "x" 4))
       'caught)

(define rest-checked (lambda-checked ((x number?) . rest) (cons x rest)))
(check "lambda-checked: checked formal + rest arg" (rest-checked 1 2 3) '(1 2 3))
(check "lambda-checked: checked formal fails even with rest present"
       (guard (e (#t 'caught)) (rest-checked "x" 2 3))
       'caught)

(define all-rest (lambda-checked args args))
(check "lambda-checked: fully variadic (bare symbol formals)" (all-rest 1 2 3) '(1 2 3))

(define two-checked (lambda-checked ((x number?) (y string?)) (list x y)))
(check "lambda-checked: two checked formals, both pass" (two-checked 1 "a") '(1 "a"))
(check "lambda-checked: two checked formals, second fails"
       (guard (e (#t 'caught)) (two-checked 1 2))
       'caught)

;;; ---- case-lambda-checked ----

(define clc
  (case-lambda-checked
    (((a number?)) (list "one" a))
    (((a number?) (b string?)) (list "two" a b))
    ((a b c . rest) (list "many" a b c rest))))

(check "case-lambda-checked: 1-arg clause" (clc 5) '("one" 5))
(check "case-lambda-checked: 2-arg clause with two checked formals" (clc 5 "hi") '("two" 5 "hi"))
(check "case-lambda-checked: rest-arg clause" (clc 1 2 3 4 5) '("many" 1 2 3 (4 5)))
(check "case-lambda-checked: 1-arg clause predicate fails, no other clause matches -- raises"
       (guard (e (#t 'caught)) (clc "x"))
       'caught)
(check "case-lambda-checked: 2-arg clause predicate fails, falls through to no match -- raises"
       (guard (e (#t 'caught)) (clc 5 42))
       'caught)
(check "case-lambda-checked: zero-arg case falls through cleanly to error"
       (guard (e (#t 'caught)) (clc))
       'caught)

;;; ---- define-checked ----

(define-checked (mul (a number?) (b number?)) (* a b))
(check "define-checked: procedure form, passes" (mul 3 4) 12)
(check "define-checked: procedure form, checked arg fails"
       (guard (e (#t 'caught)) (mul "x" 4))
       'caught)

(define-checked answer number? 42)
(check "define-checked: variable form, value bound" answer 42)

;;; ---- define-record-type-checked ----

(define-record-type-checked <point>
  (make-point x y)
  point?
  (x number? point-x set-point-x!)
  (y number? point-y))

(define pt (make-point 3 4))
(check "define-record-type-checked: predicate" (point? pt) #t)
(check "define-record-type-checked: accessors" (list (point-x pt) (point-y pt)) '(3 4))
(check "define-record-type-checked: constructor rejects a bad arg"
       (guard (e (#t 'caught)) (make-point "x" 4))
       'caught)
(check "define-record-type-checked: constructor rejects a bad arg in the second position"
       (guard (e (#t 'caught)) (make-point 3 "y"))
       'caught)

(set-point-x! pt 99)
(check "define-record-type-checked: mutator, valid value" (point-x pt) 99)
(check "define-record-type-checked: mutator rejects a bad value"
       (guard (e (#t 'caught)) (set-point-x! pt "bad"))
       'caught)
(check "define-record-type-checked: rejected mutation didn't corrupt the field"
       (point-x pt) 99)
(check "define-record-type-checked: read-only field (y) has no set-point-y!"
       (guard (e (#t 'caught)) (eval '(set-point-y! pt 1) (interaction-environment)))
       'caught)

;;; ---- Curry's extended type predicates as checkers ----
;;; The whole point of "catering to curry's additional types": any of
;;; curry's own extended numeric-tower/object predicates work as-is,
;;; with no special support needed in this library. bignum?/multivector?
;;; were genuinely missing from curry's core before this work and were
;;; added alongside it (src/builtins.c) specifically so this SRFI could
;;; be used meaningfully across curry's whole type system.

(check "check-arg: quaternion? rejects a plain number"
       (guard (e (#t 'caught)) (check-arg quaternion? 5))
       'caught)
(check "check-arg: quaternion? accepts a real quaternion"
       (quaternion? (check-arg quaternion? (make-quaternion 1 2 3 4)))
       #t)
(check "check-arg: bignum? rejects a fixnum" (guard (e (#t 'caught)) (check-arg bignum? 5)) 'caught)
(check "check-arg: bignum? accepts a value past fixnum range"
       (bignum? (check-arg bignum? (expt 2 100)))
       #t)
(check "check-arg: multivector? rejects a plain number"
       (guard (e (#t 'caught)) (check-arg multivector? 5))
       'caught)
(check "check-arg: multivector? accepts a real multivector"
       (multivector? (check-arg multivector? (make-mv 3 0 0)))
       #t)
(check "lambda-checked with a curry-specific predicate"
       ((lambda-checked ((q quaternion?)) (quaternion-w q)) (make-quaternion 5 0 0 0))
       5.0)

;;; ---- Summary ----

(newline)
(display "srfi-253 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
