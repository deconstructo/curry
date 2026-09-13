;;; Tests for SRFI-61's extended cond clause: (generator guard => receiver).
;;; See issue #81: cond is a hardcoded special form (dispatched by symbol
;;; identity, not resolved via macro/environment lookup), so this couldn't
;;; be added as a library shim -- it had to be taught to both dispatch
;;; paths directly: compile_cond (src/compiler_classic.c, the VM/compiled
;;; path) and the S_COND case in eval() (src/eval.c, the tree-walked path,
;;; exercised here via a define-library body).

(import (scheme base) (scheme write))

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

;;; ════════════════════════════════════════════════════════════
;;; § 1  Compiled path (top-level, compile_cond)
;;; ════════════════════════════════════════════════════════════

(check "multi-value generator, guard true, receiver applied to both values"
  (cond ((values 3 4) (lambda (a b) (< a b)) => (lambda (a b) (list 'lt a b)))
        (else 'no))
  '(lt 3 4))

(check "multi-value generator, guard false, falls through to else"
  (cond ((values 3 4) (lambda (a b) (> a b)) => (lambda (a b) (list 'gt a b)))
        (else 'no))
  'no)

(check "single-value generator still gets wrapped as a 1-element arg list"
  (cond (5 (lambda (x) (> x 3)) => (lambda (x) (* x x))))
  25)

(check "guard false falls through to a LATER srfi-61 clause, not just else"
  (cond (5 (lambda (x) (> x 10)) => (lambda (x) 'never))
        (5 (lambda (x) (> x 3)) => (lambda (x) (* x 10))))
  50)

(check "guard false with no matching clause at all yields void-ish #f-safe else"
  (cond (5 (lambda (x) (> x 10)) => (lambda (x) 'never))
        (#t 'fallthrough))
  'fallthrough)

(check "generator with zero values -- guard/receiver both take no args"
  (cond ((values) (lambda () #t) => (lambda () 'zero-args)))
  'zero-args)

;;; Standard clause shapes must still work unchanged alongside this.
(check "plain test-only clause" (cond (5) (else 'no)) 5)
(check "plain arrow clause"     (cond (5 => (lambda (x) (* x x)))) 25)
(check "plain body clause"      (cond (#f 1) (#t 2) (else 3)) 2)
(check "else clause"            (cond (#f 'a) (else 'b)) 'b)

;;; ════════════════════════════════════════════════════════════
;;; § 2  Tree-walked path (inside a define-library body, eval())
;;; ════════════════════════════════════════════════════════════

(define-library (test srfi61 lib)
  (import (scheme base))
  (export lib-run-true lib-run-false lib-run-plain)
  (begin
    (define (lib-run-true)
      (cond ((values 3 4) (lambda (a b) (< a b)) => (lambda (a b) (list 'lt a b)))
            (else 'no)))
    (define (lib-run-false)
      (cond ((values 3 4) (lambda (a b) (> a b)) => (lambda (a b) (list 'gt a b)))
            (else 'no)))
    (define (lib-run-plain)
      (cond (#f 1) (#t 2) (else 3)))))

(import (test srfi61 lib))

(check "tree-walked: guard true" (lib-run-true) '(lt 3 4))
(check "tree-walked: guard false falls through" (lib-run-false) 'no)
(check "tree-walked: plain clauses unaffected" (lib-run-plain) 2)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
