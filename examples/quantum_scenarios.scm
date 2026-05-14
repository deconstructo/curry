;;; quantum_scenarios.scm
;;; Version: 1.0
;;;
;;; Real-world applications of Curry's quantum superposition type.
;;;
;;; The quantum type is a classical probability amplitude distribution over
;;; arbitrary Scheme values.  Arithmetic lifts over branches automatically,
;;; (observe q) collapses to one outcome, and the full distribution is always
;;; available via (quantum-states q).  This makes it useful for any problem
;;; involving:
;;;
;;;   - propagating uncertainty through a calculation
;;;   - ensemble / scenario modelling
;;;   - Monte Carlo estimation by repeated observation
;;;   - probabilistic decisions without explicit loop-over-cases
;;;
;;; Three scenarios are shown:
;;;   1. Ensemble weather forecast  — event planning under forecast uncertainty
;;;   2. Pharmacokinetic dosing     — therapeutic window for a variable patient population
;;;   3. Portfolio scenario analysis — expected return, variance, probability of loss
;;;
;;; Run: ./build/curry examples/quantum_scenarios.scm

(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ---- Utility library ------------------------------------------------

(define (probability-of state) (expt (car state) 2))
(define (value-of       state) (cdr state))

(define (expected-value q)
  (apply + (map (lambda (s) (* (probability-of s) (value-of s)))
                (quantum-states q))))

(define (variance q)
  (let* ((mu  (expected-value q))
         (e-x2 (apply + (map (lambda (s)
                               (* (probability-of s)
                                  (expt (value-of s) 2)))
                             (quantum-states q)))))
    (- e-x2 (* mu mu))))

(define (std-dev q) (sqrt (variance q)))

(define (prob-event q pred?)
  (apply + (map (lambda (s)
                  (if (pred? (value-of s)) (probability-of s) 0))
                (quantum-states q))))

(define (monte-carlo q trials f)
  (let loop ((n 0) (acc 0))
    (if (= n trials)
        (exact->inexact (/ acc trials))
        (loop (+ n 1) (+ acc (f (observe q)))))))

(define (quantum-map f q)
  (superpose (map (lambda (s) (cons (car s) (f (cdr s))))
                  (quantum-states q))))

(define (sort-asc lst)
  (define (insert x sorted)
    (cond ((null? sorted) (list x))
          ((<= x (car sorted)) (cons x sorted))
          (else (cons (car sorted) (insert x (cdr sorted))))))
  (fold-left (lambda (acc x) (insert x acc)) '() lst))

(define (percentile-of samples pct)
  (let* ((sorted (sort-asc samples))
         (idx    (inexact->exact (floor (* pct (length samples))))))
    (list-ref sorted idx)))

(define (display-distribution label q unit)
  (display label) (newline)
  (for-each (lambda (s)
              (display "  ")
              (display (round (* 1000 (probability-of s))))
              (display "/1000  ")
              (display (value-of s))
              (display unit)
              (newline))
            (quantum-states q)))

(define (fmt n decimals)
  (let* ((factor (expt 10 decimals))
         (rounded (/ (round (* n factor)) factor)))
    (exact->inexact rounded)))

(define divider
  (lambda () (display "─────────────────────────────────────────\n")))

;;; ═══════════════════════════════════════════════════════════════════════
;;; Scenario 1: Ensemble weather forecast
;;; ═══════════════════════════════════════════════════════════════════════
;;;
;;; A numerical weather model generates an ensemble of 10 equally likely
;;; runs, but we group them into 5 scenario classes by outcome type.
;;; We want to decide whether to hold an outdoor event and, if so,
;;; whether to hire a marquee.

(divider)
(display "SCENARIO 1: ENSEMBLE WEATHER FORECAST\n")
(divider)

(define forecast
  ; (amplitude . temperature-°C)
  ; Amplitudes are proportional to the number of ensemble members;
  ; (superpose) normalises automatically.
  (superpose '((3 . 28)    ; hot/sunny        — 9/100 ensemble members → prob 9%  scaled
               (5 . 22)    ; warm/partly cloudy — most common outcome
               (4 . 17)    ; mild/overcast
               (2 . 11)    ; cool/showery
               (1 . 6))))  ; cold front / heavy rain

(display-distribution "Temperature forecast distribution:" forecast " °C")

(let* ((mean   (expected-value forecast))
       (spread (std-dev forecast))
       (p-warm  (prob-event forecast (lambda (t) (>= t 20))))
       (p-rain  (prob-event forecast (lambda (t) (<  t 12)))))
  (newline)
  (display "Expected temperature : ") (display (fmt mean 1)) (display " °C\n")
  (display "Std deviation        : ") (display (fmt spread 1)) (display " °C\n")
  (display "P(≥ 20 °C, outdoor-ok): ") (display (fmt (* 100 p-warm) 1)) (display "%\n")
  (display "P(< 12 °C, marquee)  : ") (display (fmt (* 100 p-rain) 1)) (display "%\n")
  (newline)
  (display "Decision: ")
  (cond
    ((>= p-warm 0.55) (display "Hold outdoor event — forecast is favourable.\n"))
    ((>= p-rain 0.25) (display "Book marquee — significant cold-front risk.\n"))
    (else             (display "Outdoor event, but have indoor backup ready.\n"))))

;;; Arithmetic over the superposition: wind-chill adjusted temperature.
;;; Wind chill ≈ T - 3°C at 20 km/h wind.  Apply uniformly.
(define wind-chill-adjusted (- forecast 3))
(newline)
(display "Wind-chill adjusted distribution:\n")
(display-distribution "" wind-chill-adjusted " °C")


;;; ═══════════════════════════════════════════════════════════════════════
;;; Scenario 2: Pharmacokinetic dosing
;;; ═══════════════════════════════════════════════════════════════════════
;;;
;;; Drug X has a therapeutic plasma concentration window of 3–8 mg/L.
;;; Below 3: sub-therapeutic (ineffective).  Above 8: toxic.
;;;
;;; Patient population variability in CYP2D6 metaboliser status determines
;;; the effective Cmax at a standard 200 mg oral dose.
;;; Proportions and Cmax values are illustrative but order-of-magnitude
;;; realistic for a moderate-clearance compound.

(newline)
(divider)
(display "SCENARIO 2: PHARMACOKINETIC DOSING — Drug X, 200 mg oral\n")
(divider)

(define cmax-distribution
  ; (weight . Cmax-mg/L)
  (superpose
    `((1  . 1.4)    ; ultra-rapid metabolisers (~5%):  sub-therapeutic
      (8  . 4.1)    ; extensive (normal, ~40%):        mid-range
      (7  . 6.3)    ; intermediate (~35%):             upper range
      (3  . 9.8)    ; poor metabolisers (~15%):        above safe window
      (1  . 14.6)   ; very poor / drug interaction:    potentially toxic
      )))

(display-distribution "Cmax distribution across patient population:" cmax-distribution " mg/L")

(let* ((therapeutic-lo 3.0)
       (therapeutic-hi 8.0)
       (p-sub   (prob-event cmax-distribution (lambda (c) (< c therapeutic-lo))))
       (p-ok    (prob-event cmax-distribution (lambda (c) (and (>= c therapeutic-lo)
                                                               (<= c therapeutic-hi)))))
       (p-toxic (prob-event cmax-distribution (lambda (c) (> c therapeutic-hi))))
       (mean-c  (expected-value cmax-distribution)))
  (newline)
  (display "Therapeutic window   : 3–8 mg/L\n")
  (display "Expected Cmax        : ") (display (fmt mean-c 2)) (display " mg/L\n")
  (display "P(sub-therapeutic)   : ") (display (fmt (* 100 p-sub) 1)) (display "%\n")
  (display "P(in window)         : ") (display (fmt (* 100 p-ok) 1)) (display "%\n")
  (display "P(toxic)             : ") (display (fmt (* 100 p-toxic) 1)) (display "%\n")
  (newline)
  (display "Recommendation: ")
  (cond
    ((> p-toxic 0.15)
     (display "Genotype patient before prescribing — high toxic risk.\n"))
    ((> p-sub 0.10)
     (display "Consider 250 mg dose for poor responders — sub-therapeutic tail is large.\n"))
    (else
     (display "Standard 200 mg dose appropriate for this population.\n"))))

;;; Estimate dose adjustment needed to bring mean Cmax to centre of window (5.5 mg/L).
;;; Cmax scales linearly with dose, so adjustment factor = target/current mean.
(let* ((target 5.5)
       (mean-c (expected-value cmax-distribution))
       (adj-factor (/ target mean-c))
       (adjusted-dose (* 200 adj-factor))
       (adjusted-cmax (* cmax-distribution adj-factor)))
  (newline)
  (display "Adjusted dose to centre mean at 5.5 mg/L: ")
  (display (fmt adjusted-dose 0)) (display " mg\n")
  (display "Adjusted distribution:\n")
  (display-distribution "" adjusted-cmax " mg/L"))


;;; ═══════════════════════════════════════════════════════════════════════
;;; Scenario 3: Portfolio scenario analysis
;;; ═══════════════════════════════════════════════════════════════════════
;;;
;;; A £10,000 portfolio: 60% equities (FTSE tracker), 40% bonds (gilt fund).
;;; Four macro-economic scenarios with estimated probabilities and 12-month
;;; total returns for each asset class.

(newline)
(divider)
(display "SCENARIO 3: PORTFOLIO — £10,000  (60% equity / 40% bond)\n")
(divider)

(define equity-initial 6000)
(define bond-initial   4000)

; Each scenario: (weight . (equity-return% . bond-return%))
(define scenarios
  (superpose
    `((2 . ( 19 .  3))   ; Soft landing / bull  — growth, mild inflation
      (4 . (  7 .  3))   ; Base case / neutral
      (2 . (-12 .  6))   ; Mild recession       — equities down, bonds bid
      (1 . (-30 . 11))   ; Deep recession       — flight to safety
      (1 . (-42 . -4))   ; Stagflation          — everything down
      )))

; Build the portfolio-value quantum value by lifting arithmetic over branches.
; The value of each branch is a single portfolio £-value.
(define portfolio-value
  (quantum-map
    (lambda (returns)
      (let ((eq-ret  (/ (car returns) 100))
            (bnd-ret (/ (cdr returns) 100)))
        (+ (* equity-initial (+ 1 eq-ret))
           (* bond-initial   (+ 1 bnd-ret)))))
    scenarios))

(display-distribution "Portfolio value in 12 months, by scenario:" portfolio-value " £")

(let* ((mean    (expected-value portfolio-value))
       (sigma   (std-dev portfolio-value))
       (p-loss  (prob-event portfolio-value (lambda (v) (< v 10000))))
       (p-10pct (prob-event portfolio-value (lambda (v) (< v 9000)))))
  (newline)
  (display "Initial value        : £10,000\n")
  (display "Expected value       : £") (display (fmt mean 0)) (newline)
  (display "Expected return      : ")
  (display (fmt (* 100 (/ (- mean 10000) 10000)) 1)) (display "%\n")
  (display "Std deviation        : £") (display (fmt sigma 0)) (newline)
  (display "P(any loss)          : ") (display (fmt (* 100 p-loss) 1)) (display "%\n")
  (display "P(loss > 10%)        : ") (display (fmt (* 100 p-10pct) 1)) (display "%\n"))

;;; Monte Carlo: simulate 2000 independent annual outcomes and compute
;;; the empirical 5th-percentile (Value at Risk proxy).
(let* ((trials  2000)
       (samples (let loop ((n trials) (acc '()))
                  (if (= n 0) acc
                      (loop (- n 1) (cons (observe portfolio-value) acc)))))
       (var-5   (percentile-of samples 0.05))
       (var-1   (percentile-of samples 0.01)))
  (newline)
  (display "Monte Carlo (") (display trials) (display " trials):\n")
  (display "  5th percentile (VaR 95%) : £") (display (fmt var-5 0)) (newline)
  (display "  1st percentile (VaR 99%) : £") (display (fmt var-1 0)) (newline))

;;; Compare with a more conservative 40/60 portfolio.
(newline)
(display "Comparison: 40% equity / 60% bond portfolio:\n")
(define conservative-value
  (quantum-map
    (lambda (returns)
      (let ((eq-ret  (/ (car returns) 100))
            (bnd-ret (/ (cdr returns) 100)))
        (+ (* 4000 (+ 1 eq-ret))
           (* 6000 (+ 1 bnd-ret)))))
    scenarios))
(let* ((mean-c  (expected-value conservative-value))
       (p-loss-c (prob-event conservative-value (lambda (v) (< v 10000)))))
  (display "  Expected value : £") (display (fmt mean-c 0)) (newline)
  (display "  P(any loss)    : ") (display (fmt (* 100 p-loss-c) 1)) (display "%\n"))

(newline)
(divider)
(display "Done.\n")
