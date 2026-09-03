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
;;; 3d. define-rule / define-ruleset — native compiler codegen
;;; ============================================================
;;; Regression coverage for compile_define_rule/compile_define_ruleset
;;; (compiler.c), which replaced the tree-eval punt that always built
;;; guard/template closures against GLOBAL_ENV regardless of where the
;;; form actually appeared in source.

;;; Plain top-level define-rule still fires.
(symbolic dr1)
(define-rule (dbl ?a) -> (* 2 ?a))
(assert-equal "define-rule: top-level rule fires"
  (simplify (sym-expr 'dbl dr1)) (simplify (* 2 dr1)))

;;; define-ruleset: multiple clauses, including one with a guard, both fire.
(symbolic rs1)
(define-ruleset arith-demo
  ((rsadd ?a ?b) -> (+ ?a ?b))
  ((rsdbl ?a) -> (* 2 ?a) #:when (> 1 0)))
(assert-equal "define-ruleset: first clause fires"
  (simplify (sym-expr 'rsadd 3 4)) 7)
(assert-equal "define-ruleset: second clause fires (with guard)"
  (simplify (sym-expr 'rsdbl rs1)) (simplify (* 2 rs1)))

;;; define-rule inside a lambda body, guard referencing an enclosing local
;;; variable. Before native codegen this raised "unbound variable: thresh"
;;; even though thresh is plainly in scope, because the tree-eval punt
;;; always built the guard closure against GLOBAL_ENV instead of the
;;; actual lexical environment of make-threshold-rule!'s call frame.
(symbolic dr2)
(define (make-threshold-rule! thresh)
  (define-rule (thresh-check ?a) -> ?a #:when (> thresh 3)))
(make-threshold-rule! 10)
(assert-equal "define-rule: guard closes over enclosing lexical scope"
  (simplify (sym-expr 'thresh-check dr2)) dr2)

;;; ============================================================
;;; 3e. define-algebra inside a lambda body — native compiler codegen
;;; ============================================================
;;; Regression coverage for compile_define_algebra (compiler.c). The
;;; auto-bound operator procedure must get a real LOCAL binding when the
;;; operator name is a compile-time literal (the (define-algebra 'sym ...)
;;; shape used everywhere above) — previously it always leaked into the
;;; global environment via tree-eval's hardcoded env_define(GLOBAL_ENV, ...),
;;; even when define-algebra appeared inside a function.

(define (use-local-algebra!)
  (define-algebra 'local-op9182 #:associative? #t)
  (local-op9182 1 2))
(assert-equal "define-algebra: usable inside its own defining scope"
  (sym-expr? (use-local-algebra!)) #t)
(assert-equal "define-algebra: operator does not leak to global scope"
  (guard (e (#t 'unbound)) (local-op9182 1 2) 'leaked)
  'unbound)

;;; A local define-algebra must not corrupt the enclosing lambda's local
;;; slot layout for unrelated names. Before this fix, compile_lambda's
;;; internal-define prescan misread the quoted operator form's `quote`
;;; token as define-algebra's bound variable name and reserved a bogus,
;;; permanently-uninitialised local slot for it — so a later bare
;;; reference to the special-form name `quote` inside the same body
;;; silently read back void instead of raising unbound-variable.
(define (g-quote-slot-test)
  (define-algebra 'another-op772 #:associative? #t)
  quote)
(assert-equal "define-algebra: does not reserve a bogus local slot"
  (guard (e (#t 'unbound)) (g-quote-slot-test) 'leaked)
  'unbound)

;;; Akkadian spelling of quote (kīma) must take the same compile-time-
;;; literal fast path as 'sym / (quote sym) — is_quoted_symbol compares via
;;; akk_translate, not a raw S_QUOTE check, specifically so this doesn't
;;; silently fall back to the (still-correct, but always-global) tree-eval
;;; path. Found by review.
(define (use-akkadian-quote-algebra!)
  (define-algebra (kīma local-op-akk) #:commutative? #t)
  (local-op-akk 1 2))
(assert-equal "define-algebra: (kīma sym) takes the literal fast path too"
  (sym-expr? (use-akkadian-quote-algebra!)) #t)
(assert-equal "define-algebra: (kīma sym) operator stays local, not global"
  (guard (e (#t 'unbound)) (local-op-akk 1 2) 'leaked)
  'unbound)

;;; define-ruleset used internally in a lambda body: both clauses must
;;; fire, including the guard-bearing one closing over the local `mult`.
(symbolic rs-in1)
(define (make-ruleset-internally! mult)
  (define-ruleset internal-demo
    ((rsi-add ?a ?b) -> (+ ?a ?b))
    ((rsi-scale ?a) -> (* mult ?a) #:when (> mult 0))))
(make-ruleset-internally! 5)
(assert-equal "define-ruleset: internal usage, first clause fires"
  (simplify (sym-expr 'rsi-add 3 4)) 7)
(assert-equal "define-ruleset: internal usage, guard closes over local"
  (simplify (sym-expr 'rsi-scale rs-in1)) (simplify (* 5 rs-in1)))

;;; define-ruleset: a malformed clause among valid ones is skipped, not a
;;; compile error, matching eval.c's per-clause `continue`.
(symbolic rs-mal1)
(define-ruleset mixed-validity
  ((rsm-ok ?a) -> ?a)
  (this-is-not-a-valid-clause)
  ((rsm-also-ok ?a) -> (* 3 ?a)))
(assert-equal "define-ruleset: malformed clause skipped, valid ones still fire (1)"
  (simplify (sym-expr 'rsm-ok rs-mal1)) rs-mal1)
(assert-equal "define-ruleset: malformed clause skipped, valid ones still fire (2)"
  (simplify (sym-expr 'rsm-also-ok rs-mal1)) (simplify (* 3 rs-mal1)))

;;; define-algebra with a RUNTIME-computed (non-literal-quote) operator
;;; name. There's no way to give a runtime-only name a real lexical
;;; binding in a slot-based compiled VM (compile_define_algebra's
;;; documented limit), so this stays on the pre-existing tree-eval path —
;;; which, unchanged by this migration, only ever evaluates against
;;; GLOBAL_ENV. The auto-bound operator always ends up a GLOBAL binding
;;; under whatever name op-expr evaluated to, so it must be invoked by
;;; that known literal name (there is no other way to reach a
;;; runtime-computed binding) — confirmed identical on pre-migration main.
(define global-name-thunk (lambda () 'dyn-op-global))
(define (use-dynamic-algebra-global!)
  (define-algebra (global-name-thunk) #:commutative? #t)
  (dyn-op-global 1 2))
(assert-equal "define-algebra: dynamic operator name referencing only globals works"
  (sym-expr? (use-dynamic-algebra-global!)) #t)

;;; ...but confirmed UNCHANGED (not a regression introduced by native
;;; codegen) when op-expr references an enclosing LOCAL variable: this
;;; already raised unbound-variable before this migration too, since
;;; tree-eval's env is always GLOBAL_ENV regardless of where the form
;;; appears. compile_define_algebra's fallback branch preserves that
;;; exact pre-existing limitation rather than silently changing it.
(define (use-dynamic-algebra-local! name-thunk)
  (define-algebra (name-thunk) #:commutative? #t)
  (dyn-op-local 1 2))
(assert-equal "define-algebra: dynamic operator name referencing a LOCAL still fails (unchanged limitation, not a new regression)"
  (guard (e (#t 'unbound-as-before))
    (use-dynamic-algebra-local! (lambda () 'dyn-op-local)))
  'unbound-as-before)

;;; ============================================================
;;; 4. sym-var? / built-in assumption predicates still work
;;; ============================================================

(assert-equal "sym-var? #t for sym-var"   (sym-var? x)  #t)
(assert-equal "sym-var? #f for number"    (sym-var? 42) #f)

;; x+ was created with 'positive — should still be simplifiable
(assert-equal "sqrt(x+^2) = x+ (created positive)"
  (simplify (sqrt (expt x+ 2))) x+)

;;; ============================================================
;;; 5. sx_simplify memoization (issue #137) -- O(depth) not O(depth^2),
;;;    and a rule/algebra registered AFTER a node was cached still fires
;;; ============================================================

;; Building a chain of N nested (sin ...) applications one at a time,
;; each call re-simplifying the whole existing tree so far, cost
;; O(depth^2) before #137 -- at this depth that would have taken many
;; seconds (or hit #134's own stack-depth guard first, depending on the
;; ulimit in effect); after #137 it's O(depth) and fast regardless.
;; Correctness matters more than timing here (a slow-but-correct CI
;; environment shouldn't fail this test), so this only asserts the
;; construction actually completes and produces a well-formed result,
;; not a wall-clock budget.
(define (wrap-sin n e) (if (= n 0) e (wrap-sin (- n 1) (sin e))))
(define deep-sin (wrap-sin 200000 x))
(assert-equal "sx_simplify memoization: deep (sin (sin ...)) chain still builds to the right op"
  (sym-expr-op deep-sin) 'sin)
(assert-equal "sx_simplify memoization: re-simplifying an already-simplified node is a no-op (same object back)"
  (eq? (simplify deep-sin) deep-sin) #t)

;; sx_invalidate_simplify_cache correctness: a node cached as "fully
;; simplified" BEFORE a new rule for its own operator existed must not
;; keep being served stale from that cache once the rule is registered.
(define cached-before-rule (sym-expr 'sx137-op x))
(assert-equal "sx_simplify memoization: uninterpreted op is left as-is before any rule exists"
  (sym->string cached-before-rule) "sx137-op(x)")
(define-rule (sx137-op ?a) -> (* 111 ?a))
(assert-equal "sx_simplify memoization: a rule registered AFTER caching still fires on the cached node"
  (sym->string (simplify cached-before-rule)) "111 * x")

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
