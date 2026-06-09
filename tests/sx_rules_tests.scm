;;; Tests for user-defined CAS rewrite rules (Phase 4a)

(define passed 0)
(define failed 0)

(define-syntax assert-equal
  (syntax-rules ()
    [(assert-equal label a b)
     (let ([va a] [vb b])
       (if (equal? va vb)
           (begin (set! passed (+ passed 1)))
           (begin (set! failed (+ failed 1))
                  (display "FAIL: ") (display label) (newline)
                  (display "  expected: ") (display vb) (newline)
                  (display "  got:      ") (display va) (newline))))]))

;;; Helpers
(define x (sym-var 'x))
(define y (sym-var 'y))
(define n (sym-var 'n))

;;; --- 1. Basic rule: constant folding override ---
;;; Multiply by zero → 0 (this is already a built-in, but user rules fire first)
(define-rule (* 0 ?x) → 0)
(assert-equal "rule: (* 0 x) → 0"  (simplify (* 0 x))  0)
(assert-equal "rule: (* 0 3) → 0"  (simplify (* 0 3))  0)

;;; --- 2. Rule with pattern variable in template ---
(define-rule (log (expt ?base ?exp)) → (* ?exp (log ?base)))
(assert-equal "rule: log(x^3) → 3*log(x)"
  (simplify (log (expt x 3)))
  (simplify (* 3 (log x))))

;;; --- 3. Rule with #:when guard (assumption-based) ---
(define x+ (sym-var 'x+ 'positive))
(define-rule (sqrt (expt ?v 2)) → ?v  #:when (positive? ?v))
(assert-equal "rule: sqrt(x+^2) → x+ (positive guard)"
  (simplify (sqrt (expt x+ 2)))
  x+)
;;; Must NOT fire for a generic variable (no positive assumption)
(let ([result (simplify (sqrt (expt x 2)))])
  (assert-equal "rule: sqrt(x^2) unevaluated without positive assumption"
    (sym-expr? result) #t))

;;; --- 4. Multi-variable rule ---
(define-rule (+ (* ?a ?x) (* ?b ?x)) → (* (+ ?a ?b) ?x))
(assert-equal "rule: a*x + b*x → (a+b)*x"
  (simplify (+ (* 3 x) (* 5 x)))
  (simplify (* 8 x)))

;;; --- 5. define-ruleset ---
(define-ruleset trig-ids
  [(+ (expt (sin ?u) 2) (expt (cos ?u) 2)) → 1]
  [(sin (* 2 ?u)) → (* 2 (sin ?u) (cos ?u))])

(assert-equal "ruleset: sin²+cos²=1"
  (simplify (+ (expt (sin x) 2) (expt (cos x) 2)))
  1)
(assert-equal "ruleset: sin(2x) = 2 sin(x) cos(x)"
  (simplify (sin (* 2 x)))
  (simplify (* 2 (sin x) (cos x))))

;;; --- 6. User rule fires BEFORE built-in for same operator ---
;;; Rule for (+ v 0) sets a flag and returns v (non-circular, same result as built-in)
(define rule-fired? #f)
(define-rule (+ ?v 0) → (begin (set! rule-fired? #t) ?v))
(set! rule-fired? #f)
(simplify (+ x 0))
(assert-equal "user rule fires before built-in dispatch" rule-fired? #t)

;;; Clean up the degenerate rule
(clear-rules!)
(assert-equal "clear-rules! removes all rules"
  (null? (list-rules)) #t)

;;; --- 7. list-rules introspection ---
(define-rule (+ ?a ?b) → (+ ?b ?a))  ; commutativity demo
(let ([rules (list-rules)])
  (assert-equal "list-rules returns non-empty list after define-rule"
    (null? rules) #f))
(let ([rules (list-rules '+)])
  (assert-equal "list-rules filtered by operator"
    (null? rules) #f))
(let ([rules (list-rules '*)])
  (assert-equal "list-rules for * is empty after clear-rules!"
    (null? rules) #t))

;;; --- 8. Ruleset-scoped clear ---
(clear-rules!)
(define-ruleset my-set [(* ?x 1) → ?x])
(define-rule (+ ?x 0) → ?x)
(clear-rules! 'my-set)
;;; my-set rule gone, global rule remains
(assert-equal "clear-rules! by name leaves other rules"
  (null? (list-rules '+)) #f)
(assert-equal "clear-rules! by name removes named ruleset"
  (null? (list-rules '*)) #t)

;;; --- Report ---
(display "sx_rules tests: ")
(display passed) (display " passed, ")
(display failed) (display " failed")
(newline)
(if (> failed 0)
    (error "sx_rules test failures" failed)
    (display "OK\n"))
