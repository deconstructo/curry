# Module: (curry random)

*v1.6.4 — 2026-07-10*

Continuous probability distributions for sampling, density evaluation, and
symbolic analysis. Pure Scheme — no C extension beyond the `erf`/`erfc` builtins
added alongside this module.

Discrete distributions are intentionally omitted. Curry's `T_QUANTUM` type already
models discrete probability distributions as quantum superpositions with real
amplitudes; use `(observe q)` for sampling.

## Import

```scheme
(import (curry random))
```

## Symbolic awareness

Every `distribution-pdf` and `distribution-cdf` function is symbolic-aware. Pass a
`(sym-var 'x)` instead of a number to get a CAS expression back:

```scheme
(define x (sym-var 'x))

(distribution-pdf (normal-distribution 0 1) x)
;; => (/ (exp (- (/ (* x x) 2))) (sqrt (* 2 π)))  [symbolic]

(distribution-cdf (normal-distribution 0 1) x)
;; => (* 1/2 (+ 1 (erf (/ x (sqrt 2)))))  [symbolic]

;; Differentiate the PDF symbolically:
(simplify (∂ (distribution-pdf (normal-distribution 0 1) x) x))
;; => (* (- x) (/ (exp (- (/ (* x x) 2))) (sqrt (* 2 π))))
```

This works because the arithmetic inside `distribution-pdf` propagates through
Curry's numeric tower — the same code path that produces a float for numeric `x`
produces a symbolic expression for symbolic `x`.

## Distributions

### Uniform

```scheme
(uniform-distribution lo hi)
```

Continuous uniform distribution on `[lo, hi]`.

```scheme
(define d (uniform-distribution 0 10))

(distribution-mean d)        ;; => 5
(distribution-variance d)    ;; => 100/12
(distribution-pdf d 5)       ;; => 1/10
(distribution-pdf d -1)      ;; => 0       (outside range)
(distribution-cdf d 5)       ;; => 1/2
(sample d)                   ;; => 7.3421… (uniform in [0,10])
```

### Normal (Gaussian)

```scheme
(normal-distribution mean std)
```

Box-Muller sampling. PDF and CDF are symbolic-aware; CDF uses `erf`.

```scheme
(define d (normal-distribution 5.0 2.0))

(distribution-mean d)        ;; => 5.0
(distribution-variance d)    ;; => 4.0
(distribution-std d)         ;; => 2.0
(distribution-pdf d 5.0)     ;; => 0.1994…   (peak)
(distribution-cdf d 5.0)     ;; => 0.5        (median = mean for symmetric dist)
(distribution-cdf d 9.0)     ;; => 0.9772…   (≈ 97.7% below mean + 2σ)
(sample d 5)                 ;; => (4.2 6.1 3.8 5.9 7.1)  (5 draws)
```

### Exponential

```scheme
(exponential-distribution rate)
```

Rate parameter λ; mean = 1/λ. Models waiting times between Poisson events.

```scheme
(define d (exponential-distribution 2.0))   ; mean = 0.5

(distribution-mean d)        ;; => 0.5
(distribution-variance d)    ;; => 0.25
(distribution-pdf d 1.0)     ;; => 2·e^{-2} ≈ 0.2707
(distribution-cdf d 1.0)     ;; => 1 - e^{-2} ≈ 0.8647
(sample d)                   ;; => 0.3142…  (non-negative)
```

### Gamma

```scheme
(gamma-distribution shape scale)
```

Marsaglia-Tsang sampling. Shape k > 0, scale θ > 0. Mean = k·θ, variance = k·θ².

The exponential distribution is Gamma(1, 1/λ). The chi-squared distribution with ν
degrees of freedom is Gamma(ν/2, 2). The Erlang distribution is Gamma(k, θ) for
integer k.

```scheme
(define d (gamma-distribution 3.0 2.0))

(distribution-mean d)        ;; => 6.0
(distribution-variance d)    ;; => 12.0
(sample d)                   ;; => 5.84…   (positive)
```

PDF at x: x^(k-1) · e^(-x/θ) / (Γ(k) · θ^k)

### Beta

```scheme
(beta-distribution alpha beta)
```

Samples via ratio of two gamma variates. Supported on [0, 1]; useful for
modelling probabilities and proportions.

