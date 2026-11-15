;;; Library loops that stop only when a counter equals or passes a bound must
;;; not be given a bound that it can never reach (#383). With NaN being
;;; unordered (#367), (= i +nan.0) and (>= t +nan.0) are never true, so
;;; (iota +nan.0) and an integrator with a NaN end time ran forever. They, and
;;; the same loops given a negative or fractional count or a zero step, now
;;; raise an error. Each check is run under `guard`: if one of them were to
;;; loop, this file would hang and ctest would time out.

(import (scheme base) (scheme inexact) (scheme write))
(import (prefix (srfi 1) s1:))
(import (prefix (curry random) r:))
(import (srfi 158))
(import (srfi 194))
(import (srfi 133))
(import (curry ode))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))

(define nan (/ 0. 0.))

;;; iota ─────────────────────────────────────────────────────────────────────
(for-each
 (lambda (entry)
   (let ((name (car entry)) (iota (cdr entry)))
     (check (string-append name ": +nan.0 count raises") (raises? (lambda () (iota nan))) #t)
     (check (string-append name ": -1 count raises") (raises? (lambda () (iota -1))) #t)
     (check (string-append name ": fractional count raises") (raises? (lambda () (iota 2.5))) #t)
     (check (string-append name ": +inf.0 count raises") (raises? (lambda () (iota +inf.0))) #t)
     (check (string-append name ": not a number raises") (raises? (lambda () (iota 'x))) #t)
     (check (string-append name ": 5") (iota 5) '(0 1 2 3 4))
     (check (string-append name ": 0") (iota 0) '())
     (check (string-append name ": start and step") (iota 3 10 5) '(10 15 20))
     (check (string-append name ": an integer-valued flonum count still works") (iota 3.0) '(0 1 2))))
 (list (cons "srfi 1 iota" s1:iota) (cons "curry random iota" r:iota)))

(check "make-iota-generator: +nan.0 raises" (raises? (lambda () (make-iota-generator nan))) #t)
(check "make-iota-generator: -2 raises" (raises? (lambda () (make-iota-generator -2))) #t)
(check "make-iota-generator: 3" (generator->list (make-iota-generator 3)) '(0 1 2))

;;; list->vector and shuffle (a non-list used to ask for a 2^32-element vector) ─
(check "list->vector of a number raises" (raises? (lambda () (list->vector nan))) #t)
(check "list->vector of an improper list raises" (raises? (lambda () (list->vector '(1 2 . 3)))) #t)
(check "list->vector of a circular list raises"
       (let ((c (list 1 2 3))) (set-cdr! (cddr c) c) (raises? (lambda () (list->vector c)))) #t)
(check "list->vector of a list" (list->vector '(1 2 3)) #(1 2 3))
(check "shuffle of a number raises" (raises? (lambda () (r:shuffle nan))) #t)
(check "shuffle keeps the elements"
       (let ((l (r:shuffle '(1 2 3 4 5))))
         (list (length l) (apply + l) (apply * l)))
       '(5 15 120))

;;; list-ref and list-tail read the car and cdr of what they reach: an index past
;;; the end, a negative index or a non-list argument was a segmentation fault.
(check "list-ref past the end raises" (raises? (lambda () (list-ref '(1 2) 5))) #t)
(check "list-ref at the length raises" (raises? (lambda () (list-ref '(1 2) 2))) #t)
(check "list-ref of a non-list raises" (raises? (lambda () (list-ref nan 0))) #t)
(check "list-ref with a negative index raises" (raises? (lambda () (list-ref '(1 2) -1))) #t)
(check "list-ref of an improper list past its pairs raises"
       (raises? (lambda () (list-ref '(1 2 . 3) 2))) #t)
(check "list-ref in range" (list-ref '(a b c) 2) 'c)
(check "list-tail past the end raises" (raises? (lambda () (list-tail '(1) 3))) #t)
(check "list-tail of a non-list raises" (raises? (lambda () (list-tail 5 1))) #t)
(check "list-tail with a negative index raises" (raises? (lambda () (list-tail '(1 2) -1))) #t)
(check "list-tail at the length is the empty list" (list-tail '(1 2) 2) '())
(check "list-tail in range" (list-tail '(a b c) 1) '(b c))

;;; srfi 194 generators ──────────────────────────────────────────────────────
(check "binomial: +nan.0 n raises" (raises? (lambda () (make-binomial-generator nan))) #t)
(check "binomial: fractional n raises" (raises? (lambda () (make-binomial-generator 2.5))) #t)
(check "binomial: -1 raises" (raises? (lambda () (make-binomial-generator -1))) #t)
(check "binomial: 0 trials is 0" ((make-binomial-generator 0)) 0)
(check "binomial: always succeeds when p = 1" ((make-binomial-generator 5 1)) 5)
(check "geometric: p = +nan.0 raises" (raises? (lambda () (make-geometric-generator nan))) #t)
(check "geometric: p = 0 raises" (raises? (lambda () (make-geometric-generator 0))) #t)
(check "geometric: p < 0 raises" (raises? (lambda () (make-geometric-generator -0.5))) #t)
(check "geometric: p > 1 raises" (raises? (lambda () (make-geometric-generator 1.5))) #t)
(check "geometric: p = 1 is 1" ((make-geometric-generator 1)) 1)
(check "poisson: mu = +nan.0 raises" (raises? (lambda () (make-poisson-generator nan))) #t)
(check "poisson: mu = +inf.0 raises" (raises? (lambda () (make-poisson-generator +inf.0))) #t)
(check "poisson: mu < 0 raises" (raises? (lambda () (make-poisson-generator -1))) #t)
(check "poisson: mu = 0 is 0" ((make-poisson-generator 0)) 0)
(check "poisson: an ordinary draw is a non-negative integer"
       (let ((k ((make-poisson-generator 3)))) (and (exact-integer? k) (>= k 0))) #t)

;;; ODE integrators ──────────────────────────────────────────────────────────
(define (f t y) (- y))
(define (accel t q) (- q))
(define (bad-spans who run)
  (check (string-append who ": +nan.0 end time raises") (raises? (lambda () (run 1.0 0 nan 0.1))) #t)
  (check (string-append who ": +nan.0 start time raises") (raises? (lambda () (run 1.0 nan 1 0.1))) #t)
  (check (string-append who ": infinite end time raises") (raises? (lambda () (run 1.0 0 +inf.0 0.1))) #t)
  (check (string-append who ": +nan.0 step raises") (raises? (lambda () (run 1.0 0 1 nan))) #t)
  (check (string-append who ": zero step raises") (raises? (lambda () (run 1.0 0 1 0))) #t)
  (check (string-append who ": negative step raises") (raises? (lambda () (run 1.0 0 1 -0.1))) #t))
(bad-spans "ode-euler" (lambda (y0 t0 t1 h) (ode-euler f y0 t0 t1 h)))
(bad-spans "ode-euler/steps" (lambda (y0 t0 t1 h) (ode-euler/steps f y0 t0 t1 h)))
(bad-spans "ode-rk4" (lambda (y0 t0 t1 h) (ode-rk4 f y0 t0 t1 h)))
(bad-spans "ode-rk4/steps" (lambda (y0 t0 t1 h) (ode-rk4/steps f y0 t0 t1 h)))
(bad-spans "ode-verlet" (lambda (y0 t0 t1 h) (ode-verlet accel y0 0.0 t0 t1 h)))
(bad-spans "ode-verlet/steps" (lambda (y0 t0 t1 h) (ode-verlet/steps accel y0 0.0 t0 t1 h)))
(check "ode-rk45: +nan.0 end time raises" (raises? (lambda () (ode-rk45 f 1.0 0 nan))) #t)
(check "ode-rk45: +nan.0 tolerance raises" (raises? (lambda () (ode-rk45 f 1.0 0 1 nan))) #t)
(check "ode-rk45: zero tolerance raises" (raises? (lambda () (ode-rk45 f 1.0 0 1 0))) #t)
(check "ode-rk45/steps: +nan.0 end time raises" (raises? (lambda () (ode-rk45/steps f 1.0 0 nan))) #t)

(check "ode-rk4: a step too small to advance t raises"
       (raises? (lambda () (ode-rk4 f 1.0 1e6 1000001.0 1e-12))) #t)
(check "ode-rk4: a start time after the end time returns y0 whatever h is"
       (ode-rk4 f 1.0 5 0 0) 1.0)
(check "ode-euler: ... and so does an equal end time" (ode-euler f 2.0 3 3 -1) 2.0)

;; The same calls with ordinary arguments give the right answers.
(define (close? a b) (< (abs (- a b)) 1e-3))
(check "ode-rk4 solves y' = -y" (close? (ode-rk4 f 1.0 0 1 0.01) (exp -1.0)) #t)
(check "ode-euler solves y' = -y roughly" (close? (ode-euler f 1.0 0 1 0.0001) (exp -1.0)) #t)
(check "ode-rk45 solves y' = -y" (close? (ode-rk45 f 1.0 0 1) (exp -1.0)) #t)
(check "ode-rk4/steps ends at t1" (car (car (reverse (ode-rk4/steps f 1.0 0 1 0.25)))) 1.0)
(check "a start time after the end time returns the initial value" (ode-rk4 f 1.0 2 1 0.1) 1.0)

;;; Summary ──────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
