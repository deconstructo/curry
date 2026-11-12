;;; Numeric comparisons with NaN: a NaN is unordered, so =, <, <=, >, >=
;;; are all #f when any operand is NaN (IEEE 754; R6RS 11.7.4.3 gives
;;; (= +nan.0 z) => #f).  num_cmp used to report an unordered pair as
;;; equal, which made (= +nan.0 x), (<= +nan.0 x) and (>= +nan.0 x) #t
;;; for every number x.  max and min return NaN when any argument is NaN,
;;; whatever the order, as Chez, Racket and Guile do.

(import (scheme base) (scheme write) (scheme inexact))

(define pass-count 0)
(define fail-count 0)
(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display name)
             (display " expected ") (write expected)
             (display " got ") (write actual) (newline))))

(define operators (list (cons "=" =) (cons "<" <) (cons "<=" <=) (cons ">" >) (cons ">=" >=)))

(define others
  (list 0.0 -0.0 1.0 -1.5 +inf.0 -inf.0 +nan.0 0 7 -7 (expt 2 100) (- (expt 2 100)) 1/3))

;; Every operator, NaN on either side, against every kind of real number.
;; Called through apply, so this goes through the primitives.
(for-each
  (lambda (op)
    (for-each
      (lambda (x)
        (let ((label (lambda (a b)
                       (let ((port (open-output-string)))
                         (write (list (car op) a b) port)
                         (get-output-string port)))))
          (check (label +nan.0 x) #f (apply (cdr op) (list +nan.0 x)))
          (check (label x +nan.0) #f (apply (cdr op) (list x +nan.0)))))
      others))
  operators)

;; The same through direct calls, which the compiler open-codes.
(define nan +nan.0)
(check "(= nan nan)" #f (= nan nan))
(check "(= nan 0.0)" #f (= nan 0.0))
(check "(< nan 1.0)" #f (< nan 1.0))
(check "(<= nan 1.0)" #f (<= nan 1.0))
(check "(> nan 1.0)" #f (> nan 1.0))
(check "(>= nan 1.0)" #f (>= nan 1.0))
(check "(<= 1 nan)" #f (<= 1 nan))
(check "(>= (expt 2 100) nan)" #f (>= (expt 2 100) nan))
(check "x is NaN exactly when (= x x) is #f" #t (not (= nan nan)))
(define (compiled-le a b) (<= a b))
(check "<= inside a procedure" #f (compiled-le nan 0.0))

;; Chains: a NaN anywhere makes the whole chain #f.
(check "(= 1.0 1.0 nan)" #f (= 1.0 1.0 nan))
(check "(<= 0.0 nan 1.0)" #f (<= 0.0 nan 1.0))
(check "(>= nan 1.0 0.0)" #f (>= nan 1.0 0.0))
(check "(< 1.0 2.0 nan)" #f (< 1.0 2.0 nan))

;; Comparisons without NaN are unchanged.
(check "(= 1.0 1.0)" #t (= 1.0 1.0))
(check "(= 1 1.0)" #t (= 1 1.0))
(check "(= 0.0 -0.0)" #t (= 0.0 -0.0))
(check "(= +inf.0 +inf.0)" #t (= +inf.0 +inf.0))
(check "(< -inf.0 +inf.0)" #t (< -inf.0 +inf.0))
(check "(<= 1 1.0 2)" #t (<= 1 1.0 2))
(check "(>= 2 1.5 1)" #t (>= 2 1.5 1))
(check "(> 1/3 0.3)" #t (> 1/3 0.3))
(check "(< 1 2 3)" #t (< 1 2 3))
(check "(< 1 3 2)" #f (< 1 3 2))

;; max and min propagate NaN regardless of argument order.
(check "(max nan 1.0)" #t (nan? (max nan 1.0)))
(check "(max 1.0 nan)" #t (nan? (max 1.0 nan)))
(check "(min nan 1.0)" #t (nan? (min nan 1.0)))
(check "(min 1.0 nan)" #t (nan? (min 1.0 nan)))
(check "(max 1 nan)" #t (nan? (max 1 nan)))
(check "(min 2.0 nan 0.0)" #t (nan? (min 2.0 nan 0.0)))
(check "(max 1.0 2.0 3.0 nan)" #t (nan? (max 1.0 2.0 3.0 nan)))
(check "(max 1.0 3.0 2.0)" 3.0 (max 1.0 3.0 2.0))
(check "(min 3.0 1.0 2.0)" 1.0 (min 3.0 1.0 2.0))

;; A non-number still raises, NaN or not.
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))
(for-each
  (lambda (op)
    (check (string-append "(" (car op) " nan \"str\") raises") #t
           (raises? (lambda () ((cdr op) nan "str"))))
    (check (string-append "(" (car op) " 'a nan) raises") #t
           (raises? (lambda () ((cdr op) 'a nan)))))
  operators)
(check "(max nan \"s\") raises" #t (raises? (lambda () (max nan "s"))))
(check "(min 'a nan) raises" #t (raises? (lambda () (min 'a nan))))

;; NaN and infinities have no exact value, so exact raises a catchable
;; error.  It used to kill the process with SIGFPE, which matters more now
;; that max and min keep a NaN: (exact (round (max 0 (min 255 nan)))) used
;; to give 255 by accident.
(check "(exact nan) raises" #t (raises? (lambda () (exact nan))))
(check "(exact +inf.0) raises" #t (raises? (lambda () (exact +inf.0))))
(check "(exact -inf.0) raises" #t (raises? (lambda () (exact -inf.0))))
(check "clamp then exact raises" #t
       (raises? (lambda () (exact (round (max 0 (min 255 nan)))))))
(check "(exact 2.5)" 5/2 (exact 2.5))
(check "(exact -0.0)" 0 (exact -0.0))

;; A quaternion with a NaN component is not = to itself.
(define qnan (make-quaternion 1.0 +nan.0 0.0 0.0))
(check "(= qnan qnan)" #f (= qnan qnan))

;; Symbolic: a sign-flagged variable is not ordered against NaN either, and
;; equal? on expressions is structural, so NaN matches NaN there but no
;; longer matches every other number.
(define xp (sym-var 'xp 'positive))
(define xn (sym-var 'xn 'negative))
(define z (sym-var 'z))
(check "(> xp nan)" #f (> xp nan))
(check "(>= xp nan)" #f (>= xp nan))
(check "(< nan xp)" #f (< nan xp))
(check "(< xn nan)" #f (< xn nan))
(check "(<= nan xn)" #f (<= nan xn))
(check "(> xp 0)" #t (> xp 0))
(check "(< xn 0)" #t (< xn 0))
(check "(equal? (+ z nan) (+ z nan))" #t (equal? (+ z nan) (+ z nan)))
(check "(equal? (+ z nan) (+ z 5))" #f (equal? (+ z nan) (+ z 5)))
(check "(equal? (+ z 5) (+ z 5))" #t (equal? (+ z 5) (+ z 5)))

;; Sign predicates were already right; keep them that way.
(check "(zero? nan)" #f (zero? nan))
(check "(positive? nan)" #f (positive? nan))
(check "(negative? nan)" #f (negative? nan))

(display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(exit (if (= fail-count 0) 0 1))
