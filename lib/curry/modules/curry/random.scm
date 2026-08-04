;;; (curry random) — continuous probability distributions
;;;
;;; Continuous distributions: uniform, normal, exponential, gamma, beta,
;;; cauchy, log-normal, chi-squared, student-t.
;;;
;;; Every distribution is a record with three interfaces:
;;;   (sample dist)               — draw one variate
;;;   (sample dist n)             — draw n variates as a list
;;;   (distribution-pdf dist x)   — density at x; symbolic-aware
;;;   (distribution-cdf dist x)   — cumulative probability; symbolic-aware
;;;   (distribution-mean dist)    — exact where tractable
;;;   (distribution-variance dist)
;;;   (distribution-std dist)
;;;
;;; Discrete distributions are omitted intentionally — use T_QUANTUM.
;;;
;;; Symbolic awareness: pass a (sym-var 'x) to pdf/cdf to get a CAS expression.

(define-library (curry random)
  (import (scheme base))
  (import (scheme inexact))
  (export
    iota
    uniform-distribution uniform-distribution? uniform-lo uniform-hi
    normal-distribution normal-distribution? normal-mean normal-std
    exponential-distribution exponential-distribution? exponential-rate
    gamma-distribution gamma-distribution? gamma-shape gamma-scale
    beta-distribution beta-distribution? beta-alpha beta-beta-param
    cauchy-distribution cauchy-distribution? cauchy-location cauchy-scale
    log-normal-distribution log-normal-distribution? log-normal-mu log-normal-sigma
    chi-squared-distribution chi-squared-distribution? chi-squared-df
    student-t-distribution student-t-distribution? student-t-df
    sample
    distribution-pdf distribution-cdf
    distribution-mean distribution-variance distribution-std
    shuffle! shuffle random-choice random-sample)
  (begin

;;; ── helpers ─────────────────────────────────────────────────────────────────

(define (iota count . args)
  (let ((start (if (null? args) 0 (car args)))
        (step  (if (or (null? args) (null? (cdr args))) 1 (cadr args))))
    (let loop ((i 0) (acc '()))
      (if (= i count) (reverse acc)
          (loop (+ i 1) (cons (+ start (* i step)) acc))))))

;;; ── random primitives ────────────────────────────────────────────────────────
;;; Thin wrappers around the C PRNG; these are the only places we touch random-real
;;; and random-integer, making future per-source support a one-place change.

(define (%u01)  (random-real))
(define (%uint n) (random-integer n))

;;; ── Box-Muller normal sampler ────────────────────────────────────────────────

(define (%standard-normal)
  (let* ((u1 (%u01)) (u2 (%u01)))
    (* (sqrt (* -2.0 (log u1))) (cos (* 2.0 π u2)))))

;;; ── Marsaglia-Tsang Gamma sampler ───────────────────────────────────────────

(define (%gamma-sample shape)
  (cond
    ((= shape 1.0)
     (- (log (%u01))))
    ((> shape 1.0)
     (let* ((d (- shape (/ 1.0 3.0)))
            (c (/ 1.0 (sqrt (* 9.0 d)))))
       (let loop ()
         (let* ((x  (%standard-normal))
                (cx (+ 1.0 (* c x)))
                (v  (* cx cx cx)))
           (if (and (> cx 0.0)
                    (let ((u (%u01)))
                      (or (< u (- 1.0 (* 0.0331 (expt x 4))))
                          (< (log u)
                             (+ (* 0.5 x x)
                                (* d (+ (- 1.0 v) (log v))))))))
               (* d v)
               (loop))))))
    (else
     (* (%gamma-sample (+ shape 1.0))
        (expt (%u01) (/ 1.0 shape))))))

;;; ── Distribution records ─────────────────────────────────────────────────────

(define-record-type <uniform-distribution>
  (uniform-distribution lo hi)
  uniform-distribution?
  (lo uniform-lo)
  (hi uniform-hi))

(define-record-type <normal-distribution>
  (normal-distribution mean std)
  normal-distribution?
  (mean normal-mean)
  (std  normal-std))

(define-record-type <exponential-distribution>
  (exponential-distribution rate)
  exponential-distribution?
  (rate exponential-rate))

(define-record-type <gamma-distribution>
  (gamma-distribution shape scale)
  gamma-distribution?
  (shape gamma-shape)
  (scale gamma-scale))

(define-record-type <beta-distribution>
  (beta-distribution alpha beta-param)
  beta-distribution?
  (alpha      beta-alpha)
  (beta-param beta-beta-param))

(define-record-type <cauchy-distribution>
  (cauchy-distribution location scale)
  cauchy-distribution?
  (location cauchy-location)
  (scale    cauchy-scale))

(define-record-type <log-normal-distribution>
  (log-normal-distribution mu sigma)
  log-normal-distribution?
  (mu    log-normal-mu)
  (sigma log-normal-sigma))

(define-record-type <chi-squared-distribution>
  (chi-squared-distribution df)
  chi-squared-distribution?
  (df chi-squared-df))

(define-record-type <student-t-distribution>
  (student-t-distribution df)
  student-t-distribution?
  (df student-t-df))

;;; ── sample ───────────────────────────────────────────────────────────────────

(define (sample dist . args)
  (if (null? args)
      (%sample-one dist)
      (let ((n (car args)))
        (let loop ((i 0) (acc '()))
          (if (= i n) (reverse acc)
              (loop (+ i 1) (cons (%sample-one dist) acc)))))))

(define (%sample-one dist)
  (cond
    ((uniform-distribution? dist)
     (let ((lo (uniform-lo dist)) (hi (uniform-hi dist)))
       (+ lo (* (%u01) (- hi lo)))))
    ((normal-distribution? dist)
     (+ (normal-mean dist) (* (normal-std dist) (%standard-normal))))
    ((exponential-distribution? dist)
     (/ (- (log (%u01))) (exponential-rate dist)))
    ((gamma-distribution? dist)
     (* (gamma-scale dist)
        (%gamma-sample (inexact (gamma-shape dist)))))
    ((beta-distribution? dist)
     (let ((a (%gamma-sample (inexact (beta-alpha dist))))
           (b (%gamma-sample (inexact (beta-beta-param dist)))))
       (/ a (+ a b))))
    ((cauchy-distribution? dist)
     (+ (cauchy-location dist)
        (* (cauchy-scale dist) (tan (* π (- (%u01) 0.5))))))
    ((log-normal-distribution? dist)
     (exp (+ (log-normal-mu dist)
             (* (log-normal-sigma dist) (%standard-normal)))))
    ((chi-squared-distribution? dist)
     (* 2.0 (%gamma-sample (* 0.5 (inexact (chi-squared-df dist))))))
    ((student-t-distribution? dist)
     (let* ((df (inexact (student-t-df dist)))
            (z  (%standard-normal))
            (v  (* 2.0 (%gamma-sample (* 0.5 df)))))
       (/ z (sqrt (/ v df)))))
    (else (error "sample: not a distribution" dist))))

;;; ── distribution-pdf ─────────────────────────────────────────────────────────
;;; Symbolic-aware: if x is a sym-var or sym-expr, arithmetic propagates through
;;; the numeric tower and returns a symbolic expression automatically.

(define (distribution-pdf dist x)
  (cond
    ((uniform-distribution? dist)
     (let ((lo (uniform-lo dist)) (hi (uniform-hi dist)))
       (if (and (number? x) (or (< x lo) (> x hi)))
           0
           (/ 1 (- hi lo)))))
    ((normal-distribution? dist)
     (let ((μ (normal-mean dist)) (σ (normal-std dist)))
       (/ (exp (- (/ (* (- x μ) (- x μ)) (* 2 σ σ))))
          (* σ (sqrt (* 2 π))))))
    ((exponential-distribution? dist)
     (let ((λ (exponential-rate dist)))
       (* λ (exp (- (* λ x))))))
    ((gamma-distribution? dist)
     (let ((k (gamma-shape dist)) (θ (gamma-scale dist)))
       (/ (* (expt x (- k 1)) (exp (- (/ x θ))))
          (* (gamma k) (expt θ k)))))
    ((beta-distribution? dist)
     (let ((α (beta-alpha dist)) (β (beta-beta-param dist)))
       (/ (* (expt x (- α 1)) (expt (- 1 x) (- β 1)))
          (beta α β))))
    ((cauchy-distribution? dist)
     (let ((x0 (cauchy-location dist)) (γ (cauchy-scale dist)))
       (/ 1 (* π γ (+ 1 (expt (/ (- x x0) γ) 2))))))
    ((log-normal-distribution? dist)
     (let ((μ (log-normal-mu dist)) (σ (log-normal-sigma dist)))
       (/ (exp (- (/ (expt (- (log x) μ) 2) (* 2 σ σ))))
          (* x σ (sqrt (* 2 π))))))
    ((chi-squared-distribution? dist)
     (distribution-pdf (gamma-distribution (/ (chi-squared-df dist) 2) 2) x))
    ((student-t-distribution? dist)
     (let ((ν (student-t-df dist)))
       (* (/ (gamma (/ (+ ν 1) 2))
             (* (sqrt (* ν π)) (gamma (/ ν 2))))
          (expt (+ 1 (/ (* x x) ν)) (- (/ (+ ν 1) 2))))))
    (else (error "distribution-pdf: not a distribution" dist))))

;;; ── distribution-cdf ─────────────────────────────────────────────────────────

(define (distribution-cdf dist x)
  (cond
    ((uniform-distribution? dist)
     (let ((lo (uniform-lo dist)) (hi (uniform-hi dist)))
       (cond ((and (number? x) (< x lo)) 0)
             ((and (number? x) (> x hi)) 1)
             (else (/ (- x lo) (- hi lo))))))
    ((normal-distribution? dist)
     (let ((μ (normal-mean dist)) (σ (normal-std dist)))
       (* 1/2 (+ 1 (erf (/ (- x μ) (* σ (sqrt 2))))))))
    ((exponential-distribution? dist)
     (let ((λ (exponential-rate dist)))
       (- 1 (exp (- (* λ x))))))
    ((cauchy-distribution? dist)
     (let ((x0 (cauchy-location dist)) (γ (cauchy-scale dist)))
       (+ 1/2 (/ (atan (/ (- x x0) γ)) π))))
    (else (%numerical-cdf dist x))))

(define (%numerical-cdf dist x)
  ;; Midpoint-rule quadrature from a reasonable lower tail.
  ;; For production use, replace with Gauss-Legendre or romberg.
  (let* ((lo (- (if (number? x) x 0.0) 15.0))
         (n  500)
         (h  (/ (- (if (number? x) x 0.0) lo) n)))
    (* h (apply + (map (lambda (i)
                         (let ((xi (+ lo (* (+ i 0.5) h))))
                           (let ((p (distribution-pdf dist xi)))
                             (if (number? p) p 0.0))))
                       (iota n))))))

;;; ── moments ──────────────────────────────────────────────────────────────────

(define (distribution-mean dist)
  (cond
    ((uniform-distribution? dist)
     (/ (+ (uniform-lo dist) (uniform-hi dist)) 2))
    ((normal-distribution? dist)      (normal-mean dist))
    ((exponential-distribution? dist) (/ 1 (exponential-rate dist)))
    ((gamma-distribution? dist)
     (* (gamma-shape dist) (gamma-scale dist)))
    ((beta-distribution? dist)
     (let ((α (beta-alpha dist)) (β (beta-beta-param dist)))
       (/ α (+ α β))))
    ((cauchy-distribution? dist)     'undefined)
    ((log-normal-distribution? dist)
     (exp (+ (log-normal-mu dist)
             (/ (* (log-normal-sigma dist) (log-normal-sigma dist)) 2))))
    ((chi-squared-distribution? dist) (chi-squared-df dist))
    ((student-t-distribution? dist)
     (if (> (student-t-df dist) 1) 0 'undefined))
    (else (error "distribution-mean: not a distribution" dist))))

(define (distribution-variance dist)
  (cond
    ((uniform-distribution? dist)
     (/ (expt (- (uniform-hi dist) (uniform-lo dist)) 2) 12))
    ((normal-distribution? dist)
     (expt (normal-std dist) 2))
    ((exponential-distribution? dist)
     (/ 1 (expt (exponential-rate dist) 2)))
    ((gamma-distribution? dist)
     (* (gamma-shape dist) (expt (gamma-scale dist) 2)))
    ((beta-distribution? dist)
     (let ((α (beta-alpha dist)) (β (beta-beta-param dist)))
       (/ (* α β) (* (expt (+ α β) 2) (+ α β 1)))))
    ((cauchy-distribution? dist)     'undefined)
    ((log-normal-distribution? dist)
     (let ((σ² (expt (log-normal-sigma dist) 2))
           (μ  (log-normal-mu dist)))
       (* (exp (+ (* 2 μ) σ²)) (- (exp σ²) 1))))
    ((chi-squared-distribution? dist) (* 2 (chi-squared-df dist)))
    ((student-t-distribution? dist)
     (let ((ν (student-t-df dist)))
       (if (> ν 2) (/ ν (- ν 2)) 'undefined)))
    (else (error "distribution-variance: not a distribution" dist))))

(define (distribution-std dist)
  (let ((v (distribution-variance dist)))
    (if (number? v) (sqrt v) v)))

;;; ── utilities ────────────────────────────────────────────────────────────────

(define (shuffle! vec)
  (let ((n (vector-length vec)))
    (let loop ((i (- n 1)))
      (when (> i 0)
        (let* ((j   (%uint (+ i 1)))
               (tmp (vector-ref vec i)))
          (vector-set! vec i (vector-ref vec j))
          (vector-set! vec j tmp))
        (loop (- i 1))))
    vec))

(define (shuffle lst)
  (vector->list (shuffle! (list->vector lst))))

(define (random-choice lst)
  (list-ref lst (%uint (length lst))))

(define (random-sample lst k)
  ;; Vitter's Algorithm R (reservoir sampling) — O(n)
  (let ((reservoir (list->vector (list-head lst k)))
        (n         (length lst)))
    (let loop ((i k) (rest (list-tail lst k)))
      (unless (null? rest)
        (let ((j (%uint (+ i 1))))
          (when (< j k)
            (vector-set! reservoir j (car rest)))
          (loop (+ i 1) (cdr rest)))))
    (vector->list reservoir)))

  )) ;; end begin, define-library