```scheme
(define d (beta-distribution 2.0 5.0))

(distribution-mean d)        ;; => 2/7 ≈ 0.286
(distribution-variance d)    ;; => 10/392 ≈ 0.0255
(sample d)                   ;; => 0.247…  (in (0,1))
```

PDF at x: x^(α-1) · (1-x)^(β-1) / B(α, β)

`B(α,β) = Γ(α)Γ(β)/Γ(α+β)` — exact when α and β are integers or half-integers,
thanks to the symbolic `gamma` and `beta` builtins.

### Cauchy

```scheme
(cauchy-distribution location scale)
```

Heavy-tailed; mean and variance are undefined (infinite). The ratio of two
independent standard normals follows Cauchy(0,1).

```scheme
(define d (cauchy-distribution 0.0 1.0))

(distribution-mean d)        ;; => 'undefined
(distribution-variance d)    ;; => 'undefined
(distribution-pdf d 0.0)     ;; => 1/π ≈ 0.3183
(distribution-cdf d 0.0)     ;; => 1/2
(distribution-cdf d 1.0)     ;; => 3/4
```

### Log-Normal

```scheme
(log-normal-distribution mu sigma)
```

If X ~ N(μ, σ), then e^X ~ LogNormal(μ, σ). Always positive; right-skewed.
Used for multiplicative processes, stock prices, particle sizes.

```scheme
(define d (log-normal-distribution 0.0 1.0))

(distribution-mean d)        ;; => e^{1/2} ≈ 1.6487
(sample d)                   ;; => 1.382…  (positive)
```

### Chi-Squared

```scheme
(chi-squared-distribution df)
```

Chi-squared with `df` degrees of freedom = Gamma(df/2, 2). Arises as the sum
of squares of `df` independent standard normals.

```scheme
(define d (chi-squared-distribution 4))

(distribution-mean d)        ;; => 4
(distribution-variance d)    ;; => 8
(sample d)                   ;; => 3.71…  (positive)
```

### Student's t

```scheme
(student-t-distribution df)
```

t-distribution with `df` degrees of freedom. Symmetric around 0; heavier tails
than the normal; approaches N(0,1) as df → ∞.

```scheme
(define d (student-t-distribution 10))

(distribution-mean d)        ;; => 0        (defined for df > 1)
(distribution-variance d)    ;; => 10/8     (defined for df > 2)
(distribution-pdf d 0.0)     ;; => 0.3891…  (taller-tailed than N(0,1))
```

For df = 1 the variance is `'undefined` (Cauchy distribution).

## Sampling

```scheme
(sample dist)          ; draw one variate
(sample dist n)        ; draw n variates as a list
```

```scheme
(sample (normal-distribution 0.0 1.0))         ;; => -0.471…
(sample (exponential-distribution 1.0) 4)      ;; => (0.32 1.07 0.18 2.41)
```

## Distribution properties

```scheme
(distribution-mean     dist)   ; exact where tractable; 'undefined for Cauchy
(distribution-variance dist)   ; exact where tractable; 'undefined where undefined
(distribution-std      dist)   ; sqrt of variance
(distribution-pdf      dist x) ; probability density at x
(distribution-cdf      dist x) ; cumulative P(X ≤ x)
```

For distributions where the CDF has no closed form (Gamma, Beta, Student's t,
Log-Normal), `distribution-cdf` falls back to midpoint-rule numerical integration
with 500 intervals. This is sufficient for most uses; for high-accuracy CDF
evaluation use `quad` directly on the PDF.

## Utilities

```scheme
(shuffle  lst)         ; return a shuffled copy of lst (Fisher-Yates)
(shuffle! vec)         ; shuffle a vector in-place; returns the vector
(random-choice lst)    ; return one uniformly random element of lst
(random-sample lst k)  ; return k elements chosen without replacement (reservoir)
```

```scheme
(shuffle '(1 2 3 4 5))           ;; => (3 1 5 2 4)  (order varies)

(define deck (iota 52))
(define hand (random-sample deck 5))
hand                             ;; => (7 31 3 49 18)  (5 distinct cards)

(random-choice '(rock paper scissors))  ;; => paper
```

## New builtins: `erf` and `erfc`

Added alongside this module and available globally (no import needed):

```scheme
(erf  x)    ; error function: (2/√π) ∫₀ˣ e^{-t²} dt
(erfc x)    ; complementary: 1 - erf(x)
```

Both are symbolic-aware:

