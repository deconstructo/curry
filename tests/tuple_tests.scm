;;; Tuple tests — up / down tuples (contravariant / covariant)
;;;
;;; (up a b c)   — contravariant tuple (column vector)
;;; (down a b c) — covariant tuple (row vector / covector)
;;; (* (down ...) (up ...)) — scalar contraction (inner product)

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

;;; ════════════════════════════════════════════════════════════
;;; § 1  Construction and predicates
;;; ════════════════════════════════════════════════════════════

(check "up construction"   (up 1 2 3)   (up 1 2 3))
(check "down construction" (down 4 5 6) (down 4 5 6))

; predicates
(check "up? up"       (up? (up 1 2))   #t)
(check "up? down"     (up? (down 1 2)) #f)
(check "down? down"   (down? (down 1 2)) #t)
(check "down? up"     (down? (up 1 2))   #f)
(check "tuple? up"    (tuple? (up 1 2))   #t)
(check "tuple? down"  (tuple? (down 1 2)) #t)
(check "tuple? num"   (tuple? 42)          #f)

;;; ════════════════════════════════════════════════════════════
;;; § 2  Accessors
;;; ════════════════════════════════════════════════════════════

(define v3 (up 10 20 30))
(check "ref 0"     (ref v3 0)     10)
(check "ref 1"     (ref v3 1)     20)
(check "ref 2"     (ref v3 2)     30)
(check "dimension" (dimension v3) 3)

(define d2 (down 7 8))
(check "ref down 0" (ref d2 0) 7)
(check "ref down 1" (ref d2 1) 8)
(check "dim down 2" (dimension d2) 2)

;;; ════════════════════════════════════════════════════════════
;;; § 3  Componentwise arithmetic
;;; ════════════════════════════════════════════════════════════

(check "up + up"     (+ (up 1 2 3) (up 4 5 6))  (up 5 7 9))
(check "down + down" (+ (down 1 2) (down 3 4))  (down 4 6))
(check "up - up"     (- (up 5 7 9) (up 1 2 3))  (up 4 5 6))
(check "- up (neg)"  (- (up 1 2 3))              (up -1 -2 -3))

; scalar scaling
(check "2 * up"    (* 2 (up 1 2 3))    (up 2 4 6))
(check "up * 3"    (* (up 1 2 3) 3)    (up 3 6 9))
(check "1/2 * up"  (* 1/2 (up 2 4 6)) (up 1 2 3))

; variadic + and *
(check "variadic +"  (+ (up 1 0 0) (up 0 1 0) (up 0 0 1)) (up 1 1 1))
(check "variadic *"  (* 2 3 (up 1 2 3))                    (up 6 12 18))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Contraction (inner product)
;;; ════════════════════════════════════════════════════════════

(check "down·up = scalar"  (* (down 1 2 3) (up 4 5 6))  32)   ; 1*4+2*5+3*6=32
(check "unit inner product" (* (down 1 0 0) (up 0 0 1))   0)
(check "identity contraction" (* (down 1 1 1) (up 1 1 1)) 3)

; inexact
(check-num "inexact contraction"
  (* (down 1.0 2.0) (up 3.0 4.0))
  11.0 1e-12)

;;; ════════════════════════════════════════════════════════════
;;; § 5  Conversions
;;; ════════════════════════════════════════════════════════════

(check "tuple->list up"    (tuple->list (up 1 2 3))   '(1 2 3))
(check "tuple->list down"  (tuple->list (down 4 5))   '(4 5))
(check "list->up"   (list->up   '(1 2 3)) (up 1 2 3))
(check "list->down" (list->down '(4 5))   (down 4 5))

;;; ════════════════════════════════════════════════════════════
;;; § 6  Symbolic differentiation through tuples
;;; ════════════════════════════════════════════════════════════

(define x (sym-var 'x))

; ∂/∂x (up x x²) = (up 1 2x)
(let ((q (up x (* x x))))
  (check-infix "∂ up"   (ref (∂ q x) 0) "1")
  (check-infix "∂ up 1" (ref (∂ q x) 1) "2 * x"))

; ∂/∂x (down sin(x) cos(x)) = (down cos(x) -sin(x))
(let ((d (∂ (down (sin x) (cos x)) x)))
  (check-infix "∂ down sin" (ref d 0) "cos(x)")
  (check-infix "∂ down cos" (ref d 1) "-sin(x)"))

; D on a function returning a tuple
(let ((f (D (lambda (s) (up (sin s) (cos s))))))
  (let ((df (f x)))
    (check-infix "D up sin" (ref df 0) "cos(x)")
    (check-infix "D up cos" (ref df 1) "-sin(x)")))

; numerical evaluation after differentiation
(let ((dq (∂ (up (* x x) (* x x x)) x)))
  (let ((at2 (substitute dq x 2)))
    (check "∂(x²,x³)/∂x at 2, comp 0" (ref at2 0) 4)
    (check "∂(x²,x³)/∂x at 2, comp 1" (ref at2 1) 12)))

;;; ════════════════════════════════════════════════════════════
;;; § 7  Symbolic contraction
;;; ════════════════════════════════════════════════════════════

(define vx (sym-var 'vx))
(define vy (sym-var 'vy))

; KE = 1/2 * (down vx vy) · (up vx vy) = 1/2*(vx²+vy²)
(let ((KE (* 1/2 (* (down vx vy) (up vx vy)))))
  (check-infix "KE infix"
    (simplify KE)
    "1/2 * (vx * vx + vy * vy)"))

;;; ════════════════════════════════════════════════════════════
;;; § 8  Zero-length and 1-length tuples
;;; ════════════════════════════════════════════════════════════

(check "up empty"     (up)      (up))
(check "up singleton" (up 42)   (up 42))
(check "dim 0"        (dimension (up)) 0)
(check "dim 1"        (dimension (up 99)) 1)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
