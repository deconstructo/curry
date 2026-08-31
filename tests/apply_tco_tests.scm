;;; apply_tco_tests.scm — regression coverage for issue #102: `apply` was
;;; never tail-called, so a self-recursive procedure built on `apply` (most
;;; visibly (scheme case-lambda)'s own dispatch) stack-overflowed at a few
;;; hundred iterations instead of looping. Fixed with a new OP_TAIL_APPLY
;;; opcode, mirroring OP_TAIL_CALL's existing frame-reuse machinery.

(import (scheme case-lambda))

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

;; A direct, non-case-lambda self-recursive loop through (apply f args) --
;; the exact new opcode's own most basic exercise.
(define (count-via-apply n acc)
  (if (= n 0) acc (apply count-via-apply (list (- n 1) (+ acc 1)))))
(check "apply TCO: direct self-recursive apply loop, 1,000,000 iterations"
       (count-via-apply 1000000 0)
       1000000)

;; apply with fixed leading args plus a trailing list, in tail position.
(define (sum-via-apply n acc)
  (if (= n 0) acc (apply sum-via-apply (- n 1) (list (+ acc n)))))
(check "apply TCO: fixed args + trailing list, in tail position"
       (sum-via-apply 100000 0)
       (/ (* 100000 100001) 2))

;; The exact issue #102 repro: a self-recursive case-lambda accumulator,
;; whose dispatch mechanism is built entirely on apply.
(define count
  (case-lambda
    ((n) (count n 0))
    ((n acc) (if (= n 0) acc (count (- n 1) (+ acc 1))))))
(check "apply TCO: case-lambda's own apply-based dispatch, 1,000,000 iterations"
       (count 1000000)
       1000000)

;; Non-tail apply (result used by the caller, not just returned) must
;; still work correctly -- this opcode is unaffected, but confirms the
;; new OP_TAIL_APPLY / OP_APPLY split didn't break the existing case.
(check "apply TCO: non-tail apply still works, fixed args only"
       (apply + (list 1 2 3))
       6)
(check "apply TCO: non-tail apply still works, mixed fixed + list args"
       (apply + 1 2 (list 3 4))
       10)
(check "apply TCO: non-tail apply's result composes with surrounding code"
       (+ 10 (apply * (list 2 3 4)))
       34)
(check "apply TCO: non-tail apply inside a higher-order call"
       (map (lambda (n) (apply + (list n n))) (list 1 2 3))
       '(2 4 6))

;; apply in tail position but targeting a non-BcClosure (a primitive) --
;; exercises OP_TAIL_APPLY's non-bcclosure branch specifically.
(define (call-plus-in-tail-position . args)
  (apply + args))
(check "apply TCO: tail-position apply targeting a primitive"
       (call-plus-in-tail-position 1 2 3 4)
       10)

;; Regression: two real, independent-review-found SIGSEGVs in the first
;; version of OP_TAIL_APPLY, both now-clean catchable errors instead.
;;
;; 1. total_args could go NEGATIVE: "(apply f)" (a one-argument apply
;;    call) computed n_fixed = total-2 = -1 with no validation, reaching
;;    memmove(..., (size_t)(-1) * sizeof(val_t)) before this fix.
(define (target-no-args) 'ok)
(define (bad-apply-in-tail-position) (apply target-no-args))
(check "apply TCO: (apply f) with no trailing list in tail position raises cleanly, not a crash"
       (guard (e (#t 'raised)) (bad-apply-in-tail-position))
       'raised)

;; 2. Nothing bounded total_args against the actual value stack size:
;;    OP_APPLY's own path always goes through vm_push (which DOES check
;;    against VM_STACK_MAX), but the direct frame-reuse memmove bypassed
;;    that check entirely -- a long enough applied list overran
;;    frame->slots, the frame array, and the handler stack.
(define (sink . xs) (length xs))
(define (unbounded-tail-apply n) (apply sink (make-list n 1)))
(check "apply TCO: a very long applied list in tail position raises a clean stack-overflow, not a crash"
       (guard (e (#t 'raised)) (unbounded-tail-apply 200000))
       'raised)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