```scheme
(erf  0)           ;; => 0        (exact)
(erf  1.0)         ;; => 0.8427…  (float)
(erfc 1.0)         ;; => 0.1573…  (float)

(let ((x (sym-var 'x)))
  (erf x))         ;; => (erf x)  [symbolic expression]
```

## Examples

### Fitting a distribution to data

```scheme
(import (curry random))

(define data '(2.1 1.8 3.4 0.9 2.7 1.5 4.1 2.3 1.1 3.0))

;; Method-of-moments fit for exponential: rate = 1/mean
(define n    (length data))
(define mean (/ (apply + data) n))
(define fit  (exponential-distribution (/ 1.0 mean)))

(display "Fitted rate: ") (display (exponential-rate fit)) (newline)
(display "Log-likelihood: ")
(display (apply + (map (lambda (x) (log (distribution-pdf fit x))) data)))
(newline)
```

### Monte Carlo integration

```scheme
(import (curry random))

;; Estimate π: fraction of points in unit circle
(define (estimate-pi n)
  (define d (uniform-distribution -1.0 1.0))
  (define hits
    (length (filter (lambda (_) #t)
      (let loop ((i 0) (acc '()))
        (if (= i n) acc
            (let ((x (sample d)) (y (sample d)))
              (loop (+ i 1)
                    (if (< (+ (* x x) (* y y)) 1.0)
                        (cons #t acc)
                        acc))))))))
  (* 4.0 (/ hits n)))

(estimate-pi 100000)    ;; => 3.1416…  (varies by run)
```

### Bayesian update with symbolic prior

```scheme
(import (curry random))

;; Beta-Binomial: Beta prior, binomial likelihood, Beta posterior
;; Prior: Beta(1,1) = Uniform on [0,1]
;; Observe: 7 heads out of 10 flips
;; Posterior: Beta(1+7, 1+3) = Beta(8,4)

(define prior     (beta-distribution 1 1))
(define posterior (beta-distribution 8 4))

(distribution-mean posterior)        ;; => 2/3  (exact rational)
(distribution-std  posterior)        ;; => 0.149…

;; 95% credible interval (numerical):
(define (find-quantile dist p)
  ;; bisection on the CDF
  (let loop ((lo 0.0) (hi 1.0) (i 0))
    (if (> i 50) (/ (+ lo hi) 2.0)
        (let* ((mid (/ (+ lo hi) 2.0))
               (c   (distribution-cdf dist mid)))
          (if (< c p)
              (loop mid hi (+ i 1))
              (loop lo mid (+ i 1)))))))

(find-quantile posterior 0.025)      ;; => 0.357…
(find-quantile posterior 0.975)      ;; => 0.900…
;; 95% CI: (0.357, 0.900) — consistent with 70% heads rate
```

### Symbolic PDF of the normal distribution

```scheme
(import (curry random))

(define x (sym-var 'x))
(define μ (sym-var 'μ))
(define σ (sym-var 'σ))

(define pdf-expr (distribution-pdf (normal-distribution μ σ) x))
;; The full symbolic PDF

;; Verify it integrates to 1 symbolically (CAS will simplify):
;; (∫ pdf-expr x -∞ +∞) => 1

;; Compute the score function (derivative of log-pdf) — used in gradient descent:
(define log-pdf (simplify (log pdf-expr)))
(define score   (simplify (∂ log-pdf x)))
score   ;; => (/ (- μ x) (* σ σ))
```

## Quick reference

| Distribution | Constructor | Mean | Variance |
|---|---|---|---|
| Uniform | `(uniform-distribution lo hi)` | (lo+hi)/2 | (hi-lo)²/12 |
| Normal | `(normal-distribution μ σ)` | μ | σ² |
| Exponential | `(exponential-distribution λ)` | 1/λ | 1/λ² |
| Gamma | `(gamma-distribution k θ)` | kθ | kθ² |
| Beta | `(beta-distribution α β)` | α/(α+β) | αβ/((α+β)²(α+β+1)) |
| Cauchy | `(cauchy-distribution x₀ γ)` | undefined | undefined |
| Log-Normal | `(log-normal-distribution μ σ)` | e^(μ+σ²/2) | (e^σ²-1)e^(2μ+σ²) |
| Chi-Squared | `(chi-squared-distribution ν)` | ν | 2ν |
| Student's t | `(student-t-distribution ν)` | 0 (ν>1) | ν/(ν-2) (ν>2) |
