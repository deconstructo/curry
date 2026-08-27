;;; random_tests.scm — tests for (curry random)

(import (curry random))

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

;;; ── local helpers ────────────────────────────────────────────────────────────

(define (list-sort less? lst)
  (if (or (null? lst) (null? (cdr lst)))
      lst
      (let* ((pivot (car lst))
             (rest  (cdr lst))
             (lo    (filter (lambda (x) (less? x pivot)) rest))
             (hi    (filter (lambda (x) (not (less? x pivot))) rest)))
        (append (list-sort less? lo) (list pivot) (list-sort less? hi)))))

(define (symbol<? a b)
  (string<? (symbol->string a) (symbol->string b)))

(define (delete-duplicates lst)
  (let loop ((rest lst) (seen '()))
    (cond ((null? rest) (reverse seen))
          ((member (car rest) seen) (loop (cdr rest) seen))
          (else (loop (cdr rest) (cons (car rest) seen))))))

;;; ── iota ────────────────────────────────────────────────────────────────────
(check "iota 5"       (iota 5)       '(0 1 2 3 4))
(check "iota 3 10"    (iota 3 10)    '(10 11 12))
(check "iota 4 0 2"   (iota 4 0 2)   '(0 2 4 6))
(check "iota 0"       (iota 0)       '())

;;; ── erf / erfc ──────────────────────────────────────────────────────────────
(check "erf 0"        (erf 0)        0)
(check-approx "erf 1.0"  (erf 1.0)  0.8427007929 1e-7)
(check-approx "erfc 1.0" (erfc 1.0) 0.1572992071 1e-7)
(check-approx "erf+erfc=1" (+ (erf 2.0) (erfc 2.0)) 1.0 1e-14)

;;; Symbolic: erf of a sym-var should return a sym-expr
(let ((x (sym-var 'x)))
  (check-true "erf symbolic" (symbolic? (erf x)))
  (check-true "erfc symbolic" (symbolic? (erfc x))))

;;; ── uniform distribution ─────────────────────────────────────────────────────
(let ((d (uniform-distribution 0 10)))
  (check "uniform mean"     (distribution-mean d)     5)
  (check "uniform variance" (distribution-variance d) 100/12)
  (check "uniform pdf"      (distribution-pdf d 5)    1/10)
  (check "uniform pdf out"  (distribution-pdf d -1)   0)
  (check "uniform cdf lo"   (distribution-cdf d 0)    0)
  (check "uniform cdf hi"   (distribution-cdf d 10)   1)
  (check "uniform cdf mid"  (distribution-cdf d 5)    1/2)
  (let ((s (sample d)))
    (check-true "uniform sample in range" (and (>= s 0) (<= s 10)))))

;;; ── normal distribution ──────────────────────────────────────────────────────
(let ((d (normal-distribution 0.0 1.0)))
  (check-approx "normal mean"     (distribution-mean d)     0.0 1e-15)
  (check-approx "normal variance" (distribution-variance d) 1.0 1e-15)
  (check-approx "normal std"      (distribution-std d)      1.0 1e-15)
  (check-approx "normal pdf at 0" (distribution-pdf d 0.0)
                (/ 1.0 (sqrt (* 2.0 π))) 1e-10)
  (check-approx "normal cdf at 0" (distribution-cdf d 0.0) 0.5 1e-10)
  (check-approx "normal cdf at 1.96" (distribution-cdf d 1.96) 0.975 1e-3))

;;; Symbolic PDF: N(μ,σ) at sym-var should yield a sym-expr
(let ((x (sym-var 'x)))
  (check-true "normal pdf symbolic"
    (symbolic? (distribution-pdf (normal-distribution 0.0 1.0) x))))

;;; Law of large numbers: mean of 2000 N(5,2) samples ≈ 5
(let* ((d    (normal-distribution 5.0 2.0))
       (samp (sample d 2000))
       (mean (/ (apply + samp) 2000)))
  (check-approx "normal LLN mean" mean 5.0 0.15))

;;; ── exponential distribution ─────────────────────────────────────────────────
(let ((d (exponential-distribution 2.0)))
  (check-approx "exp mean"      (distribution-mean d)     0.5  1e-15)
  (check-approx "exp variance"  (distribution-variance d) 0.25 1e-15)
  (check-approx "exp pdf at 1"  (distribution-pdf d 1.0)  (* 2.0 (exp -2.0)) 1e-10)
  (check-approx "exp cdf at 0"  (distribution-cdf d 0.0)  0.0 1e-15)
  (check-approx "exp cdf at 1"  (distribution-cdf d 1.0)  (- 1.0 (exp -2.0)) 1e-10)
  (check-true   "exp sample ≥ 0" (>= (sample d) 0.0)))

;;; ── gamma distribution ───────────────────────────────────────────────────────
(let ((d (gamma-distribution 2.0 3.0)))
  (check-approx "gamma mean"     (distribution-mean d)     6.0 1e-15)
  (check-approx "gamma variance" (distribution-variance d) 18.0 1e-15)
  (check-true   "gamma sample > 0" (> (sample d) 0.0)))

;;; LLN for Gamma(3,2): mean ≈ 6
(let* ((d    (gamma-distribution 3.0 2.0))
       (samp (sample d 2000))
       (mean (/ (apply + samp) 2000.0)))
  (check-approx "gamma LLN mean" mean 6.0 0.3))

;;; ── beta distribution ────────────────────────────────────────────────────────
(let ((d (beta-distribution 2.0 5.0)))
  (check-approx "beta mean"     (distribution-mean d)     (/ 2.0 7.0) 1e-10)
  (check-approx "beta variance" (distribution-variance d) (/ 10.0 (* 49.0 8.0)) 1e-10)
  (let ((s (sample d)))
    (check-true "beta sample in (0,1)" (and (> s 0.0) (< s 1.0)))))

;;; ── cauchy distribution ──────────────────────────────────────────────────────
(let ((d (cauchy-distribution 0.0 1.0)))
  (check "cauchy mean"     (distribution-mean d)     'undefined)
  (check "cauchy variance" (distribution-variance d) 'undefined)
  (check-approx "cauchy cdf at 0" (distribution-cdf d 0.0) 0.5 1e-15)
  (check-approx "cauchy pdf at 0" (distribution-pdf d 0.0) (/ 1.0 π) 1e-10))

;;; ── log-normal distribution ──────────────────────────────────────────────────
(let ((d (log-normal-distribution 0.0 1.0)))
  (check-approx "lognormal mean" (distribution-mean d) (exp 0.5) 1e-10)
  (check-true   "lognormal sample > 0" (> (sample d) 0.0)))

;;; ── chi-squared distribution ─────────────────────────────────────────────────
(let ((d (chi-squared-distribution 4)))
  (check "chi2 mean"     (distribution-mean d)     4)
  (check "chi2 variance" (distribution-variance d) 8)
  (check-true "chi2 sample > 0" (> (sample d) 0.0)))

;;; ── student-t distribution ───────────────────────────────────────────────────
(let ((d (student-t-distribution 10)))
  (check "t mean" (distribution-mean d) 0)
  (check-approx "t variance" (distribution-variance d) (/ 10.0 8.0) 1e-10))

;;; ── shuffle / random-choice / random-sample ──────────────────────────────────
(let* ((lst '(1 2 3 4 5))
       (sh  (shuffle lst)))
  (check "shuffle same elements"
    (equal? (list-sort < sh) '(1 2 3 4 5))
    #t)
  (check "shuffle original unchanged"
    lst '(1 2 3 4 5)))

(let ((v (list->vector '(a b c d e))))
  (shuffle! v)
  (check "shuffle! same elements"
    (equal? (list-sort symbol<? (vector->list v))
            '(a b c d e))
    #t))

(check-true "random-choice in list"
  (member (random-choice '(x y z)) '(x y z)))

(let ((s (random-sample '(1 2 3 4 5 6 7 8 9 10) 3)))
  (check "random-sample length" (length s) 3)
  (check-true "random-sample no duplicates"
    (= (length s) (length (delete-duplicates s)))))

;;; ── SRFI-27 deterministic pseudo-randomize! ────────────────────────────────
;;; random-source-pseudo-randomize! previously ignored its seed arguments
;;; entirely and reseeded from the OS, same as random-source-randomize! --
;;; every value it claimed to make reproducible was actually still random.
;;; These are the regression tests for that fix.

(random-source-pseudo-randomize! (make-random-source) 42 7)
(define %seeded-run-1 (list (random-real) (random-real) (random-integer 1000)))
(random-source-pseudo-randomize! (make-random-source) 42 7)
(define %seeded-run-2 (list (random-real) (random-real) (random-integer 1000)))
(check "pseudo-randomize! same seed -> identical sequence"
  %seeded-run-1 %seeded-run-2)

(random-source-pseudo-randomize! (make-random-source) 1 1)
(define %seeded-run-3 (list (random-real) (random-real) (random-integer 1000)))
(check-true "pseudo-randomize! different seed -> different sequence"
  (not (equal? %seeded-run-1 %seeded-run-3)))

;;; random-source->random-integer previously returned the random-real
;;; generator (a zero-argument procedure) instead of an integer generator,
;;; since it was bound to the same primitive as random-source->random-real.
(let ((gen (random-source->random-integer (make-random-source))))
  (check-true "random-source->random-integer produces an exact integer"
    (exact-integer? (gen 100)))
  (check-true "random-source->random-integer respects its bound"
    (let loop ((i 0))
      (or (= i 50)
          (and (< (gen 100) 100) (loop (+ i 1)))))))

;;; ── SRFI-27 random-source-state-ref / random-source-state-set! ─────────────
;;; Tier 2 gap-closing addition: capture the RNG's current state, run some
;;; draws, restore the captured state, and confirm the same draws repeat.

(random-source-pseudo-randomize! (make-random-source) 99 3)
(define %captured-state (random-source-state-ref (make-random-source)))
(define %post-capture-run-1 (list (random-real) (random-real) (random-integer 1000)))
(random-source-state-set! (make-random-source) %captured-state)
(define %post-capture-run-2 (list (random-real) (random-real) (random-integer 1000)))
(check "random-source-state-ref/set! round-trips the RNG state"
  %post-capture-run-1 %post-capture-run-2)

;;; ── summary ──────────────────────────────────────────────────────────────────
(newline)
(display "Random tests: ") (display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(when (> fail 0) (error "test failures" fail))
