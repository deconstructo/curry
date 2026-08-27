;;; (curry gillespie) — stochastic simulation of cell biochemistry
;;;
;;; The Gillespie algorithm (SSA) models a set of chemical species
;;; undergoing reactions as a continuous-time Markov chain: instead of
;;; solving smooth ODEs for concentrations, it simulates individual random
;;; reaction events, which matters because real gene expression is noisy at
;;; low molecule counts -- a handful of mRNA copies, not a continuous
;;; concentration.
;;;
;;; Design doc: docs/thoughts/gillespie-cell-model.md (also covers an SBML
;;; import angle and composing this with a toy evolution model -- neither
;;; implemented here, this file is just the base simulation engine).
;;;
;;; A reaction's propensity (its probability-per-unit-time of firing next,
;;; given the current state) is an ordinary Scheme closure of
;;; (species-table environment) -> nonnegative-real. That's the whole
;;; composability story: temperature/pH/nutrient sensitivity isn't a
;;; separate feature bolted on, it's just a propensity function reading
;;; environment fields, and richer propensities are built by composing the
;;; small combinators below (mass-action, arrhenius, michaelis-menten,
;;; rate*) with ordinary procedure composition -- not a special mini-DSL.

(define-library (curry gillespie)
  (import (scheme base) (scheme inexact))
  (export
    ;; reactions
    make-reaction reaction? reaction-name reaction-reactants reaction-products
    reaction-propensity

    ;; environment
    make-environment environment?
    environment-temperature set-environment-temperature!
    environment-ph set-environment-ph!
    environment-nutrients set-environment-nutrients!

    ;; species tables (a species table is a plain hash-table: symbol -> count)
    make-species species-count set-species-count! adjust-species!

    ;; cells
    make-cell cell? cell-species cell-reactions cell-environment
    cell-time set-cell-time!

    ;; composable rate-law combinators
    mass-action arrhenius michaelis-menten hill rate*

    ;; simulation
    gillespie-step! gillespie-run! cell-trajectory)
  (begin

;;; ── reactions ────────────────────────────────────────────────────────────
;;; reactants/products are alists: ((species-symbol . stoichiometry) ...)

(define-record-type <reaction>
  (make-reaction name reactants products propensity)
  reaction?
  (name       reaction-name)
  (reactants  reaction-reactants)
  (products   reaction-products)
  (propensity reaction-propensity))

;;; ── environment ──────────────────────────────────────────────────────────
;;; Deliberately just three fields for the base model -- temperature (K),
;;; ph (dimensionless), nutrients (a plain number, meant to double as an
;;; extra species pool an uptake reaction depletes). A caller who wants
;;; more environment axes can always fold them into the species table
;;; itself instead (e.g. "oxygen" as an ordinary species).

(define-record-type <environment>
  (make-environment temperature ph nutrients)
  environment?
  (temperature environment-temperature set-environment-temperature!)
  (ph          environment-ph          set-environment-ph!)
  (nutrients   environment-nutrients   set-environment-nutrients!))

;;; ── species tables ───────────────────────────────────────────────────────

;; (make-species '((A . 10) (B . 0))) -> hash-table
(define (make-species initial-alist)
  (let ((tbl (make-hash-table)))
    (for-each (lambda (p) (hash-table-set! tbl (car p) (cdr p))) initial-alist)
    tbl))

(define (species-count species key)
  (hash-table-ref species key 0))

(define (set-species-count! species key n)
  (hash-table-set! species key n))

(define (adjust-species! species key delta)
  (set-species-count! species key (+ (species-count species key) delta)))

;;; ── cells ────────────────────────────────────────────────────────────────

(define-record-type <cell>
  (make-cell species reactions environment time)
  cell?
  (species     cell-species)
  (reactions   cell-reactions)
  (environment cell-environment)
  (time        cell-time set-cell-time!))

;;; ── composable rate-law combinators ─────────────────────────────────────
;;; Every combinator below returns a propensity procedure of
;;; (species environment) -> nonnegative-real. Combine them with rate* or
;;; ordinary lambda wrapping -- there's no separate combinator algebra to
;;; learn beyond "these are just functions."

;; Universal gas constant, J/(mol*K), for the Arrhenius combinator.
(define %R 8.314)

;; The falling factorial n*(n-1)*...*(n-k+1) -- the standard Gillespie
;; combinatorial correction for a reactant with stoichiometry k: k
;; molecules of the same species must be drawn from n available without
;; replacement, e.g. 2A -> B has propensity k*n*(n-1)/2, not k*n^2.
;; Note: no separate n<=0 guard -- when n < k, one of the k factors (n-i)
;; for i in [0, n] is exactly 0, so the product is correctly 0 without any
;; special case. An earlier version special-cased n<=0 by returning acc
;; unmultiplied (i.e. 1, the identity), which meant a fully depleted
;; reactant (count 0) still had a nonzero mass-action propensity -- found
;; via a real simulation run that consumed a finite resource down to
;; -970 instead of stopping at 0.
(define (%falling-factorial n k)
  (let loop ((i 0) (acc 1))
    (if (= i k)
        acc
        (loop (+ i 1) (* acc (- n i))))))

;; (mass-action k reactants) -- the standard elementary-reaction rate law:
;; propensity = k * product over each reactant of its falling-factorial
;; term, and stoichiometry 2+ divided by the corresponding factorial (so
;; 2A -> B is k*C(n,2) = k*n*(n-1)/2, not k*n*(n-1)).
(define (mass-action k reactants)
  (lambda (species environment)
    (* k (%mass-action-combinatorial species reactants))))

(define (%factorial n)
  (let loop ((i 2) (acc 1)) (if (> i n) acc (loop (+ i 1) (* acc i)))))

;; Clamps a negative count to 0 before computing the falling factorial --
;; species-count/adjust-species! are plain hash-table wrappers that don't
;; themselves forbid a negative count (found by code review: nothing
;; upstream of this function *should* ever produce one now that
;; %falling-factorial correctly zeroes out a depleted reactant, but a
;; negative count reaching here at all -- via direct adjust-species!
;; misuse, or a future reaction-application bug -- would otherwise make
;; %falling-factorial return a *negative* propensity, silently corrupting
;; %pick-weighted's cumulative-sum bucket test rather than raising or
;; clamping to a sane 0).
(define (%mass-action-combinatorial species reactants)
  (apply * (map (lambda (r)
                  (let ((n (max 0 (species-count species (car r))))
                        (k (cdr r)))
                    (/ (%falling-factorial n k) (%factorial k))))
                reactants)))

;; (arrhenius A Ea) -- temperature-dependent rate constant, k(T) = A *
;; exp(-Ea / (R*T)). Meant to be combined with mass-action via rate* to
;; make a mass-action reaction temperature-sensitive, e.g.
;;   (rate* (mass-action 1 reactants) (arrhenius 1e13 50000))
;; Guards temperature = 0 (absolute zero -- a degenerate but reachable
;; parameter, same class of caller mistake as km=0/S=0 in
;; michaelis-menten) by returning 0 rather than dividing by zero: as T -> 0+
;; with Ea > 0, exp(-Ea/(R*T)) -> 0 in the actual limit, so 0 is the
;; physically correct answer here, not just an arbitrary guard value.
;; Found by code review, which noted this combinator lacked the same
;; zero-denominator guard michaelis-menten already has.
(define (arrhenius A Ea)
  (lambda (species environment)
    (let ((T (environment-temperature environment)))
      (if (= T 0) 0 (* A (exp (- (/ Ea (* %R T)))))))))

;; (michaelis-menten vmax km substrate-key) -- saturating uptake/enzyme
;; kinetics: rate = vmax*S / (km+S). Approaches vmax as substrate S grows
;; large, approaches zero as it's depleted -- the standard toy model for
;; nutrient-limited growth. Guards km+S = 0 (only possible when both are
;; 0 -- km negative would be a nonsensical parameter, S is never negative
;; after the max-0 clamp) by returning 0 rather than dividing 0 by 0,
;; found by code review: an unguarded 0/0 there is a silent NaN whose
;; propensity then poisons gillespie-step!'s (> a0 0) quiescence check
;; forever, permanently freezing an otherwise-live cell with no error.
(define (michaelis-menten vmax km substrate-key)
  (lambda (species environment)
    (let ((s (max 0 (species-count species substrate-key))))
      (if (= (+ km s) 0) 0 (/ (* vmax s) (+ km s))))))

;; (hill optimal width) -- a bell-curve multiplier peaking at 1.0 when
;; (env-reader environment) = optimal, falling off over the given width.
;; The generic shape behind "this reaction's rate depends on pH" or any
;; other environment axis with an optimum rather than a monotonic scaling.
;; Guards width = 0 (a degenerate parameter -- the bell curve's limit as
;; width -> 0 is a delta function, 1.0 exactly at the optimum and 0
;; everywhere else) rather than dividing by zero. Found by code review
;; alongside the same gap in arrhenius.
(define (hill env-reader optimal width)
  (lambda (species environment)
    (let ((x (env-reader environment)))
      (if (= width 0)
          (if (= x optimal) 1 0)
          (exp (- (/ (* (- x optimal) (- x optimal)) (* 2 width width))))))))

;; (rate* proc ...) -- combine any number of propensity procedures by
;; multiplying their outputs. This is the actual composability mechanism:
;; a temperature-sensitive, pH-sensitive, nutrient-saturating reaction is
;; just (rate* (mass-action ...) (arrhenius ...) (hill ...) (michaelis-menten ...)).
(define (rate* . procs)
  (lambda (species environment)
    (apply * (map (lambda (p) (p species environment)) procs))))

;;; ── the Gillespie direct method ──────────────────────────────────────────

;; One step: draw an exponential waiting time from the sum of all
;; propensities, pick which reaction fires weighted by its own share,
;; apply it, advance time. Returns #f (and changes nothing) when every
;; propensity is zero -- e.g. a nutrient pool has run out and nothing can
;; react any more -- rather than looping or raising, so a caller can
;; treat "the cell went quiescent" as an ordinary, checkable outcome.
;; random-real can return exactly 0.0 (an all-zero 53-bit mantissa,
;; probability 2^-53 per draw but not zero) -- (log 0.0) is -inf.0, which
;; would make tau below +inf.0 and jump the cell's clock to infinity in a
;; single step, silently ending the run with no error. Found by code
;; review. Redrawing on the (astronomically rare) exact-zero case is
;; effectively free amortized and avoids ever computing (log 0.0) at all.
(define (%random-real-nonzero)
  (let ((u (random-real)))
    (if (= u 0.0) (%random-real-nonzero) u)))

(define (gillespie-step! cell)
  (let* ((reactions (cell-reactions cell))
         (props (map (lambda (r) ((reaction-propensity r)
                                   (cell-species cell) (cell-environment cell)))
                      reactions))
         (a0 (apply + props)))
    (and (> a0 0)
         (let ((tau (/ (- (log (%random-real-nonzero))) a0))
               (chosen (%pick-weighted reactions props a0)))
           (set-cell-time! cell (+ (cell-time cell) tau))
           (%apply-reaction! cell chosen)
           #t))))

(define (%pick-weighted reactions props a0)
  (let ((r (* (random-real) a0)))
    (let loop ((rs reactions) (ps props) (acc 0))
      (let ((acc* (+ acc (car ps))))
        (if (or (null? (cdr rs)) (< r acc*))
            (car rs)
            (loop (cdr rs) (cdr ps) acc*))))))

(define (%apply-reaction! cell reaction)
  (let ((species (cell-species cell)))
    (for-each (lambda (r) (adjust-species! species (car r) (- (cdr r))))
              (reaction-reactants reaction))
    (for-each (lambda (p) (adjust-species! species (car p) (cdr p)))
              (reaction-products reaction))))

;; (gillespie-run! cell t-max) -- step until cell-time reaches t-max or the
;; cell goes quiescent (every propensity zero). Mutates cell in place;
;; returns the final cell-time reached (which is < t-max exactly when the
;; run ended by quiescence rather than reaching t-max).
(define (gillespie-run! cell t-max)
  (let loop ()
    (if (and (< (cell-time cell) t-max) (gillespie-step! cell))
        (loop)
        (cell-time cell))))

;; (cell-trajectory cell t-max dt) -- run the simulation to t-max,
;; recording a snapshot of every species count every dt time units (not
;; every reaction event, which for a fast reaction network could be
;; thousands of points for a single dt worth of biological time). Returns
;; a list of (time . ((species . count) ...)) pairs, oldest first --
;; directly usable as the data series for a Qt canvas or any plotting
;; code. Mutates cell in place, same as gillespie-run!.
;;
;; dt must be positive -- found by code review: an unguarded dt <= 0 (a
;; swapped-argument call, or dt computed as t-max/n for some n that
;; rounds to 0) left next-sample fixed forever, hanging the process with
;; no error and no way to tell what went wrong.
;;
;; Sample times are i*dt for an integer step count i, not built by
;; repeatedly adding dt to itself -- found by code review: repeated
;; floating-point addition of a dt that isn't an exact binary fraction
;; (e.g. 0.1) accumulates rounding error over many steps, which can shift
;; the final sample across the t-max boundary and silently produce one
;; more or fewer samples than (+ 1 (/ t-max dt)) would predict.
;;
;; Once the cell goes quiescent (every reaction's propensity is 0 -- see
;; gillespie-step!) nothing will ever change again, so this stops calling
;; gillespie-step! for the remaining samples and reuses one cached
;; snapshot instead of rebuilding an identical hash-table->alist every
;; time -- found by code review: without the cache, a cell that goes
;; quiescent early in a long, fine-grained run (e.g. t-max=10000, dt=0.01,
;; quiescent by t=1) still reallocated an identical alist from scratch at
;; each of the ~1,000,000 remaining sample points for no new information.
(define (cell-trajectory cell t-max dt)
  (unless (> dt 0)
    (error "cell-trajectory: dt must be positive" dt))
  (let loop ((i 0) (quiescent-snapshot #f) (acc '()))
    (let ((next-sample (* i dt)))
      (cond
        ((> next-sample t-max) (reverse acc))
        (quiescent-snapshot
         (loop (+ i 1) quiescent-snapshot
               (cons (cons (cell-time cell) quiescent-snapshot) acc)))
        (else
         (let record-until ()
           (if (< (cell-time cell) next-sample)
               (if (gillespie-step! cell)
                   (record-until)
                   ;; Just went quiescent this iteration -- cache the
                   ;; snapshot from here on instead of rebuilding it.
                   (let ((s (%species-snapshot cell)))
                     (loop (+ i 1) s (cons (cons (cell-time cell) s) acc))))
               (loop (+ i 1) #f
                     (cons (cons (cell-time cell) (%species-snapshot cell)) acc)))))))))

(define (%species-snapshot cell)
  (hash-table->alist (cell-species cell)))

  )) ;; end begin, define-library
