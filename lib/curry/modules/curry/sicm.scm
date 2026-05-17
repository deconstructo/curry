;;; (curry sicm) — Structure and Interpretation of Classical Mechanics
;;;
;;; Implements the core SICM/scmutils interface on top of Curry's CAS:
;;;   tuples (up/down), (partial i), symbolic functions, D operator.
;;;
;;; Reference: Sussman & Wisdom, "Structure and Interpretation of Classical
;;; Mechanics" (2nd ed., MIT Press).  Procedure names follow Chapter 1.
;;;
;;; Supported:
;;;   literal-function   time  coordinate  velocity  acceleration
;;;   square  compose
;;;   Gamma  Gamma-bar
;;;   Lagrange-equations
;;;   Lagrangian->energy  Lagrangian->T  Lagrangian->V
;;;   Euler-Lagrange-operator
;;;   L-free-particle  L-harmonic  L-uniform-acceleration

(import (scheme base))
(import (scheme inexact))

;;; ════════════════════════════════════════════════════════════
;;; § 1  Utilities
;;; ════════════════════════════════════════════════════════════

;;; Function composition: ((compose f g) x) = (f (g x))
(define (compose . fns)
  (if (null? fns)
      (lambda (x) x)
      (let ((f (car fns))
            (rest (apply compose (cdr fns))))
        (lambda args (f (apply rest args))))))

;;; Square: (* x x) for scalars; Σ xᵢ² for tuples
(define (square x)
  (if (tuple? x)
      (let loop ((i 0) (acc 0))
        (if (= i (dimension x))
            acc
            (loop (+ i 1) (+ acc (* (ref x i) (ref x i))))))
      (* x x)))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Literal functions
;;; ════════════════════════════════════════════════════════════

;;; (literal-function 'f) → a symbolic function of one argument t.
;;; When applied to a sym-var, produces the unevaluated expression f(t).
;;; D and partial work through it symbolically:
;;;   (D (literal-function 'f)) → f_t (velocity function)
;;;   ((partial i) L) where L calls literal-function → symbolic EOM

(define (literal-function name)
  (sym-fn name (sym-var 't)))

;;; Multi-argument literal function: (literal-function* 'f n)
;;; produces a symbolic function of n arguments.
(define (literal-function* name n)
  (apply sym-fn name
         (map (lambda (i)
                (sym-var (string->symbol
                          (string-append "_arg" (number->string i)))))
              (iota n))))

;;; ════════════════════════════════════════════════════════════
;;; § 3  Local tuple selectors
;;; ════════════════════════════════════════════════════════════

;;; A local tuple is (up t q qdot [qddot ...]).
;;; These selectors follow SICM notation.

(define (time         local) (ref local 0))
(define (coordinate   local) (ref local 1))
(define (velocity     local) (ref local 2))
(define (acceleration local) (ref local 3))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Path functor Γ
;;; ════════════════════════════════════════════════════════════

;;; (Gamma q) → t → (up t (q t) ((D q) t))
;;;
;;; Lifts a coordinate path q: ℝ→ℝ (or ℝ→upⁿ) to a local-tuple function.
;;; The resulting function maps time to the first-order local tuple:
;;;   time, position, velocity.
(define (Gamma q)
  (lambda (t)
    (up t (q t) ((D q) t))))

;;; (Gamma-bar q n) → first n+1 derivative slots.
;;; Gamma = (Gamma-bar q 2).  Useful for higher-order Lagrangians.
(define (Gamma-bar q n)
  (lambda (t)
    (let loop ((k 0) (dk q) (slots (list t)))
      (if (> k n)
          (apply up (reverse slots))
          (loop (+ k 1) (D dk) (cons (dk t) slots))))))

;;; ════════════════════════════════════════════════════════════
;;; § 5  Euler-Lagrange equations
;;; ════════════════════════════════════════════════════════════

;;; (Lagrange-equations L) → (lambda (q) (lambda (t) residual))
;;;
;;; Returns the Euler-Lagrange residual:
;;;   EL[L](q)(t) = ∂₁L∘Γ(q) − D(∂₂L∘Γ(q))
;;;
;;; Setting this to zero gives the equations of motion.
;;; The residual is automatically simplified.
(define (Lagrange-equations L)
  (lambda (q)
    (let* ((local-fn   (Gamma q))
           (dL-dq      (compose ((partial 1) L) local-fn))
           (dL-dqdot   (compose ((partial 2) L) local-fn))
           (D-dL-dqdot (D dL-dqdot)))
      (lambda (t)
        (simplify (- (dL-dq t) (D-dL-dqdot t)))))))

;;; (Euler-Lagrange-operator L) → function on local tuples
;;; Computes EL residual at a given local tuple (not applied to a path).
;;; Useful for testing Lagrangians without specifying a path.
(define (Euler-Lagrange-operator L)
  (lambda (local)
    (simplify (- (((partial 1) L) local)
                 (((partial 2) L) local)))))   ; no D here — path-free form

;;; ════════════════════════════════════════════════════════════
;;; § 6  Energy and Hamiltonian
;;; ════════════════════════════════════════════════════════════

;;; Total energy E = qdot·∂L/∂qdot − L  (Legendre transform in qdot)
;;; For standard T−V Lagrangians this equals T+V.
(define (Lagrangian->energy L)
  (lambda (local)
    (let ((qdot (velocity local))
          (dL   (((partial 2) L) local)))
      (simplify (- (* qdot dL) (L local))))))

;;; Extract kinetic energy: T = L + V (assuming L = T - V)
;;; Requires the potential function V.
(define (Lagrangian->T L V)
  (lambda (local)
    (simplify (+ (L local) (V (coordinate local))))))

;;; ════════════════════════════════════════════════════════════
;;; § 7  Standard Lagrangians
;;; ════════════════════════════════════════════════════════════

;;; Free particle: L = ½ m qdot²
(define (L-free-particle m)
  (lambda (local)
    (* 1/2 m (square (velocity local)))))

;;; Harmonic oscillator: L = ½ m qdot² − ½ k q²
(define (L-harmonic m k)
  (lambda (local)
    (- (* 1/2 m (square (velocity local)))
       (* 1/2 k (square (coordinate local))))))

;;; Particle in uniform gravitational field: L = ½ m qdot² − m g q
(define (L-uniform-acceleration m g)
  (lambda (local)
    (- (* 1/2 m (square (velocity local)))
       (* m g (coordinate local)))))

;;; General T−V Lagrangian given separate T and V functions
(define (make-Lagrangian T V)
  (lambda (local)
    (- (T local) (V local))))
