;;; (curry sicm) — Structure and Interpretation of Classical Mechanics
;;;
;;; Implements the core SICM/scmutils interface on top of Curry's CAS:
;;;   tuples (up/down), (partial i), symbolic functions, D operator.
;;;
;;; Reference: Sussman & Wisdom, "Structure and Interpretation of Classical
;;; Mechanics" (2nd ed., MIT Press).  Procedure names follow Chapter 1.
;;;
;;; scmutils compatibility shim included — see § 0.
;;;
;;; Supported:
;;;   literal-function  literal-function*
;;;   time  coordinate  velocity  acceleration  momentum
;;;   square  compose
;;;   Gamma  Gamma-bar
;;;   Lagrange-equations  Euler-Lagrange-operator
;;;   Lagrangian->energy  Lagrangian->T  Lagrangian->V
;;;   Lagrangian->Hamiltonian  Hamilton-equations
;;;   L-free-particle  L-harmonic  L-uniform-acceleration  make-Lagrangian
;;;   L-central-rectangular  L-Kepler-polar
;;;   Poisson-bracket  commutator
;;;   pe  print-expression  show-expression  define-symbolic  literal-number

(import (scheme base))
(import (scheme inexact))

;;; ════════════════════════════════════════════════════════════
;;; § 0  scmutils compatibility shim
;;; ════════════════════════════════════════════════════════════

;;; Pretty-print a symbolic expression — scmutils calls this pe / print-expression.
;;; Uses sym->infix for readable output; falls back to write for non-symbolic values.
(define (pe expr)
  (if (or (symbolic? expr) (sym-var? expr))
      (display (sym->infix expr))
      (write expr))
  (newline))

(define print-expression pe)
(define show-expression   pe)

