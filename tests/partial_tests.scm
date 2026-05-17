;;; partial tests — (partial i) operator: partial derivative by argument index
;;;
;;; (partial i)    → operator P such that (P f) = ∂f/∂(arg_i)
;;; (partial i f)  → same as ((partial i) f)  [shorthand]

(import (scheme base))
(import (scheme inexact))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (check-num label got expected tol)
  (let ((err (abs (- (inexact got) (inexact expected)))))
    (if (<= err tol)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got ") (display got)
               (display "  expected ") (display expected)
               (display "  err=") (display err) (newline)
               (set! fail (+ fail 1))))))

(define (check-infix label expr expected-str)
  (let ((got (sym->infix expr)))
    (if (equal? got expected-str)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got \"") (display got)
               (display "\" expected \"") (display expected-str)
               (display "\"") (newline)
               (set! fail (+ fail 1))))))

(define x (sym-var 'x))
(define y (sym-var 'y))
(define t (sym-var 't))

;;; ════════════════════════════════════════════════════════════
;;; § 1  Basic 2-argument function, symbolic
;;; ════════════════════════════════════════════════════════════

(define (f2 a b) (+ (* a a) (* b b)))

(check-infix "∂f2/∂0 sym" (((partial 0) f2) x y) "2 * x")
(check-infix "∂f2/∂1 sym" (((partial 1) f2) x y) "2 * y")

; 2-arg shorthand form
(check-infix "partial 0 f2 short" ((partial 0 f2) x y) "2 * x")
(check-infix "partial 1 f2 short" ((partial 1 f2) x y) "2 * y")

;;; ════════════════════════════════════════════════════════════
;;; § 2  Concrete (numeric) evaluation
;;; ════════════════════════════════════════════════════════════

; ∂(x²+y²)/∂x at (3,4) = 6
(check "∂f2/∂0 at (3,4)" (((partial 0) f2) 3 4) 6)
(check "∂f2/∂1 at (3,4)" (((partial 1) f2) 3 4) 8)

; ∂(x*y)/∂x at (2,5) = 5
(define (prod a b) (* a b))
(check "∂(x*y)/∂x at (2,5)" (((partial 0) prod) 2 5) 5)
(check "∂(x*y)/∂y at (2,5)" (((partial 1) prod) 2 5) 2)

; inexact
(check-num "∂sin(x)/∂x at pi/6" (((partial 0) (lambda (s) (sin s))) 0.5236)
           (cos 0.5236) 1e-4)

;;; ════════════════════════════════════════════════════════════
;;; § 3  Three-argument function (SICM Lagrangian pattern)
;;; ════════════════════════════════════════════════════════════

;;; L(t, q, qdot) = 1/2·qdot² − 9.8·q
(define (Lfree local-t q qdot)
  (- (* 1/2 (* qdot qdot)) (* 9.8 q)))

;;; ∂L/∂t = 0  (no explicit time dependence)
(check "∂L/∂t numeric" (((partial 0) Lfree) 0.0 1.0 2.0) 0)

;;; ∂L/∂q = −9.8
(check-num "∂L/∂q numeric" (((partial 1) Lfree) 0.0 1.0 2.0) -9.8 1e-12)

;;; ∂L/∂qdot = qdot
(check-num "∂L/∂qdot at qdot=2" (((partial 2) Lfree) 0.0 1.0 2.0) 2.0 1e-12)
(check-num "∂L/∂qdot at qdot=5" (((partial 2) Lfree) 0.0 1.0 5.0) 5.0 1e-12)

;;; Symbolic Lagrangian
(define q   (sym-var 'q))
(define qd  (sym-var 'qd))
(define g98 (sym-var 'g))

(define (Lsym local-t qv qdotv)
  (- (* 1/2 (* qdotv qdotv)) (* g98 qv)))

(check-infix "∂Lsym/∂q"    (((partial 1) Lsym) t q qd) "-g")
(check-infix "∂Lsym/∂qdot" (((partial 2) Lsym) t q qd) "qd")

;;; ════════════════════════════════════════════════════════════
;;; § 4  Agreement with ∂ operator
;;; ════════════════════════════════════════════════════════════

;;; (partial i f) applied to sym-vars should agree with (∂ (f ...) var)
(let* ((via-partial (((partial 0) f2) x y))
       (via-diff    (∂ (f2 x y) x)))
  (check "partial=∂ for f2/∂x" via-partial via-diff))

(let* ((via-partial (((partial 1) f2) x y))
       (via-diff    (∂ (f2 x y) y)))
  (check "partial=∂ for f2/∂y" via-partial via-diff))

;;; ════════════════════════════════════════════════════════════
;;; § 5  Partial on function returning a tuple
;;; ════════════════════════════════════════════════════════════

;;; f(s) = (up s² s³) — D and (partial 0) should agree for 1-arg functions
(define (Fvec s) (up (* s s) (* s s s)))

(let* ((via-D       (D Fvec))
       (via-partial ((partial 0) Fvec)))
  (let ((dD (via-D x))
        (dp (via-partial x)))
    (check "D=partial on tuple-valued fn comp 0" (ref dD 0) (ref dp 0))
    (check "D=partial on tuple-valued fn comp 1" (ref dD 1) (ref dp 1))))

;;; ════════════════════════════════════════════════════════════
;;; § 6  (partial i) is first-class: store and compose
;;; ════════════════════════════════════════════════════════════

(define P0 (partial 0))
(define P1 (partial 1))

(check-infix "stored P0 applied" ((P0 f2) x y) "2 * x")
(check-infix "stored P1 applied" ((P1 f2) x y) "2 * y")

; apply via map
(define partials (list (partial 0) (partial 1)))
(let ((results (map (lambda (op) ((op f2) x y)) partials)))
  (check-infix "map partial 0" (car results)  "2 * x")
  (check-infix "map partial 1" (cadr results) "2 * y"))

;;; ════════════════════════════════════════════════════════════
;;; § 7  Second partial derivatives (Hessian entries)
;;; ════════════════════════════════════════════════════════════

;;; ∂²(x²+y²)/∂x² = 2
(let* ((df-dx (P0 f2))
       (d2f-dx2 ((partial 0) df-dx)))
  (check "∂²f2/∂x² sym" (d2f-dx2 x y) 2))

;;; ∂²(x*y)/∂x∂y = 1
(let* ((df-dy (P1 prod))
       (d2f-dx-dy ((partial 0) df-dy)))
  (check "∂²(x*y)/∂x∂y sym" (d2f-dx-dy x y) 1))

;;; Numerically: ∂²(x²+y²)/∂y² at (3,4) = 2
(let* ((df-dy (P1 f2))
       (d2f-dy2 ((partial 1) df-dy)))
  (check "∂²f2/∂y² at (3,4)" (d2f-dy2 3 4) 2))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
