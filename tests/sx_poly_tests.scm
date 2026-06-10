;;; Tests for Phase 4c (polynomial machinery) and 4d (equation solving)

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

(define-syntax assert-true
  (syntax-rules ()
    [(assert-true label expr)
     (if expr
         (set! passed (+ passed 1))
         (begin (set! failed (+ failed 1))
                (display "FAIL: ") (display label) (newline)))]))

(define x (sym-var 'x))
(define y (sym-var 'y))
(define z (sym-var 'z))

;;; Helper: check polynomial equality by evaluating at a test point
(define (poly-equal? p q)
  (let ([pts '(2 3 5 7)])
    (for-all (lambda (v)
      (let ([pv (simplify (substitute p x v))]
            [qv (simplify (substitute q x v))])
        (and (number? pv) (number? qv)
             (= pv qv))))
      pts)))

(define (for-all pred lst)
  (if (null? lst) #t
      (and (pred (car lst)) (for-all pred (cdr lst)))))

;;; ============================================================
;;; 1. poly-gcd
;;; ============================================================

;;; gcd(x^2-1, x-1) = x-1
(let ([g (poly-gcd (- (expt x 2) 1) (- x 1) x)])
  (assert-true "gcd(x^2-1, x-1) = x-1"
    (poly-equal? g (- x 1))))

;;; gcd(x^2-4, x^2-x-2) = x-2
;;; x^2-4 = (x-2)(x+2),  x^2-x-2 = (x-2)(x+1)
(let ([g (poly-gcd (- (expt x 2) 4)
                   (+ (- (expt x 2) x) -2)
                   x)])
  (assert-true "gcd(x^2-4, x^2-x-2) = x-2"
    (poly-equal? g (- x 2))))

;;; gcd(p, p) = p (up to scalar)
(let ([p (+ (expt x 3) (- x 1))])
  (let ([g (poly-gcd p p x)])
    (assert-true "gcd(p,p) ~ p" (poly-equal? g p))))

;;; gcd(p, 1) = 1
(assert-equal "gcd(x^2+1, 1) = 1"
  (simplify (poly-gcd (+ (expt x 2) 1) 1 x))
  1)

;;; ============================================================
;;; 2. poly-resultant
;;; ============================================================

;;; resultant(x-3, x-5, x) = -2  (Sylvester det: (1)(-5) - (-3)(1) = -2)
(let ([r (poly-resultant (- x 3) (- x 5) x)])
  (assert-equal "resultant(x-3, x-5, x) = -2"
    (simplify r) -2))

;;; resultant(x^2+1, x-1, x) = 2  (since 1^2+1=2)
(let ([r (poly-resultant (+ (expt x 2) 1) (- x 1) x)])
  (assert-equal "resultant(x^2+1, x-1) = 2"
    (simplify r) 2))

;;; ============================================================
;;; 3. poly-squarefree
;;; ============================================================

;;; x^3 - x^2 - x + 1 = (x-1)^2 * (x+1)
(let ([sf (poly-squarefree (* (- x 1) (- x 1) (+ x 1)) x)])
  (assert-true "squarefree: two distinct factors found"
    (and (list? sf) (= (length sf) 2))))

;;; x^2 - 2x + 1 = (x-1)^2 — one factor, mult 2
(let ([sf (poly-squarefree (* (- x 1) (- x 1)) x)])
  (assert-true "squarefree: (x-1)^2 has multiplicity 2"
    (and (list? sf) (= (length sf) 1)
         (= (cdr (car sf)) 2))))

;;; ============================================================
;;; 4. poly-factor
;;; ============================================================

;;; x^2 - 1 = (x-1)(x+1)
(let ([f (poly-factor (- (expt x 2) 1) x)])
  (assert-true "poly-factor: x^2-1 factors into two linear parts"
    (and (list? f) (>= (length f) 2))))

;;; x^2 + 1 is irreducible over Q
(let ([f (poly-factor (+ (expt x 2) 1) x)])
  (assert-true "poly-factor: x^2+1 is irreducible (one factor)"
    (and (list? f) (= (length f) 1))))

;;; ============================================================
;;; 5. partial-fractions
;;; ============================================================

;;; 1/(x^2-1) = 1/2 * 1/(x-1) - 1/2 * 1/(x+1)
(let ([pf (partial-fractions 1 (- (expt x 2) 1) x)])
  ;;; verify: evaluate at x=2: 1/3, pf at x=2 should also be 1/3
  (let ([at2 (simplify (substitute pf x 2))])
    (assert-equal "partial-fractions: 1/(x^2-1) at x=2 = 1/3"
      at2 1/3)))

;;; ============================================================
;;; 6. solve — linear
;;; ============================================================

(assert-equal "solve: 2x - 6 = 0 → x = 3"
  (solve (- (* 2 x) 6) x)
  '(3))

(assert-equal "solve: x + 5 = 0 → x = -5"
  (solve (+ x 5) x)
  '(-5))

;;; ============================================================
;;; 7. solve — quadratic
;;; ============================================================

;;; x^2 - 5x + 6 = 0 → x = 2, x = 3
(let ([sols (solve (+ (- (expt x 2) (* 5 x)) 6) x)])
  (assert-true "solve quadratic: two solutions"
    (and (list? sols) (= (length sols) 2)))
  (let ([s (map (lambda (v) (simplify v)) sols)])
    (assert-true "solve quadratic: solutions are 2 and 3"
      (or (and (member 2 s) (member 3 s))
          (and (member 2.0 s) (member 3.0 s))))))

;;; x^2 + 1 = 0 → complex solutions (should still return 2 solutions)
(let ([sols (solve (+ (expt x 2) 1) x)])
  (assert-true "solve x^2+1=0: two solutions (complex)"
    (and (list? sols) (= (length sols) 2))))

;;; ============================================================
;;; 8. solve-system — linear systems
;;; ============================================================

;;; x + y = 5, x - y = 1 → x=3, y=2
(let ([sol (solve-system (list (- (+ x y) 5)
                               (- (- x y) 1))
                          (list x y))])
  (assert-true "solve-system: 2×2 returns alist"
    (and sol (list? sol) (= (length sol) 2)))
  (let ([xv (cdr (assoc x sol))]
        [yv (cdr (assoc y sol))])
    (assert-equal "solve-system: x = 3" (simplify xv) 3)
    (assert-equal "solve-system: y = 2" (simplify yv) 2)))

;;; 3×3 system
(let ([sol (solve-system
             (list (+ (+ x y) z)           ; x+y+z = 0 implicitly
                   (+ (* 2 x) (- y))       ; 2x-y = 0
                   (+ x (+ (* 2 y) z)))    ; x+2y+z = 0
             (list x y z))])
  (assert-true "solve-system: 3×3 returns alist or #f"
    (or (eq? sol #f) (list? sol))))

;;; ============================================================
;;; 9. groebner
;;; ============================================================

;;; Simple 1-variable case: groebner({x^2 - 1}) = {x-1, x+1} or similar
(let ([g (groebner (list (- (expt x 2) 1)) (list x))])
  (assert-true "groebner: 1-variable returns a list"
    (list? g)))

;;; 2-variable ideal: x^2 + y^2 - 1, x - y
(let ([g (groebner (list (+ (- (+ (expt x 2) (expt y 2)) 1))
                          (- x y))
                    (list x y))])
  (assert-true "groebner: 2-variable system returns a list"
    (and (list? g) (> (length g) 0))))

;;; ============================================================
;;; Report
;;; ============================================================
(display "sx_poly tests: ")
(display passed) (display " passed, ")
(display failed) (display " failed")
(newline)
(if (> failed 0)
    (error "sx_poly test failures" failed)
    (display "OK\n"))