;;; Declare one or more symbolic variables in one go.
;;;   (define-symbolic t x y z)
;;; is equivalent to:
;;;   (define t (sym-var 't))  (define x (sym-var 'x))  ...
(define-syntax define-symbolic
  (syntax-rules ()
    ((_ var)
     (define var (sym-var 'var)))
    ((_ var rest ...)
     (begin (define var (sym-var 'var))
            (define-symbolic rest ...)))))

;;; scmutils alias: (literal-number 'x) ≡ (sym-var 'x)
(define literal-number sym-var)

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

;;; (literal-function 'f)                    — 1-arg function of t (scmutils default)
;;; (literal-function 'f n)                  — n-arg function (integer arity)
;;; (literal-function 'f (-> ...))           — scmutils type annotation, ignored
(define (literal-function name . rest)
  (if (and (not (null? rest)) (integer? (car rest)))
      (literal-function* name (car rest))
      (sym-fn name (sym-var 't))))

;;; Multi-argument literal function: (literal-function* 'f n)
;;; produces a symbolic function of n arguments.
(define (literal-function* name n)
  (let loop ((i 0) (args '()))
    (if (= i n)
        (apply sym-fn name (reverse args))
        (loop (+ i 1)
              (cons (sym-var (string->symbol
                              (string-append "_arg" (number->string i))))
                    args)))))

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

;;; Extract potential energy: V(local) = −L(local with qdot=0)
;;; Works because T vanishes at zero velocity for standard Lagrangians.
(define (Lagrangian->V L)
  (lambda (local)
    (simplify (- (L (up (time local) (coordinate local) 0))))))

;;; Extract kinetic energy: T = L + V  (since L = T − V)
(define (Lagrangian->T L)
  (lambda (local)
    (simplify (+ (L local) ((Lagrangian->V L) local)))))

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

;;; ════════════════════════════════════════════════════════════
;;; § 8  Multi-DOF Lagrangians
;;; ════════════════════════════════════════════════════════════

;;; 2D / n-D free particle: L = ½ m qdot·qdot
;;; q and qdot are up-tuples; dot-product gives the inner product.
(define (L-free-particle-nd m)
  (lambda (local)
    (* 1/2 m (dot-product (velocity local) (velocity local)))))

;;; n-D isotropic harmonic oscillator: L = ½ m |qdot|² − ½ k |q|²
(define (L-harmonic-nd m k)
  (lambda (local)
    (- (* 1/2 m (dot-product (velocity local) (velocity local)))
       (* 1/2 k (dot-product (coordinate local) (coordinate local))))))

;;; Central force in 2D Cartesian: L = ½ m (ẋ² + ẏ²) − V(r)
;;; V-fn is a function of r = sqrt(x²+y²).
(define (L-central-rectangular m V-fn)
  (lambda (local)
    (let* ((q    (coordinate local))
           (qdot (velocity local))
           (r    (sqrt (dot-product q q))))
      (- (* 1/2 m (dot-product qdot qdot))
         (V-fn r)))))

;;; Kepler problem in polar coordinates: q = (up r θ), qdot = (up ṙ θ̇)
;;; L = ½ m (ṙ² + r²θ̇²) + GM/r
(define (L-Kepler-polar m GM)
  (lambda (local)
    (let ((q    (coordinate local))
          (qdot (velocity local)))
      (let ((r    (ref q 0))
            (rdot (ref qdot 0))
            (tdot (ref qdot 1)))
        (+ (* 1/2 m (+ (* rdot rdot) (* r r tdot tdot)))
           (/ GM r))))))

;;; ════════════════════════════════════════════════════════════
;;; § 9  Hamiltonian mechanics
;;; ════════════════════════════════════════════════════════════

;;; A Hamiltonian state is (up t q p) where p is the conjugate momentum.
;;; Selectors follow the same convention as local tuples.
(define (momentum state) (ref state 2))

;;; Legendre transform: given a Lagrangian L, return the Hamiltonian H.
;;;
;;; Works for standard Lagrangians with diagonal (separable) kinetic energy
;;; T = ½ mᵢ qdotᵢ².  The Legendre transform is:
;;;   H(t,q,p) = p·qdot(p) − L(t,q,qdot(p))
;;; where qdot(p) is obtained by inverting p = ∂L/∂qdot.
;;;
;;; For T = ½ m |qdot|²: p = m·qdot, so qdot = p/m.
;;; The effective mass is computed as mᵢ = ∂pᵢ/∂qdotᵢ evaluated at a
;;; probe point.  This is exact for linear momentum maps (diagonal T).
(define (Lagrangian->Hamiltonian L)
  (lambda (H-state)
    (let* ((t (time H-state))
           (q (coordinate H-state))
           (p (momentum H-state))
           ;; V(q) = −L(t,q,0) — potential energy, independent of momentum
           (V-val  ((Lagrangian->V L) (up t q 0))))
      (if (tuple? p)
          ;; Multi-DOF: p is a down-tuple.  Compute mass coefficients from
          ;; the momentum map at a probe velocity, then H = Σ pᵢ²/(2mᵢ) + V.
          (let* ((n (dimension p))
                 (v-list (let loop ((i 0) (acc '()))
                           (if (= i n) (reverse acc)
                               (loop (+ i 1)
                                     (cons (sym-var (string->symbol
                                                     (string-append "_vL" (number->string i))))
                                           acc)))))
                 (v-tup   (apply up v-list))
                 (local-v (up t q v-tup))
                 (p-map   (((partial 2) L) local-v))
                 (m-list  (map (lambda (p-i v-i) (∂ p-i v-i))
                               (let loop ((i 0) (acc '()))
                                 (if (= i n) (reverse acc)
                                     (loop (+ i 1) (cons (ref p-map i) acc))))
                               v-list))
                 ;; T*(p) = Σ pᵢ²/(2mᵢ)
                 (T*-val  (apply + (map (lambda (p-i m-i) (/ (* p-i p-i) (* 2 m-i)))
                                        (let loop ((i 0) (acc '()))
                                          (if (= i n) (reverse acc)
                                              (loop (+ i 1) (cons (ref p i) acc))))
                                        m-list))))
            (simplify (+ T*-val V-val)))
          ;; 1-DOF scalar case: H = p²/(2m) + V
          (let* ((v-sym   (sym-var (quote _vL0)))
                 (local-v (up t q v-sym))
                 (p-map   (((partial 2) L) local-v))
                 (m-eff   (∂ p-map v-sym))
                 (T*-val  (/ (* p p) (* 2 m-eff))))
            (simplify (+ T*-val V-val)))))))

;;; Direct Hamiltonian for separable T = ½ p²/m:
;;; H = |p|²/(2m) + V(q)
(define (make-Hamiltonian T* V)
  (lambda (H-state)
    (+ (T* H-state) (V (coordinate H-state)))))

;;; Hamiltonian equations: given H, return (lambda (H-state) (up dq/dt dp/dt))
;;; dqᵢ/dt = ∂H/∂pᵢ,  dpᵢ/dt = −∂H/∂qᵢ
(define (Hamilton-equations H)
  (lambda (H-state)
    (let* ((t    (time H-state))
           (q    (coordinate H-state))
           (p    (momentum H-state))
           (dH-dq  (((partial 1) H) H-state))
           (dH-dp  (((partial 2) H) H-state)))
      (up dH-dp (negate-tuple dH-dq)))))

;;; Negate all components of a tuple or return negation of a scalar.
(define (negate-tuple x)
  (if (tuple? x)
      (let loop ((i 0) (acc '()))
        (if (= i (dimension x))
            (apply (if (up? x) up down) (reverse acc))
            (loop (+ i 1) (cons (- (ref x i)) acc))))
      (- x)))

;;; ════════════════════════════════════════════════════════════
;;; § 10  Poisson bracket and commutator
;;; ════════════════════════════════════════════════════════════

;;; Poisson bracket {f, g} = Σᵢ (∂f/∂qᵢ ∂g/∂pᵢ − ∂f/∂pᵢ ∂g/∂qᵢ)
;;; f and g are functions of Hamiltonian state (up t q p).
(define (Poisson-bracket f g)
  (lambda (H-state)
    (let* ((df-dq (((partial 1) f) H-state))
           (df-dp (((partial 2) f) H-state))
           (dg-dq (((partial 1) g) H-state))
           (dg-dp (((partial 2) g) H-state)))
      (simplify
        (if (tuple? df-dq)
            ;; Multi-DOF: sum over components
            (let loop ((i 0) (acc 0))
              (if (= i (dimension df-dq))
                  acc
                  (loop (+ i 1)
                        (+ acc
                           (- (* (ref df-dq i) (ref dg-dp i))
                              (* (ref df-dp i) (ref dg-dq i)))))))
            ;; 1-DOF scalar
            (- (* df-dq dg-dp) (* df-dp dg-dq)))))))

;;; Commutator of two operators: [A, B] = A∘B − B∘A
(define (commutator A B)
  (lambda (f)
    (lambda (x)
      (- ((A (B f)) x) ((B (A f)) x)))))
