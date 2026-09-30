;;; Regression test for a core bug: invoking a call/cc continuation
;;; with more than one argument silently dropped everything past the
;;; first value instead of delivering them all, as R7RS requires (a
;;; continuation must forward whatever values it's invoked with, the
;;; same as `values` itself). The bug was in two separate call sites --
;;; eval.c's tree-walker `apply`/`eval` invoke path and runtime.c's
;;; VM/compiled-code `apply` path -- both hard-coded "take only the
;;; first argument" before longjmp. Fixed by boxing 2+ arguments into
;;; the same T_VALUES object `values`/`call-with-values` already use.
;;; Found while implementing SRFI-224's fxmapping-accumulate.

(import (scheme base) (scheme write))

(define pass-count 0)
(define fail-count 0)

(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display name)
             (display " expected=") (write expected)
             (display " actual=") (write actual) (newline))))

;; The exact repro from the bug report.
(check "call/cc multi-value delivers all values"
  '(1 2 3)
  (call-with-values (lambda () (call/cc (lambda (k) (k 1 2 3)))) list))
(check "call-with-current-continuation spelling, same bug"
  '(1 2 3)
  (call-with-values (lambda () (call-with-current-continuation (lambda (k) (k 1 2 3)))) list))

;; Boundary cases: 0, 1, and many values.
(check "call/cc zero values" '()
  (call-with-values (lambda () (call/cc (lambda (k) (k)))) list))
(check "call/cc one value" '(42)
  (call-with-values (lambda () (call/cc (lambda (k) (k 42)))) list))
(check "call/cc one value used in arithmetic context" 6
  (+ 1 (call/cc (lambda (k) (k 5)))))
(check "call/cc many values"
  '(1 2 3 4 5 6 7 8 9 10)
  (call-with-values (lambda () (call/cc (lambda (k) (apply k (list 1 2 3 4 5 6 7 8 9 10))))) list))

;; Continuation not invoked at all -- thunk's own return value passes
;; through untouched (single value).
(check "call/cc thunk falls through without invoking k"
  '(99)
  (call-with-values (lambda () (call/cc (lambda (k) 99))) list))

;; A realistic pattern: escaping from deep within nested calls with an
;; extra piece of information alongside the primary result.
(define (find-first pred lst)
  (call/cc
    (lambda (return)
      (for-each (lambda (x) (when (pred x) (return x 'found))) lst)
      (values #f 'not-found))))
(call-with-values (lambda () (find-first even? '(1 3 5 4 7)))
  (lambda (v s) (check "find-first hit" (list 4 'found) (list v s))))
(call-with-values (lambda () (find-first even? '(1 3 5 7)))
  (lambda (v s) (check "find-first miss" (list #f 'not-found) (list v s))))

(display "call/cc multi-value tests: ") (display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
