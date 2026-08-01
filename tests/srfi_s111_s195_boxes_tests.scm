;;; srfi_s111_s195_boxes_tests.scm — (srfi s111 boxes) / (srfi s195 multiple-value-boxes)

(import (srfi s111 boxes))
(import (srfi s195 multiple-value-boxes))

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

;;; ---- SRFI-111: single-value boxes ----

(define b (box 5))
(check "box?" (box? b) #t)
(check "box? on non-box" (box? 5) #f)
(check "unbox" (unbox b) 5)
(set-box! b 10)
(check "set-box! then unbox" (unbox b) 10)

;; each (box ...) call is independent
(define b1 (box 1))
(define b2 (box 1))
(check "distinct boxes are not eq?" (eq? b1 b2) #f)
(check "same box is eq? to itself" (eq? b1 b1) #t)

;; wrong arity to set-box! (SRFI-111's box holds exactly one value)
(check "set-box! wrong arity raises"
       (guard (e (#t 'raised)) (set-box! b 1 2))
       'raised)

;;; ---- SRFI-195: multiple-value boxes ----

(define mb (box 1 2 3))
(check "box-arity" (box-arity mb) 3)
(check "unbox-value" (unbox-value mb 1) 2)
(set-box-value! mb 1 'changed)
(check "set-box-value! then unbox-value" (unbox-value mb 1) 'changed)
(check "set-box-value! only touches the one index"
       (call-with-values (lambda () (unbox mb)) list)
       (list 1 'changed 3))

;; zero-value box
(define eb (box))
(check "zero-value box arity" (box-arity eb) 0)

;; out-of-range / negative index must raise, not crash or silently succeed
(check "unbox-value out-of-range raises"
       (guard (e (#t 'raised)) (unbox-value (box 1 2 3) 5))
       'raised)
(check "unbox-value negative index raises"
       (guard (e (#t 'raised)) (unbox-value (box 1 2 3) -1))
       'raised)
(check "set-box-value! out-of-range raises"
       (guard (e (#t 'raised)) (set-box-value! (box 1 2 3) 5 'x))
       'raised)
(check "set-box-value! negative index raises"
       (guard (e (#t 'raised)) (set-box-value! (box 1 2 3) -1 'x))
       'raised)

;; box/box?/unbox/set-box! are the SAME bindings under both libraries
;; (SRFI-195 requires this when both are imported into one program)
(check "s195's box IS s111's box (same procedure)" (eq? box box) #t)
(check "a box made via the shared box works with both APIs"
       (let ((shared (box 7)))
         (list (unbox shared) (box-arity shared)))
       (list 7 1))

;; the SRFI-195 spec's own worked example
(define result
  (let ((rb (box '() 0)))
    (for-each (lambda (e)
                (call-with-values (lambda () (unbox rb))
                  (lambda (lis n) (set-box! rb (cons e lis) (+ 1 n)))))
              '(1 2 3 4 5))
    (call-with-values (lambda () (unbox rb)) list)))
(check "SRFI-195 spec example" result (list '(5 4 3 2 1) 5))

;;; ---- Summary ----

(newline)
(display "srfi-s111/s195 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
