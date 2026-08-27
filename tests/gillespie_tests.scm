;;; gillespie_tests.scm — tests for (curry gillespie)

(import (curry gillespie))

;; Deterministic from here on -- the stochastic-run tests below check
;; statistical properties (mean within a tolerance band, not exact
;; values), but a fixed seed still keeps the whole suite reproducible
;; run to run instead of occasionally flaking on an unlucky draw.
(random-source-pseudo-randomize! (make-random-source) 20260827 1)

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define-syntax check-approx
  (syntax-rules ()
    ((_ label expr expected tol)
     (let ((got expr))
       (if (< (abs (- got expected)) tol)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected ~") (display expected)
             (display " ± ") (display tol) (newline)
             (display "  got: ") (display got) (newline)))))))

(define-syntax check-true
  (syntax-rules ()
    ((_ label expr)
     (if expr
         (set! pass (+ pass 1))
         (begin
           (set! fail (+ fail 1))
           (display "FAIL: ") (display label) (newline))))))

;; every isn't a core builtin outside (scheme base)'s own list procedures
;; (same gap noted in random_tests.scm's own sibling file) -- small local
;; definition, no shared private module for one call site.
(define (%every pred lst)
  (or (null? lst) (and (pred (car lst)) (%every pred (cdr lst)))))

;;; ── species tables ───────────────────────────────────────────────────────

(let ((s (make-species (list (cons 'A 10) (cons 'B 0)))))
  (check "species-count reads initial value" (species-count s 'A) 10)
  (check "species-count defaults to 0 for unset key" (species-count s 'C) 0)
  (set-species-count! s 'A 5)
  (check "set-species-count! overwrites" (species-count s 'A) 5)
  (adjust-species! s 'A 3)
  (check "adjust-species! adds delta" (species-count s 'A) 8)
  (adjust-species! s 'A -20)
  (check "adjust-species! allows negative delta arithmetically" (species-count s 'A) -12))

;;; ── environment ──────────────────────────────────────────────────────────

(let ((env (make-environment 310.0 7.0 1000)))
  (check "environment-temperature" (environment-temperature env) 310.0)
  (check "environment-ph" (environment-ph env) 7.0)
  (check "environment-nutrients" (environment-nutrients env) 1000)
  (set-environment-temperature! env 250.0)
  (check "set-environment-temperature!" (environment-temperature env) 250.0)
  (set-environment-nutrients! env 0)
  (check "set-environment-nutrients!" (environment-nutrients env) 0))

;;; ── mass-action ──────────────────────────────────────────────────────────

(let* ((species (make-species (list (cons 'A 5))))
       (env (make-environment 310.0 7.0 0))
       (rate (mass-action 2.0 (list (cons 'A 1)))))
  (check-approx "mass-action stoich-1 propensity = k*n" (rate species env) 10.0 1e-9))

(let* ((species (make-species (list (cons 'A 5))))
       (env (make-environment 310.0 7.0 0))
       (rate (mass-action 1.0 (list (cons 'A 2)))))
  ;; C(5,2) = 10
  (check-approx "mass-action stoich-2 uses combinatorial C(n,2)" (rate species env) 10.0 1e-9))

(let* ((species (make-species (list (cons 'A 0))))
       (env (make-environment 310.0 7.0 0))
       (rate (mass-action 5.0 (list (cons 'A 1)))))
  (check-approx "mass-action propensity is 0 when reactant depleted" (rate species env) 0.0 1e-9))

(let* ((species (make-species (list (cons 'A 1))))
       (env (make-environment 310.0 7.0 0))
       (rate (mass-action 5.0 (list (cons 'A 2)))))
  ;; C(1,2) = 0 -- can't choose 2 from 1 available
  (check-approx "mass-action propensity is 0 when n < stoichiometry" (rate species env) 0.0 1e-9))

(let* ((species (make-species '()))
       (env (make-environment 310.0 7.0 0))
       (rate (mass-action 3.0 '())))
  (check-approx "mass-action with no reactants (zero-order) = k" (rate species env) 3.0 1e-9))

;;; ── zero-denominator guards (arrhenius, hill) ────────────────────────────
;;; Found by code review: michaelis-menten already guarded km=0/S=0 (see
;;; below), but arrhenius and hill had no analogous guard for their own
;;; degenerate zero-denominator inputs (temperature=0, width=0).

(let* ((species (make-species '()))
       (env (make-environment 0 7.0 0)))
  (check "arrhenius at temperature=0 is 0, not a division error"
    ((arrhenius 1.0 100.0) species env) 0))

(let* ((species (make-species '()))
       (curve (hill environment-ph 7.0 0)))
  (check "hill at width=0 is 1 exactly at the optimum"
    (curve species (make-environment 310 7.0 0)) 1)
  (check "hill at width=0 is 0 away from the optimum"
    (curve species (make-environment 310 5.0 0)) 0))

;;; ── arrhenius ────────────────────────────────────────────────────────────

(let* ((species (make-species '()))
       (hot (make-environment 400.0 7.0 0))
       (cold (make-environment 200.0 7.0 0))
       (rate (arrhenius 1.0 5000.0)))
  (check-true "arrhenius: hotter environment gives a larger rate"
    (> (rate species hot) (rate species cold))))

;;; ── michaelis-menten ─────────────────────────────────────────────────────

(let* ((env (make-environment 310.0 7.0 0))
       (rate (michaelis-menten 10.0 5.0 'S)))
  (check-approx "michaelis-menten at S=km is half of vmax"
    (rate (make-species (list (cons 'S 5))) env) 5.0 1e-9)
  (check-approx "michaelis-menten approaches vmax for S >> km"
    (rate (make-species (list (cons 'S 100000))) env) 10.0 1e-3)
  (check-approx "michaelis-menten is 0 with no substrate"
    (rate (make-species (list (cons 'S 0))) env) 0.0 1e-9))

;; km=0 and S=0 together would be 0/0 unguarded -- silent NaN that then
;; poisons gillespie-step!'s (> a0 0) quiescence check forever, permanently
;; freezing an otherwise-live cell. Found by code review.
(let* ((species (make-species (list (cons 'S 0))))
       (env (make-environment 310.0 7.0 0))
       (rate (michaelis-menten 10.0 0 'S)))
  (check "michaelis-menten with km=0 and S=0 is 0, not NaN" (rate species env) 0))

;; A negative species count (reachable only via direct adjust-species!
;; misuse, since correctly-computed propensities never let a reaction
;; drive a count below 0) must not make mass-action return a negative
;; propensity -- that would silently corrupt %pick-weighted's cumulative-
;; sum bucket test. Found by code review.
(let* ((species (make-species (list (cons 'A 0)))))
  (adjust-species! species 'A -5)
  (check-approx "mass-action clamps a corrupted negative count to 0 propensity"
    ((mass-action 1.0 (list (cons 'A 1))) species (make-environment 310.0 7.0 0))
    0.0 1e-9))

;;; ── hill (bell-curve environment sensitivity) ────────────────────────────

(let* ((species (make-species '()))
       (curve (hill environment-ph 7.0 1.0)))
  (check-approx "hill peaks at 1.0 at the optimum"
    (curve species (make-environment 310 7.0 0)) 1.0 1e-9)
  (check-true "hill falls off away from the optimum"
    (< (curve species (make-environment 310 5.0 0)) 1.0))
  (check-approx "hill is symmetric around the optimum"
    (curve species (make-environment 310 5.0 0))
    (curve species (make-environment 310 9.0 0))
    1e-9))

;;; ── rate* composition ────────────────────────────────────────────────────

(let* ((species (make-species (list (cons 'S 5))))
       (env (make-environment 310.0 7.0 0))
       (a (lambda (s e) 2.0))
       (b (lambda (s e) 3.0))
       (composed (rate* a b)))
  (check-approx "rate* multiplies component propensities together"
    (composed species env) 6.0 1e-9))

;;; ── the Gillespie step/run mechanics ─────────────────────────────────────

;; A single reaction with a food pool of exactly 5 units, one consumed per
;; firing: must reach exactly 0 and go quiescent, never negative -- this
;; is the direct regression test for the %falling-factorial bug found
;; while writing this suite (a stale early-exit guard returned the
;; unmultiplied accumulator when the reactant was fully depleted, instead
;; of the correct 0, so the reaction kept firing on an empty pool and
;; drove the count to -970 in a real run).
(let* ((r (make-reaction "consume" (list (cons 'food 1)) '()
                          (mass-action 1.0 (list (cons 'food 1)))))
       (cell (make-cell (make-species (list (cons 'food 5))) (list r)
                         (make-environment 310 7 0) 0)))
  (define final-t (gillespie-run! cell 10000.0))
  (check "consuming reaction never drives count negative"
    (species-count (cell-species cell) 'food) 0)
  (check-true "quiescent run stops before t-max"
    (< final-t 10000.0)))

;; 2A -> B: an odd starting count must leave exactly 1 A unpaired.
(let* ((r (make-reaction "dimerize" (list (cons 'a 2)) (list (cons 'b 1))
                          (mass-action 1.0 (list (cons 'a 2)))))
       (cell (make-cell (make-species (list (cons 'a 5) (cons 'b 0))) (list r)
                         (make-environment 310 7 0) 0)))
  (gillespie-run! cell 10000.0)
  (check "stoich-2 reaction leaves the odd molecule out" (species-count (cell-species cell) 'a) 1)
  (check "stoich-2 reaction produces the right count of product" (species-count (cell-species cell) 'b) 2))

;; gillespie-step! on an all-zero-propensity cell returns #f and changes nothing.
(let* ((r (make-reaction "dead" (list (cons 'x 1)) '() (mass-action 1.0 (list (cons 'x 1)))))
       (cell (make-cell (make-species (list (cons 'x 0))) (list r) (make-environment 310 7 0) 0)))
  (check "gillespie-step! returns #f when every propensity is 0"
    (gillespie-step! cell) #f)
  (check "gillespie-step! leaves time unchanged when it returns #f"
    (cell-time cell) 0)
  (check "gillespie-step! leaves species unchanged when it returns #f"
    (species-count (cell-species cell) 'x) 0))

;; Birth-death process A: born at rate 5.0, degraded at rate 0.1 per
;; molecule. Analytical steady-state mean is birth/death = 50 -- run many
;; independent replicates and check the mean lands in a wide statistical
;; band around it (this is a real stochastic process, not a deterministic
;; check, so the tolerance has to be generous).
(define (%birth-death-final-count)
  (let* ((birth (make-reaction "birth" '() (list (cons 'A 1)) (mass-action 5.0 '())))
         (death (make-reaction "death" (list (cons 'A 1)) '() (mass-action 0.1 (list (cons 'A 1)))))
         (cell (make-cell (make-species (list (cons 'A 0))) (list birth death)
                           (make-environment 310 7 0) 0)))
    (gillespie-run! cell 200.0)
    (species-count (cell-species cell) 'A)))

(let loop ((i 0) (total 0))
  (if (= i 30)
      (check-approx "birth-death process settles near its analytical steady state (5.0/0.1 = 50)"
        (/ total 30.0) 50.0 15.0)
      (loop (+ i 1) (+ total (%birth-death-final-count)))))

;;; ── cell-trajectory ──────────────────────────────────────────────────────

(let* ((birth (make-reaction "birth" '() (list (cons 'A 1)) (mass-action 5.0 '())))
       (death (make-reaction "death" (list (cons 'A 1)) '() (mass-action 0.1 (list (cons 'A 1)))))
       (cell (make-cell (make-species (list (cons 'A 0))) (list birth death)
                         (make-environment 310 7 0) 0))
       (traj (cell-trajectory cell 10.0 1.0)))
  (check-true "cell-trajectory returns a non-empty list of samples" (pair? traj))
  (check-true "cell-trajectory samples are time-ordered"
    (let loop ((ts (map car traj)))
      (or (null? (cdr ts)) (and (<= (car ts) (cadr ts)) (loop (cdr ts))))))
  (check-true "each cell-trajectory sample carries a species snapshot"
    (%every (lambda (sample) (assq 'A (cdr sample))) traj)))

;; A cell that goes quiescent almost immediately must still let
;; cell-trajectory run to completion quickly over many remaining samples,
;; instead of recomputing every reaction's propensity from scratch at each
;; one for no new information. Found by code review; regression-tests the
;; short-circuit, not just correctness (a slow-but-correct implementation
;; would still pass a plain equality check, so this asserts on the actual
;; sample count and repeated-snapshot shape rather than timing directly).
(let* ((r (make-reaction "consume" (list (cons 'food 1)) '()
                          (mass-action 1.0 (list (cons 'food 1)))))
       (cell (make-cell (make-species (list (cons 'food 3))) (list r)
                         (make-environment 310.0 7.0 0) 0))
       (traj (cell-trajectory cell 1000.0 1.0)))
  (check "cell-trajectory samples the full requested range even once quiescent"
    (length traj) 1001)
  (check "cell-trajectory repeats the final snapshot once quiescent"
    (cdr (list-ref traj 500)) (cdr (list-ref traj 999))))

;; dt <= 0 must raise, not hang forever (next-sample never advancing past
;; t-max). Found by code review.
(let ((cell (make-cell (make-species '()) '() (make-environment 310 7 0) 0)))
  (check "cell-trajectory raises for dt = 0"
    (guard (e (#t 'raised)) (cell-trajectory cell 10.0 0)) 'raised)
  (check "cell-trajectory raises for negative dt"
    (guard (e (#t 'raised)) (cell-trajectory cell 10.0 -1)) 'raised))

;; Sample count must be exact for a dt that isn't a binary fraction (e.g.
;; 0.1) -- found by code review: accumulating next-sample via repeated
;; floating-point addition drifts and can land one sample off the correct
;; count; sampling as i*dt from an integer i (which the fix switched to)
;; doesn't have this problem.
(let ((cell (make-cell (make-species '()) '() (make-environment 310 7 0) 0)))
  (check "cell-trajectory sample count is exact for a non-binary dt"
    (length (cell-trajectory cell 100.0 0.1)) 1001))

;;; ── summary ──────────────────────────────────────────────────────────────

(newline)
(display "Gillespie tests: ") (display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(when (> fail 0) (error "test failures" fail))
