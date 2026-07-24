;;; Tests for Phase 4b: define-algebra, with-assumptions, assume!/can-assume?

(define passed 0)
(define failed 0)

(define-syntax assert-equal
  (syntax-rules ()
    [(assert-equal label a b)
     (let ([va a] [vb b])
       (if (equal? va vb)
           (set! passed (+ passed 1))
           (begin (set! failed (+ failed 1))
                  (display "FAIL: ") (display label) (newline)
                  (display "  expected: ") (display vb) (newline)
                  (display "  got:      ") (display va) (newline))))]))

(define x  (sym-var 'x))
(define y  (sym-var 'y))
(define x+ (sym-var 'x+ 'positive))

;;; ============================================================
;;; 1. assume! / can-assume? / drop-assumption!
;;; ============================================================

(assert-equal "fresh var: not positive"   (can-assume? x 'positive) #f)
(assert-equal "fresh var: not real"       (can-assume? x 'real)     #f)

(assume! x 'positive)
(assert-equal "after assume! positive"    (can-assume? x 'positive) #t)

(assume! x 'integer)
(assert-equal "can stack assumptions"     (can-assume? x 'integer)  #t)
(assert-equal "positive still set"        (can-assume? x 'positive) #t)

(drop-assumption! x 'integer)
(assert-equal "after drop integer"        (can-assume? x 'integer)  #f)
(assert-equal "positive survives drop"    (can-assume? x 'positive) #t)

(drop-assumption! x 'positive)
(assert-equal "restored to clean state"   (can-assume? x 'positive) #f)

;;; ============================================================
;;; 2. with-assumptions — dynamic scoping
;;; ============================================================

;;; sqrt(x^2) = x only when x is positive
(assert-equal "sqrt(x^2) without assumption stays symbolic"
  (sym-expr? (simplify (sqrt (expt x 2)))) #t)

(with-assumptions ((x positive))
  (assert-equal "sqrt(x^2) = x inside with-assumptions"
    (simplify (sqrt (expt x 2))) x))

(assert-equal "assumption removed after with-assumptions"
  (can-assume? x 'positive) #f)

;;; log(x^n) = n*log(x) requires x positive
(with-assumptions ((x positive))
  (assert-equal "log(x^3) = 3*log(x) with positive"
    (simplify (log (expt x 3)))
    (simplify (* 3 (log x)))))

(assert-equal "log(x^3) stays symbolic without assumption"
  (sym-expr? (simplify (log (expt x 3)))) #t)

;;; with-assumptions restores on exception
(define restored? #f)
(guard (exn (#t #f))
  (with-assumptions ((x positive))
    (error "deliberate")))
(set! restored? (not (can-assume? x 'positive)))
(assert-equal "with-assumptions restores flags on exception" restored? #t)

;;; with-assumptions inside a lambda body (local/lexical scope, tail
;;; position) — regression coverage for compile_with_assumptions
;;; (native codegen, compiler.c), which desugars to a let/dynamic-wind
;;; nest rather than the tree-walker's eval-based S_WITH_ASSUMPTIONS.
(define (sqrt-abs v)
  (with-assumptions ((v positive))
    (simplify (sqrt (expt v 2)))))
(assert-equal "with-assumptions in tail position of a lambda body"
  (sqrt-abs x) x)
(assert-equal "assumption removed after returning from lambda body"
  (can-assume? x 'positive) #f)

;;; Multiple clauses in one with-assumptions, each independently
;;; restored — regression coverage for the multi-clause let/dynamic-wind
;;; nest generated per with-assumptions form.
(with-assumptions ((x positive) (y negative))
  (assert-equal "multi-clause: x positive" (can-assume? x 'positive) #t)
  (assert-equal "multi-clause: y negative" (can-assume? y 'negative) #t))
(assert-equal "multi-clause: x restored" (can-assume? x 'positive) #f)
(assert-equal "multi-clause: y restored" (can-assume? y 'negative) #f)

;;; Same SymVar repeated across clauses in one with-assumptions form.
;;; The tree-walker (eval.c) interleaves per-clause snapshot-then-set, so
;;; a repeated var's later-clause snapshot already includes the earlier
;;; clause's flags, leaving a residual flag set after exit. The compiled
;;; form (compile_with_assumptions) snapshots ALL clauses' original flags
;;; upfront, so a repeated var is restored to its true original state —
;;; a deliberate, documented improvement. Locked in here so it doesn't
;;; silently regress either way.
(with-assumptions ((x positive) (x integer))
  (assert-equal "duplicate clause: both flags active inside"
    (list (can-assume? x 'positive) (can-assume? x 'integer)) '(#t #t)))
(assert-equal "duplicate clause: fully restored after exit"
  (list (can-assume? x 'positive) (can-assume? x 'integer)) '(#f #f))

;;; ============================================================
;;; 3. define-algebra — user-defined operators
;;; ============================================================

;;; 3a. Commutative + associative wedge product with identity 1
(define-algebra 'wedge #:associative? #t #:identity 1)

;;; Associative flattening
(let ([result (simplify (wedge x (wedge y x+)))])
  (assert-equal "wedge is associative: nested → flat"
    (and (sym-expr? result)
         (= (sym-expr-nargs result) 3)) #t))

;;; Identity elimination
(assert-equal "wedge x 1 → x (identity)"
  (simplify (wedge x 1)) x)
(assert-equal "wedge 1 x → x (identity)"
  (simplify (wedge 1 x)) x)
(assert-equal "wedge 1 1 → 1 (all identity)"
  (simplify (wedge 1 1)) 1)

;;; 3b. Absorbing element
(define-algebra 'outer #:associative? #t #:identity 1 #:absorbing 0)

(assert-equal "outer x 0 → 0 (absorbing)"
  (simplify (outer x 0)) 0)
(assert-equal "outer 0 x → 0 (absorbing, first arg)"
  (simplify (outer 0 x)) 0)
(assert-equal "outer x 1 → x (identity)"
  (simplify (outer x 1)) x)

;;; 3c. Relations function for custom rewriting
(define (my-algebra-fn expr)
  ;; (meet x x) → x  (idempotent)
  (if (and (sym-expr? expr)
           (= (sym-expr-nargs expr) 2)
           (equal? (sym-expr-arg expr 0)
                   (sym-expr-arg expr 1)))
      (sym-expr-arg expr 0)
      (void)))  ; V_VOID = no match

(define-algebra 'meet #:commutative? #t #:associative? #t
                       #:relations my-algebra-fn)

(assert-equal "meet x x → x (idempotent via relations)"
  (simplify (meet x x)) x)
(assert-equal "meet x y stays as-is when different"
  (sym-expr? (simplify (meet x y))) #t)

;;; ============================================================
;;; 4. sym-var? / built-in assumption predicates still work
;;; ============================================================

(assert-equal "sym-var? #t for sym-var"   (sym-var? x)  #t)
(assert-equal "sym-var? #f for number"    (sym-var? 42) #f)

;; x+ was created with 'positive — should still be simplifiable
(assert-equal "sqrt(x+^2) = x+ (created positive)"
  (simplify (sqrt (expt x+ 2))) x+)

;;; ============================================================
;;; Report
;;; ============================================================
(display "sx_algebra tests: ")
(display passed) (display " passed, ")
(display failed) (display " failed")
(newline)
(if (> failed 0)
    (error "sx_algebra test failures" failed)
    (display "OK\n"))
