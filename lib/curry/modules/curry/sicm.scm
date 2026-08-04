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
;;;
;;; Rigid body (Ch 2):
;;;   rotation-matrix-from-Euler  Euler->omega-body  Euler->omega-space
;;;   T-rigid-body  L-rigid-body  principal-value
;;;   mat-ref  mat-transpose  mat-vec-mul  mat-mat-mul
;;;
;;; Numerical trajectory evolution (Ch 3–4):
;;;   evolve-Hamiltonian  evolve-Lagrangian  lagrangian->hamiltonian-state
;;;   stoermer-verlet-step  poincare-section
;;;
;;; Canonical transformations (Ch 5):
;;;   ct-jacobian  symplectic-transform?  canonical?
;;;   F->C  F2->CT  rectangular->polar  action-angle
;;;
;;; Lie transforms and perturbation theory (Ch 6–7):
;;;   pb  Lie-derivative  Lie-transform
;;;   secular-average  secular-perturbation

(define-library (curry sicm)
  (import (scheme base))
  (import (scheme inexact))
  (export
    ;; § 0 scmutils compatibility shim
    pe print-expression show-expression define-symbolic literal-number
    ;; § 1 utilities
    compose square
    ;; § 2 literal functions
    literal-function literal-function*
    ;; § 3 local tuple selectors
    time coordinate velocity acceleration
    ;; § 4 path functor
    Gamma Gamma-bar
    ;; § 5 Euler-Lagrange equations
    Lagrange-equations Euler-Lagrange-operator
    ;; § 6 energy and Hamiltonian
    Lagrangian->energy Lagrangian->V Lagrangian->T
    ;; § 7 standard Lagrangians
    L-free-particle L-harmonic L-uniform-acceleration make-Lagrangian
    ;; § 8 multi-DOF Lagrangians
    L-free-particle-nd L-harmonic-nd L-central-rectangular L-Kepler-polar
    ;; § 9 Hamiltonian mechanics
    momentum Lagrangian->Hamiltonian make-Hamiltonian Hamilton-equations
    ;; § 10 Poisson bracket and commutator
    Poisson-bracket commutator
    ;; § 11 matrix helpers
    mat-ref mat-transpose mat-vec-mul mat-mat-mul
    ;; § 12 rigid body mechanics
    rotation-matrix-from-Euler Euler->omega-body Euler->omega-space
    T-rigid-body L-rigid-body principal-value
    ;; § 13 numerical trajectory evolution
    stoermer-verlet-step evolve-Hamiltonian
    lagrangian->hamiltonian-state evolve-Lagrangian poincare-section
    ;; § 14 canonical transformations
    ct-jacobian ct-symplectic-J symplectic-transform? canonical?
    F->C F2->CT ct-f2-mixed-jac rectangular->polar action-angle
    ;; § 15 Lie transforms and perturbation theory
    pb Lie-derivative Lie-transform secular-average secular-perturbation
    ;; § 16 controller synthesis
    lqr-mat-zero lqr-mat-eye lqr-mat-ref
    lqr-mat-add lqr-mat-sub lqr-mat-scale lqr-mat-mul lqr-mat-transpose
    lqr-mat-norm lqr-mat-inv lqr-gauss-solve lqr-mat-kron lqr-vec lqr-unvec
    lyapunov-solve lqr-continuous lqr linearise make-controller)
  (begin

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

;;; ════════════════════════════════════════════════════════════
;;; § 11  3×3 matrix helpers (internal)
;;;        Matrices are represented as (list row0 row1 row2)
;;;        where each row is (list e0 e1 e2).
;;; ════════════════════════════════════════════════════════════

(define (mat-ref M i j) (list-ref (list-ref M i) j))

(define (mat-transpose M)
  (list (list (mat-ref M 0 0) (mat-ref M 1 0) (mat-ref M 2 0))
        (list (mat-ref M 0 1) (mat-ref M 1 1) (mat-ref M 2 1))
        (list (mat-ref M 0 2) (mat-ref M 1 2) (mat-ref M 2 2))))

;;; Multiply a 3×3 matrix by a 3-list vector.
(define (mat-vec-mul M v)
  (map (lambda (row)
         (+ (* (list-ref row 0) (list-ref v 0))
            (* (list-ref row 1) (list-ref v 1))
            (* (list-ref row 2) (list-ref v 2))))
       M))

;;; Multiply two 3×3 matrices.
(define (mat-mat-mul M1 M2)
  (let ((M2t (mat-transpose M2)))
    (map (lambda (row1)
           (map (lambda (col2)
                  (apply + (map * row1 col2)))
                M2t))
         M1)))

;;; ════════════════════════════════════════════════════════════
;;; § 12  Rigid body mechanics (SICM Ch 2)
;;;
;;;  ZXZ Euler angles: first rotate by phi around space Z,
;;;  then by theta around the nodal X axis, then by psi around body Z.
;;;  Reference: Sussman & Wisdom SICM 2nd ed., Chapter 2.
;;; ════════════════════════════════════════════════════════════

;;; 3×3 rotation matrix for ZXZ Euler angles (phi, theta, psi).
;;; R = Rz(phi) * Rx(theta) * Rz(psi)
;;; Returns a list of 3 rows, each a list of 3 elements.
(define (rotation-matrix-from-Euler phi theta psi)
  (let ((cphi   (cos phi))   (sphi   (sin phi))
        (ctheta (cos theta)) (stheta (sin theta))
        (cpsi   (cos psi))   (spsi   (sin psi)))
    (list
      (list (- (* cphi cpsi) (* sphi ctheta spsi))
            (- (- (* cphi spsi)) (* sphi ctheta cpsi))
            (* sphi stheta))
      (list (+ (* sphi cpsi) (* cphi ctheta spsi))
            (+ (- (* sphi spsi)) (* cphi ctheta cpsi))
            (- (* cphi stheta)))
      (list (* stheta spsi)
            (* stheta cpsi)
            ctheta))))

;;; Angular velocity in the body frame, from a ZXZ Euler-angle local tuple.
;;; local = (up t (up phi theta psi) (up phidot thetadot psidot))
;;; Returns (up omega1 omega2 omega3) in principal-axis coordinates.
(define (Euler->omega-body local)
  (let* ((q        (coordinate local))
         (qdot     (velocity local))
         (phi      (ref q 0)) (theta (ref q 1)) (psi   (ref q 2))
         (phidot   (ref qdot 0))
         (thetadot (ref qdot 1))
         (psidot   (ref qdot 2)))
    (up (+ (* phidot (sin theta) (sin psi)) (* thetadot (cos psi)))
        (- (* phidot (sin theta) (cos psi)) (* thetadot (sin psi)))
        (+ (* phidot (cos theta)) psidot))))

;;; Angular velocity in the space frame, from a ZXZ Euler-angle local tuple.
;;; local = (up t (up phi theta psi) (up phidot thetadot psidot))
;;; Returns (up omega1 omega2 omega3) in space-frame coordinates.
(define (Euler->omega-space local)
  (let* ((q        (coordinate local))
         (qdot     (velocity local))
         (phi      (ref q 0)) (theta (ref q 1)) (psi   (ref q 2))
         (phidot   (ref qdot 0))
         (thetadot (ref qdot 1))
         (psidot   (ref qdot 2)))
    (up (+ (* thetadot (cos phi)) (* psidot (sin theta) (sin phi)))
        (- (* thetadot (sin phi)) (* psidot (sin theta) (cos phi)))
        (+ phidot (* psidot (cos theta))))))

;;; Kinetic energy of a rigid body in its principal-axis frame.
;;; A, B, C are the three principal moments of inertia.
;;; T = ½(A·ω1² + B·ω2² + C·ω3²)
(define (T-rigid-body A B C)
  (lambda (local)
    (let* ((omega (Euler->omega-body local))
           (w1 (ref omega 0)) (w2 (ref omega 1)) (w3 (ref omega 2)))
      (* 1/2 (+ (* A w1 w1) (* B w2 w2) (* C w3 w3))))))

;;; Torque-free rigid body Lagrangian: L = T (no potential energy).
(define (L-rigid-body A B C)
  (T-rigid-body A B C))

;;; Reduce angle x to the principal interval (−xmax, xmax].
;;; Example: ((principal-value (/ pi 2)) angle) normalises to (-pi/2, pi/2].
(define (principal-value xmax)
  (let ((two-xmax (* 2 xmax)))
    (lambda (x)
      (let ((y (- x (* two-xmax (floor (/ x two-xmax))))))
        (if (> y xmax) (- y two-xmax) y)))))

;;; ════════════════════════════════════════════════════════════
;;; § 13  Numerical trajectory evolution (SICM Ch 3–4)
;;;
;;; Symplectic Störmer-Verlet integration of Hamiltonian systems.
;;; Key property: the integrator is symplectic — it preserves the
;;; phase-space volume form.  Energy error is bounded (no secular
;;; drift), unlike non-symplectic methods (e.g. plain RK4) which
;;; show unbounded energy drift on long runs.
;;;
;;; State representation: (up t q p)
;;;   t — time (flonum)
;;;   q — generalised coordinates (flonum or up-tuple of flonums)
;;;   p — conjugate momenta     (flonum or up-tuple of flonums)
;;; ════════════════════════════════════════════════════════════

;;; ── Internal helpers ──────────────────────────────────────────────────────

;;; Integer list 0 .. n−1.
(define (sv-iota n)
  (let loop ((i (- n 1)) (acc '()))
    (if (< i 0) acc (loop (- i 1) (cons i acc)))))

;;; Element-wise binary op on two scalars-or-up-tuples of same shape.
(define (sv-map2 f a b)
  (if (tuple? a)
      (apply up (map (lambda (i) (f (ref a i) (ref b i))) (sv-iota (dimension a))))
      (f a b)))

;;; Element-wise unary op on a scalar-or-up-tuple.
(define (sv-map1 f a)
  (if (tuple? a)
      (apply up (map (lambda (i) (f (ref a i))) (sv-iota (dimension a))))
      (f a)))

;;; Scalar/tuple arithmetic.  Dispatch on operand type.
(define (sv+ a b) (sv-map2 + a b))
(define (sv- a b) (sv-map2 - a b))
(define (sv* s x) (sv-map1 (lambda (xi) (* s xi)) x))

;;; ── Finite-difference gradient ────────────────────────────────────────────

(define sv-eps 1e-7)   ; central-difference step

;;; Perturb component i of x (scalar or up-tuple) by eps.
(define (sv-perturb x i eps)
  (if (tuple? x)
      (apply up (map (lambda (j) (if (= j i) (+ (ref x j) eps) (ref x j)))
                     (sv-iota (dimension x))))
      (+ x eps)))   ; scalar: i must be 0

;;; Central-difference gradient of f: ℝⁿ → ℝ with respect to x.
;;; Returns the same shape as x (scalar or up-tuple).
(define (fd-grad f x)
  (if (tuple? x)
      (apply up
        (map (lambda (i)
               (/ (- (f (sv-perturb x i sv-eps))
                     (f (sv-perturb x i (- sv-eps))))
                  (* 2.0 sv-eps)))
             (sv-iota (dimension x))))
      (/ (- (f (+ x sv-eps)) (f (- x sv-eps)))
         (* 2.0 sv-eps))))

;;; ∂H/∂q at a Hamiltonian state — vary coordinate only.
(define (sv-dH-dq H state)
  (let ((t (time state)) (p (momentum state)))
    (fd-grad (lambda (q) (H (up t q p))) (coordinate state))))

;;; ∂H/∂p at a Hamiltonian state — vary momentum only.
(define (sv-dH-dp H state)
  (let ((t (time state)) (q (coordinate state)))
    (fd-grad (lambda (p) (H (up t q p))) (momentum state))))

;;; ── Störmer-Verlet symplectic step ───────────────────────────────────────
;;;
;;;   p_{n+1/2} = p_n       − (h/2) ∂H/∂q(t_n,   q_n,   p_n)
;;;   q_{n+1}   = q_n       + h     ∂H/∂p(t_n,   q_n,   p_{n+1/2})
;;;   p_{n+1}   = p_{n+1/2} − (h/2) ∂H/∂q(t_{n+1}, q_{n+1}, p_{n+1/2})
;;;   t_{n+1}   = t_n       + h
;;;
;;; Two evaluations of ∂H/∂q and one of ∂H/∂p per step.
;;; Time-reversible and volume-preserving by construction.

(define (stoermer-verlet-step H dt state)
  (let* ((h2    (* 0.5 (inexact dt)))
         (t     (time state))
         (q     (coordinate state))
         (p     (momentum state))
         ;; half-kick: update momentum by half a step
         (p-mid (sv- p (sv* h2 (sv-dH-dq H state))))
         ;; drift: advance position a full step using half-kicked momentum
         (dtf   (inexact dt))
         (q1    (sv+ q (sv* dtf (sv-dH-dp H (up t q p-mid)))))
         (t1    (+ t dtf))
         ;; half-kick: complete the momentum update
         (p1    (sv- p-mid (sv* h2 (sv-dH-dq H (up t1 q1 p-mid))))))
    (up t1 q1 p1)))

;;; ── evolve-Hamiltonian ────────────────────────────────────────────────────

;;; (evolve-Hamiltonian H state0 dt n-steps) → list of (up t q p)
;;;
;;; H      — Hamiltonian function  (up t q p) → real
;;; state0 — initial state         (up t q p)
;;; dt     — time step             (flonum, or exact converted to flonum)
;;; n-steps — number of integration steps
;;;
;;; Returns a list of (n-steps + 1) states, including state0.
;;; Uses the symplectic Störmer-Verlet integrator.

(define (evolve-Hamiltonian H state0 dt n-steps)
  (let ((dtf (inexact dt)))
    (let loop ((i 0) (state state0) (acc (list state0)))
      (if (>= i n-steps)
          (reverse acc)
          (let ((next (stoermer-verlet-step H dtf state)))
            (loop (+ i 1) next (cons next acc)))))))

;;; ── evolve-Lagrangian ─────────────────────────────────────────────────────

;;; Convert a Lagrangian local tuple (up t q qdot) to a Hamiltonian
;;; state (up t q p) by numerically computing p = ∂L/∂qdot.

(define (lagrangian->hamiltonian-state L local)
  (let* ((t    (time local))
         (q    (coordinate local))
         (qdot (velocity local))
         (p    (fd-grad (lambda (v) (L (up t q v))) qdot)))
    (up t q p)))

;;; (evolve-Lagrangian L local0 dt n-steps) → list of (up t q p)
;;;
;;; L      — Lagrangian function   (up t q qdot) → real
;;; local0 — initial local tuple   (up t q qdot)
;;; dt     — time step
;;; n-steps — number of steps
;;;
;;; Converts to Hamiltonian using Lagrangian->Hamiltonian (symbolic mass
;;; extraction), then integrates symplectically.  Works for standard
;;; T−V Lagrangians with diagonal kinetic energy (T = Σ ½ mᵢ qdotᵢ²).
;;; Returns Hamiltonian states (up t q p).

(define (evolve-Lagrangian L local0 dt n-steps)
  (let ((H      (Lagrangian->Hamiltonian L))
        (state0 (lagrangian->hamiltonian-state L local0)))
    (evolve-Hamiltonian H state0 dt n-steps)))

;;; ── Poincaré surface of section ───────────────────────────────────────────

;;; (poincare-section H state0 dt n-steps) → list of (cons q₁ p₁)
;;;
;;; Evolves the system for n-steps and collects phase-space points at
;;; each upward crossing of the section plane q₀ = 0 (i.e. whenever the
;;; first coordinate component crosses zero from below).
;;;
;;; For a 2-DOF system with q = (up x y) and p = (up px py):
;;;   section plane: x = 0 (upward crossing)
;;;   recorded point: (y . py) at the interpolated crossing
;;;
;;; For a 1-DOF system (scalar q, p):
;;;   records (q . p) at each upward zero crossing of q.
;;;
;;; Linear interpolation in t is used to locate the exact crossing.
;;; Returns a list of (cons q1 p1) pairs; use (map car pts) / (map cdr pts)
;;; to separate the two components.
;;;
;;; Typical use — Hénon-Heiles with low energy (regular) orbit:
;;;   (poincare-section H-hh s0 0.005 20000)
;;; yields a set of points lying on smooth invariant curves.
;;; At higher energy (chaotic), points fill the available area.

(define (poincare-section H state0 dt n-steps)
  (let ((dtf (inexact dt)))
    (let loop ((i 0) (state state0) (acc '()))
      (if (>= i n-steps)
          (reverse acc)
          (let* ((next   (stoermer-verlet-step H dtf state))
                 (q-cur  (coordinate state))
                 (q-nxt  (coordinate next))
                 ;; section coordinate: first component
                 (q0-cur (if (tuple? q-cur) (ref q-cur 0) q-cur))
                 (q0-nxt (if (tuple? q-nxt) (ref q-nxt 0) q-nxt))
                 ;; upward zero crossing: q0 goes from negative to non-negative
                 (cross? (and (< q0-cur 0.0) (>= q0-nxt 0.0))))
            (loop (+ i 1) next
                  (if (not cross?)
                      acc
                      ;; linear interpolation fraction to the exact crossing
                      (let* ((frac   (/ (- q0-cur) (- q0-nxt q0-cur)))
                             (p-cur  (momentum state))
                             (p-nxt  (momentum next))
                             ;; interpolated second component (or scalar)
                             (q1     (if (tuple? q-cur)
                                         (+ (ref q-cur 1)
                                            (* frac (- (ref q-nxt 1) (ref q-cur 1))))
                                         (+ q-cur (* frac (- q-nxt q-cur)))))
                             (p1     (if (tuple? p-cur)
                                         (+ (ref p-cur 1)
                                            (* frac (- (ref p-nxt 1) (ref p-cur 1))))
                                         (+ p-cur (* frac (- p-nxt p-cur))))))
                        (cons (cons q1 p1) acc)))))))))

;;; ════════════════════════════════════════════════════════════
;;; § 14  Canonical transformations (SICM Ch 5)
;;;
;;; A canonical transformation (CT) C: (up t q p) → (up t Q P)
;;; preserves Hamilton's equations.  The algebraic condition is that
;;; the 2n×2n phase-space Jacobian M = DC satisfies:
;;;
;;;   Mᵀ J M = J     where J = [[0, Iₙ], [−Iₙ, 0]]
;;;
;;; All Jacobians are computed by central finite differences, so C,
;;; F, and F2 can be arbitrary Scheme functions.
;;; ════════════════════════════════════════════════════════════

;;; ── Matrix helpers (matrices as lists of rows) ────────────────────────────

(define (ct-mat-transpose M)
  (apply map list M))

(define (ct-mat-mul A B)
  (let ((Bt (ct-mat-transpose B)))
    (map (lambda (rA)
           (map (lambda (cB) (apply + (map * rA cB))) Bt))
         A)))

(define (ct-mat-vec-mul M v)
  (map (lambda (row) (apply + (map * row v))) M))

;;; Inverse of a 1×1 or 2×2 matrix.
(define (ct-mat-inv M)
  (cond
    ((= (length M) 1)
     (list (list (/ 1.0 (caar M)))))
    ((= (length M) 2)
     (let* ((a (list-ref (list-ref M 0) 0)) (b (list-ref (list-ref M 0) 1))
            (c (list-ref (list-ref M 1) 0)) (d (list-ref (list-ref M 1) 1))
            (det (- (* a d) (* b c))))
       (if (< (abs det) 1e-14)
           (error "ct-mat-inv: singular matrix")
           (list (list (/ d det) (/ (- b) det))
                 (list (/ (- c) det) (/ a det))))))
    (else (error "ct-mat-inv: only 1×1 and 2×2 supported"))))

;;; Max absolute entry (used as error norm).
(define (ct-mat-max-abs M)
  (apply max (map (lambda (row) (apply max (map abs row))) M)))

;;; ── Phase-space helpers ───────────────────────────────────────────────────

;;; Number of degrees of freedom.
(define (ct-ndof state)
  (let ((q (coordinate state)))
    (if (tuple? q) (dimension q) 1)))

;;; Perturb element j of a list by eps.
(define (ct-list-perturb lst j eps)
  (let lp ((i 0) (v lst) (acc '()))
    (if (null? v) (reverse acc)
        (lp (+ i 1) (cdr v)
            (cons (if (= i j) (+ (car v) eps) (car v)) acc)))))

;;; Flatten (q, p) into a list of 2n numbers: (q₀…q_{n−1} p₀…p_{n−1}).
(define (ct-phase-vec state)
  (let* ((q (coordinate state)) (p (momentum state))
         (n (ct-ndof state)))
    (append (if (tuple? q) (map (lambda (i) (ref q i)) (sv-iota n)) (list q))
            (if (tuple? p) (map (lambda (i) (ref p i)) (sv-iota n)) (list p)))))

;;; Rebuild state from t, n, and a flat phase-space list of 2n numbers.
(define (ct-state-from-vec t n vec)
  (let* ((q-lst (let lp ((i 0) (a '()))
                  (if (= i n) (reverse a) (lp (+ i 1) (cons (list-ref vec i) a)))))
         (p-lst (let lp ((i 0) (a '()))
                  (if (= i n) (reverse a) (lp (+ i 1) (cons (list-ref vec (+ n i)) a)))))
         (q (if (= n 1) (car q-lst) (apply up q-lst)))
         (p (if (= n 1) (car p-lst) (apply up p-lst))))
    (up t q p)))

;;; ── Symplectic matrix and Jacobian ────────────────────────────────────────

(define ct-eps 1e-4)   ; finite-difference step for CT Jacobian
; NOTE: kept larger than sv-eps (1e-7) to suppress amplification of the inner
; fd-grad noise when computing F2->CT Jacobians.  At h=1e-4 the central-difference
; truncation error is O(h²)≈1e-8 — negligible vs. the 1e-4 symplecticity tolerance.

;;; Standard symplectic matrix J for n-DOF (2n × 2n):
;;;   J = [[0ₙ, Iₙ], [−Iₙ, 0ₙ]]
(define (ct-symplectic-J n)
  (let ((dim (* 2 n)))
    (map (lambda (i)
           (map (lambda (j)
                  (cond ((= j (+ i n)) 1.0)
                        ((= i (+ j n)) -1.0)
                        (else 0.0)))
                (sv-iota dim)))
         (sv-iota dim))))

;;; Numerical 2n × 2n Jacobian of C: (up t q p) → (up t Q P).
;;; Returns a list of 2n rows, each a list of 2n values.
(define (ct-jacobian C state)
  (let* ((n   (ct-ndof state))
         (t   (time state))
         (dim (* 2 n))
         (v0  (ct-phase-vec state))
         (cols (map (lambda (j)
                      (let* ((v+ (ct-list-perturb v0 j ct-eps))
                             (v- (ct-list-perturb v0 j (- ct-eps)))
                             (o+ (ct-phase-vec (C (ct-state-from-vec t n v+))))
                             (o- (ct-phase-vec (C (ct-state-from-vec t n v-)))))
                        (map (lambda (a b) (/ (- a b) (* 2.0 ct-eps))) o+ o-)))
                    (sv-iota dim))))
    (ct-mat-transpose cols)))

;;; ── Canonicality tests ────────────────────────────────────────────────────

;;; (symplectic-transform? C state [tol])
;;; Returns #t if the phase-space Jacobian of C at state satisfies Mᵀ J M = J.
(define (symplectic-transform? C state . rest)
  (let* ((tol  (if (null? rest) 1e-4 (car rest)))
         (n    (ct-ndof state))
         (M    (ct-jacobian C state))
         (Mt   (ct-mat-transpose M))
         (J    (ct-symplectic-J n))
         (MtJM (ct-mat-mul Mt (ct-mat-mul J M)))
         (diff (map (lambda (ra rb) (map - ra rb)) MtJM J)))
    (<= (ct-mat-max-abs diff) tol)))

;;; (canonical? C state [tol])
;;; Alias for symplectic-transform? — canonical ↔ symplectic.
(define (canonical? C state . rest)
  (apply symplectic-transform? C state rest))

;;; ── F->C: canonical lift of a coordinate transformation ──────────────────

;;; Given F: (t q) → Q, return the canonical transformation C:
;;;   Q = F(t, q)
;;;   P = (∂F/∂q)⁻ᵀ · p
;;;
;;; Numerically computes the n×n coordinate Jacobian ∂F/∂q by central
;;; differences.  Works for 1-DOF (scalar q) and 2-DOF (2-tuple q).

(define (F->C F)
  (lambda (state)
    (let* ((t    (time state))
           (q    (coordinate state))
           (p    (momentum state))
           (n    (ct-ndof state))
           ;; Q
           (Q    (F t q))
           ;; n×n Jacobian ∂F/∂q: row i = ∂Q_i/∂q_j for j=0..n-1
           (q-lst (if (tuple? q) (map (lambda (i) (ref q i)) (sv-iota n)) (list q)))
           (dF   (map (lambda (i)
                        (map (lambda (j)
                               (let* ((q+ (ct-list-perturb q-lst j ct-eps))
                                      (q- (ct-list-perturb q-lst j (- ct-eps)))
                                      (Q+v (F t (if (= n 1) (car q+) (apply up q+))))
                                      (Q-v (F t (if (= n 1) (car q-) (apply up q-))))
                                      (Qi+ (if (tuple? Q+v) (ref Q+v i) Q+v))
                                      (Qi- (if (tuple? Q-v) (ref Q-v i) Q-v)))
                                 (/ (- Qi+ Qi-) (* 2.0 ct-eps))))
                             (sv-iota n)))
                      (sv-iota n)))
           ;; P = (dF⁻¹)ᵀ · p
           (dFT-inv (ct-mat-transpose (ct-mat-inv dF)))
           (p-lst  (if (tuple? p) (map (lambda (i) (ref p i)) (sv-iota n)) (list p)))
           (P-lst  (ct-mat-vec-mul dFT-inv p-lst))
           (P      (if (= n 1) (car P-lst) (apply up P-lst))))
      (up t Q P))))

;;; ── F2->CT: canonical transformation from a type-2 generating function ───

;;; Given F2: (t q P) → real, the transformation is:
;;;   p = ∂F2/∂q  and  Q = ∂F2/∂P
;;;
;;; To find (Q, P) from (q, p):
;;;   1. Solve  p = ∂F2/∂q(t, q, P)  for P  (Newton iteration, up to 10 steps)
;;;   2. Compute  Q = ∂F2/∂P(t, q, P)
;;;
;;; Converges for well-conditioned F2.  Initial guess P₀ = p.

;;; Row i, column j of the mixed partial ∂(∂F2/∂q_i)/∂P_j (n×n matrix).
(define (ct-f2-mixed-jac F2 t q P n)
  (let* ((P-lst (if (tuple? P) (map (lambda (i) (ref P i)) (sv-iota n)) (list P))))
    (map (lambda (i)
           (map (lambda (j)
                  (let* ((P+v (let ((v (ct-list-perturb P-lst j ct-eps)))
                                (if (= n 1) (car v) (apply up v))))
                         (P-v (let ((v (ct-list-perturb P-lst j (- ct-eps))))
                                (if (= n 1) (car v) (apply up v))))
                         (g+  (fd-grad (lambda (qq) (F2 t qq P+v)) q))
                         (g-  (fd-grad (lambda (qq) (F2 t qq P-v)) q))
                         (g+i (if (tuple? g+) (ref g+ i) g+))
                         (g-i (if (tuple? g-) (ref g- i) g-)))
                    (/ (- g+i g-i) (* 2.0 ct-eps))))
                (sv-iota n)))
         (sv-iota n))))

(define (F2->CT F2)
  (lambda (state)
    (let* ((t  (time state))
           (q  (coordinate state))
           (p  (momentum state))
           (n  (ct-ndof state))
           ;; Newton: solve ∂F2/∂q(t, q, P) = p for P
           (P  (let loop ((P p) (iter 0))
                 (let* ((grad-q   (fd-grad (lambda (qq) (F2 t qq P)) q))
                        (residual (sv- grad-q p))
                        (r-norm   (if (tuple? residual)
                                      (sqrt (apply + (map (lambda (i) (* (ref residual i) (ref residual i)))
                                                          (sv-iota n))))
                                      (abs residual))))
                   (cond
                     ((< r-norm 1e-12) P)
                     ((>= iter 10) P)
                     (else
                      (let* ((J     (ct-f2-mixed-jac F2 t q P n))
                             (J-inv (ct-mat-inv J))
                             (r-lst (if (tuple? residual)
                                        (map (lambda (i) (ref residual i)) (sv-iota n))
                                        (list residual)))
                             (dP-lst (ct-mat-vec-mul J-inv (map - r-lst)))
                             (dP    (if (= n 1) (car dP-lst) (apply up dP-lst))))
                        (loop (sv+ P dP) (+ iter 1))))))))
           ;; Q = ∂F2/∂P
           (Q  (fd-grad (lambda (PP) (F2 t q PP)) P)))
      (up t Q P))))

;;; ── Standard canonical transformations ───────────────────────────────────

;;; 2D Cartesian → polar point transformation for use with F->C.
;;; F(t, q) where q = (up x y) → (up r θ).
(define (rectangular->polar t q)
  (let ((x (ref q 0)) (y (ref q 1)))
    (up (sqrt (+ (* x x) (* y y)))
        (atan y x))))

;;; (action-angle omega) → canonical transformation for the 1-DOF harmonic
;;; oscillator H = ½(p² + ω²q²).
;;;
;;;   I = (p² + ω²q²) / (2ω)     [action = H/ω]
;;;   θ = atan2(−p, ωq)           [angle]
;;;
;;; In action-angle coordinates H = ω·I (integrable, no θ dependence).
;;; The angle evolves uniformly: θ(t) = ω·t + θ₀.
(define (action-angle omega)
  (lambda (state)
    (let* ((q (coordinate state))
           (p (momentum state))
           (I (/ (+ (* p p) (* omega omega q q)) (* 2.0 omega)))
           (theta (atan (- p) (* omega q))))
      (up (time state) theta I))))


;;; ════════════════════════════════════════════════════════════
;;; § 15  Lie transforms and first-order perturbation theory
;;;        (SICM Ch 6–7)
;;;
;;; Public: pb  Lie-derivative  Lie-transform
;;;         secular-average  secular-perturbation
;;; ════════════════════════════════════════════════════════════

;;; Finite-difference step for Lie-derivative Poisson bracket.
;;; Larger than sv-eps (1e-7) to keep k=2 nested-difference errors
;;; below ~1e-4.  Optimal for double precision: sqrt(eps_mach*|H|) ≈ 1e-5.
(define lie-eps 1e-5)

;;; Dot product of two scalars or up-tuples of the same shape.
(define (pb-dot a b)
  (if (tuple? a)
      (let lp ((i 0) (s 0.0))
        (if (= i (dimension a)) s
            (lp (+ i 1) (+ s (* (ref a i) (ref b i))))))
      (* a b)))

;;; ∂f/∂q at state using lie-eps (scalar or up-tuple result).
(define (pb-grad-q f state)
  (let* ((t (time state)) (q (coordinate state)) (p (momentum state)))
    (if (tuple? q)
        (apply up
          (map (lambda (i)
                 (/ (- (f (up t (sv-perturb q i lie-eps) p))
                       (f (up t (sv-perturb q i (- lie-eps)) p)))
                    (* 2.0 lie-eps)))
               (sv-iota (dimension q))))
        (/ (- (f (up t (+ q lie-eps) p))
              (f (up t (- q lie-eps) p)))
           (* 2.0 lie-eps)))))

;;; ∂f/∂p at state using lie-eps (scalar or up-tuple result).
(define (pb-grad-p f state)
  (let* ((t (time state)) (q (coordinate state)) (p (momentum state)))
    (if (tuple? p)
        (apply up
          (map (lambda (i)
                 (/ (- (f (up t q (sv-perturb p i lie-eps)))
                       (f (up t q (sv-perturb p i (- lie-eps)))))
                    (* 2.0 lie-eps)))
               (sv-iota (dimension p))))
        (/ (- (f (up t q (+ p lie-eps)))
              (f (up t q (- p lie-eps))))
           (* 2.0 lie-eps)))))

;;; Numerical Poisson bracket {f, g}(state) = Σᵢ (∂f/∂qᵢ ∂g/∂pᵢ − ∂f/∂pᵢ ∂g/∂qᵢ).
;;; f, g: (up t q p) → real.  Uses lie-eps central differences.
;;; Accurate for single applications; nested use (k≥3) accumulates noise.
(define (pb f g)
  (lambda (state)
    (- (pb-dot (pb-grad-q f state) (pb-grad-p g state))
       (pb-dot (pb-grad-p f state) (pb-grad-q g state)))))

;;; Lie derivative operator along the Hamiltonian vector field of W.
;;; Returns an operator: given f, produces the function {f, W}.
;;; ((Lie-derivative W) f)(state) = {f, W}(state)
(define (Lie-derivative W)
  (lambda (f)
    (pb f W)))

;;; Lie series exp(ε L_W) applied to f, evaluated at state.
;;; Computes Σ_{k=0}^{n-1} (ε^k / k!) (L_W^k f)(state).
;;; Default n-terms = 8.
;;;
;;; Accuracy notes:
;;;   k=0,1: error O(lie-eps²) ≈ 1e-10 — highly accurate.
;;;   k=2:   nested FD error O(lie-eps^(-1) · eps_machine · |f|) ≈ 1e-4.
;;;   k≥3:   errors amplify rapidly; use small ε or truncate early.
;;; Best practice: choose W so the series terminates at k≤2, or use
;;; small enough ε that higher terms are negligible.
(define (Lie-transform W epsilon . rest)
  (let ((n-terms (if (null? rest) 8 (car rest))))
    (lambda (f)
      (lambda (state)
        (let loop ((Lkf f) (k 0) (coeff 1.0) (acc 0.0))
          (if (>= k n-terms)
              acc
              (loop ((Lie-derivative W) Lkf)
                    (+ k 1)
                    (* coeff (/ (inexact epsilon) (+ k 1)))
                    (+ acc (* coeff (Lkf state))))))))))

;;; ── Secular averaging ─────────────────────────────────────────────────────

;;; Angle-average <f>(I) = (1/(2π)) ∫₀^{2π} f(up t θ I) dθ.
;;; f-aa: function of action-angle state (up t theta I).
;;; Uses the trapezoidal rule with n-pts quadrature points (default 64).
;;; Exact for any Fourier mode cos(nθ) or sin(nθ) with n < n-pts/2
;;; (exploits the DFT cancellation property of equispaced nodes).
(define (secular-average f-aa . rest)
  (let ((n-pts (if (null? rest) 64 (car rest))))
    (lambda (state)
      (let* ((I   (momentum state))
             (t   (time state))
             (dth (/ (* 2.0 (acos -1.0)) n-pts)))
        (/ (let lp ((j 0) (s 0.0))
             (if (= j n-pts) s
                 (lp (+ j 1)
                     (+ s (f-aa (up t (* j dth) I))))))
           n-pts)))))

;;; First-order secular perturbation.
;;; For H = H0(I) + epsilon * H1(theta, I) in action-angle coordinates,
;;; returns the angle-averaged Hamiltonian H0(I) + epsilon * <H1>(I).
;;;
;;; H0, H1: (up t theta I) → real.
;;; Returns a function of the same form.
(define (secular-perturbation H0 H1 epsilon . rest)
  (let ((avg-H1 (apply secular-average H1 rest)))
    (lambda (state)
      (+ (H0 state) (* (inexact epsilon) (avg-H1 state))))))

;;; ════════════════════════════════════════════════════════════
;;; § 16  Controller synthesis (linearise, LQR) — SICM Ch 6 ext.
;;; ════════════════════════════════════════════════════════════
;;;
;;; Exported:
;;;   lqr-mat-*   — n×n matrix library (lists of rows)
;;;   linearise   — numerical A matrix of Hamiltonian vector field
;;;   lqr-continuous  — continuous-time CARE → gain K
;;;   lqr         — discrete-time DARE → gain K
;;;   make-controller — feedback controller (simulation)

;;; ── n×n matrix library ───────────────────────────────────────────────────

;;; Matrices are represented as lists of rows (each row a list of flonums).

(define (lqr-mat-rows M) (length M))
(define (lqr-mat-cols M) (length (car M)))
(define (lqr-mat-ref  M i j) (list-ref (list-ref M i) j))

(define (lqr-mat-zero n m)
  (map (lambda (_) (map (lambda (_) 0.0) (sv-iota m))) (sv-iota n)))

(define (lqr-mat-eye n)
  (map (lambda (i)
         (map (lambda (j) (if (= i j) 1.0 0.0)) (sv-iota n)))
       (sv-iota n)))

(define (lqr-mat-add A B)
  (map (lambda (rA rB) (map + rA rB)) A B))

(define (lqr-mat-sub A B)
  (map (lambda (rA rB) (map - rA rB)) A B))

(define (lqr-mat-scale c M)
  (map (lambda (row) (map (lambda (x) (* c x)) row)) M))

(define (lqr-mat-transpose M)
  (apply map list M))

(define (lqr-mat-mul A B)
  (let ((Bt (lqr-mat-transpose B)))
    (map (lambda (rA)
           (map (lambda (cB) (apply + (map * rA cB))) Bt))
         A)))

(define (lqr-mat-norm M)
  (apply max (map (lambda (row) (apply max (map abs row))) M)))

;;; Gaussian elimination with partial pivoting: solve Ax=b.
;;; A: n×n (list of rows), b: list of n values.
;;; Returns list of n values x.
(define (lqr-gauss-solve A b)
  (let* ((n (length A))
         ;; Build augmented matrix [A|b]
         (aug (map (lambda (row bi) (append row (list bi)))
                   A b)))
    ;; Forward elimination with partial pivot
    (let loop-col ((col 0) (aug aug))
      (if (= col n)
          ;; Back-substitution
          (let back ((k (- n 1)) (x (make-list n 0.0)))
            (if (< k 0)
                x
                (let* ((row  (list-ref aug k))
                       (diag (list-ref row k))
                       (rhs  (list-ref row n))
                       (sum  (apply + (map (lambda (j)
                                             (* (list-ref row j) (list-ref x j)))
                                           (filter (lambda (j) (> j k)) (sv-iota n)))))
                       (xk   (/ (- rhs sum) diag)))
                  (back (- k 1) (lqr-list-set x k xk)))))
          ;; Find pivot
          (let* ((pivot-row
                   (let find ((i col) (best col) (best-val (abs (list-ref (list-ref aug col) col))))
                     (if (>= i n) best
                         (let ((v (abs (list-ref (list-ref aug i) col))))
                           (if (> v best-val) (find (+ i 1) i v)
                               (find (+ i 1) best best-val))))))
                 ;; Swap pivot row with current row
                 (aug1 (lqr-swap-rows aug col pivot-row))
                 ;; Eliminate column
                 (piv  (list-ref (list-ref aug1 col) col))
                 (aug2
                   (map (lambda (i)
                          (if (= i col)
                              (list-ref aug1 i)
                              (let* ((row-i (list-ref aug1 i))
                                     (factor (/ (list-ref row-i col) piv)))
                                (map (lambda (j)
                                       (- (list-ref row-i j)
                                          (* factor (list-ref (list-ref aug1 col) j))))
                                     (sv-iota (+ n 1))))))
                        (sv-iota n))))
            (loop-col (+ col 1) aug2))))))

(define (lqr-list-set lst i val)
  (map (lambda (j x) (if (= j i) val x)) (sv-iota (length lst)) lst))

(define (lqr-swap-rows M i j)
  (map (lambda (k) (cond ((= k i) (list-ref M j))
                         ((= k j) (list-ref M i))
                         (else    (list-ref M k))))
       (sv-iota (length M))))

;;; n×n matrix inverse via Gauss-Jordan on [A|I].
(define (lqr-mat-inv A)
  (let* ((n (length A))
         (I (lqr-mat-eye n))
         ;; Solve each column of I
         (cols (map (lambda (j)
                      (lqr-gauss-solve A (map (lambda (row) (list-ref row j)) I)))
                    (sv-iota n))))
    ;; cols[j] is the j-th column of A^{-1}; transpose to get rows
    (lqr-mat-transpose cols)))

;;; Kronecker product A ⊗ B  (result is (n_A·n_B) × (m_A·m_B)).
;;; For row rA of A and row rB of B, the result row is
;;;   (a0*rB ++ a1*rB ++ ... ++ a(mA-1)*rB)
;;; Outer map over rows of A; inner map over rows of B.
(define (lqr-mat-kron A B)
  (apply append
    (map (lambda (rA)
           (map (lambda (rB)
                  (apply append
                    (map (lambda (aij)
                           (map (lambda (bij) (* (inexact aij) (inexact bij))) rB))
                         rA)))
                B))
         A)))

;;; Vectorise matrix column-major: vec(M) = [M[:,0]; M[:,1]; ...]
(define (lqr-vec M)
  (let ((Mt (lqr-mat-transpose M)))
    (apply append Mt)))

;;; Unvectorise: list of n² values → n×n matrix (column-major).
(define (lqr-unvec v n)
  (lqr-mat-transpose
    (map (lambda (j) (map (lambda (i) (list-ref v (+ (* j n) i))) (sv-iota n)))
         (sv-iota n))))

;;; ── Lyapunov solver ─────────────────────────────────────────────────────
;;;
;;; Solve  A'X + XA = -M  via Kronecker product linearisation.
;;; Returns X (n×n symmetric matrix).
;;; Algorithm: vec(A'X + XA) = (I⊗A' + A'⊗I) vec(X) = vec(-M)
;;; using the identity vec(AXB) = (B'⊗A) vec(X) [column-major]:
;;;   vec(A'·X·I) = (I ⊗ A') vec(X)   [B=I, A=A']
;;;   vec(I·X·A ) = (A' ⊗ I) vec(X)   [A=I, B=A]
;;; Coefficient matrix = I⊗A' + A'⊗I  (both terms use At = A').
(define (lyapunov-solve A M)
  (let* ((n   (length A))
         (At  (lqr-mat-transpose A))
         (I   (lqr-mat-eye n))
         ;; Coefficient matrix for vec(X): I⊗A' + A'⊗I
         (K   (lqr-mat-add (lqr-mat-kron I  At)
                           (lqr-mat-kron At I)))
         (rhs (map (lambda (x) (- x)) (lqr-vec M)))
         (x   (lqr-gauss-solve K rhs)))
    (lqr-unvec x n)))

;;; ── Continuous-time LQR (CARE solver via policy iteration) ───────────────
;;;
;;; Given ẋ = Ax + Bu, minimise J = ∫(x'Qx + u'Ru) dt.
;;; Solves  A'P + PA − PBR⁻¹B'P + Q = 0  (CARE).
;;; Returns K (m×n gain matrix) such that u = −K·x is optimal.
;;;
;;; Algorithm: policy iteration (Newton on CARE).
;;;   1. Start with K₀ = 0 (or a stabilising gain if A is unstable)
;;;   2. Solve Lyapunov:  (A−BK)' P + P(A−BK) = −(Q + K'RK)
;;;   3. Update K = R⁻¹B'P
;;;   4. Repeat until convergence.
;;;
;;; For policy iteration to converge from K₀=0, A must be stable.
;;; For unstable A (e.g. inverted pendulum), pass a stabilising initial K
;;; as the optional fifth argument (list of rows, m×n).
;;; Signature: (lqr-continuous A B Q R [max-iter [tol [K0]]])
(define (lqr-continuous A B Q R . opts)
  (let* ((n     (length A))
         (m     (lqr-mat-cols B))
         (Rinv  (lqr-mat-inv R))
         (Bt    (lqr-mat-transpose B))
         (max-it (if (null? opts) 100 (car opts)))
         (tol    (if (or (null? opts) (null? (cdr opts))) 1e-10
                     (cadr opts)))
         ;; Optional initial K (3rd vararg): use zero-matrix if not supplied.
         (K0  (if (or (null? opts) (null? (cdr opts)) (null? (cddr opts)))
                  (lqr-mat-zero m n)
                  (caddr opts))))
    (let iter ((K K0) (it 0))
      (let* (;; Closed-loop A
             (Acl  (lqr-mat-sub A (lqr-mat-mul B K)))
             ;; RHS of Lyapunov: -(Q + K'RK)
             (KtRK (lqr-mat-mul (lqr-mat-transpose K) (lqr-mat-mul R K)))
             (M    (lqr-mat-add Q KtRK))
             ;; Solve Lyapunov: Acl' P + P Acl = -M
             (P    (lyapunov-solve Acl M))
             ;; New gain
             (Knew (lqr-mat-mul Rinv (lqr-mat-mul Bt P)))
             (err  (lqr-mat-norm (lqr-mat-sub Knew K))))
        (if (or (< err tol) (>= it max-it))
            Knew
            (iter Knew (+ it 1)))))))

;;; ── Discrete-time LQR (DARE solver via policy iteration) ─────────────────
;;;
;;; Given x_{k+1} = Ax_k + Bu_k, minimise J = Σ (x'Qx + u'Ru).
;;; Solves  P = Q + A'PA − A'PB(R + B'PB)⁻¹B'PA  (DARE).
;;; Returns K (m×n gain matrix) such that u = −K·x is optimal.
(define (lqr A B Q R . opts)
  (let* ((n     (length A))
         (m     (lqr-mat-cols B))
         (At    (lqr-mat-transpose A))
         (Bt    (lqr-mat-transpose B))
         (max-it (if (null? opts) 200 (car opts)))
         (tol    (if (or (null? opts) (null? (cdr opts))) 1e-10
                     (cadr opts))))
    ;; Value iteration: P_{k+1} = Q + A'P_k A − A'P_k B (R+B'P_k B)⁻¹ B'P_k A
    (let iter ((P Q) (it 0))
      (let* ((AtP  (lqr-mat-mul At P))
             (AtPA (lqr-mat-mul AtP A))
             (AtPB (lqr-mat-mul AtP B))
             (BtPB (lqr-mat-mul Bt (lqr-mat-mul P B)))
             (S    (lqr-mat-add R BtPB))
             (Sinv (lqr-mat-inv S))
             (Pnew (lqr-mat-sub (lqr-mat-add Q AtPA)
                                (lqr-mat-mul AtPB (lqr-mat-mul Sinv (lqr-mat-mul Bt (lqr-mat-mul P A))))))
             (err  (lqr-mat-norm (lqr-mat-sub Pnew P))))
        (if (or (< err tol) (>= it max-it))
            ;; Extract gain K = (R + B'PB)⁻¹ B'PA
            (lqr-mat-mul Sinv (lqr-mat-mul Bt (lqr-mat-mul Pnew A)))
            (iter Pnew (+ it 1)))))))

;;; ── Linearise Hamiltonian vector field ───────────────────────────────────
;;;
;;; For H(t,q,p), the equations of motion are:
;;;   q̇ = ∂H/∂p,    ṗ = −∂H/∂q
;;;
;;; linearise computes the 2n×2n Jacobian A of the vector field f=(q̇,ṗ)
;;; at the equilibrium state (up t0 q0 p0) via central finite differences.
;;;
;;; Returns A as a list of rows (2n×2n for n-DOF).
(define linearise-eps 1e-5)

(define (linearise H state)
  (let* ((t  (time state))
         (q0 (coordinate state))
         (p0 (momentum state))
         ;; Flatten state to vector [q; p] for Jacobian
         (q-scalar? (not (tuple? q0)))
         (n  (if q-scalar? 1 (dimension q0)))
         (x0 (if q-scalar?
                 (list q0 p0)
                 (append (map (lambda (i) (ref q0 i)) (sv-iota n))
                         (map (lambda (i) (ref p0 i)) (sv-iota n)))))
         (dim2 (* 2 n))
         ;; Reconstruct state from flat vector x
         (make-state
           (lambda (x)
             (if q-scalar?
                 (up t (car x) (cadr x))
                 (up t
                     (apply up (list-head x n))
                     (apply up (list-tail x n))))))
         ;; RHS of equations of motion evaluated at x: returns list [q̇; ṗ]
         (rhs
           (lambda (x)
             (let* ((s    (make-state x))
                    (qdot (sv-grad-p H s))    ; ∂H/∂p
                    (pdot (lqr-neg (sv-grad-q H s)))) ; -∂H/∂q
               (if q-scalar?
                   (list qdot pdot)
                   (append (map (lambda (i) (ref qdot i)) (sv-iota n))
                           (map (lambda (i) (- (ref pdot i)) ) (sv-iota n))))))))
    ;; Build Jacobian column by column via central differences
    (lqr-mat-transpose
      (map (lambda (k)
             (let* ((xp (lqr-list-perturb x0 k    linearise-eps))
                    (xm (lqr-list-perturb x0 k (- linearise-eps)))
                    (fp (rhs xp))
                    (fm (rhs xm)))
               (map (lambda (fp_i fm_i) (/ (- fp_i fm_i) (* 2.0 linearise-eps)))
                    fp fm)))
           (sv-iota dim2)))))

;;; Helpers for linearise:
(define (sv-grad-q H state)
  (let* ((t  (time state)) (q (coordinate state)) (p (momentum state)))
    (if (tuple? q)
        (apply up
          (map (lambda (i)
                 (/ (- (H (up t (sv-perturb q i linearise-eps) p))
                       (H (up t (sv-perturb q i (- linearise-eps)) p)))
                    (* 2.0 linearise-eps)))
               (sv-iota (dimension q))))
        (/ (- (H (up t (+ q linearise-eps) p))
              (H (up t (- q linearise-eps) p)))
           (* 2.0 linearise-eps)))))

(define (sv-grad-p H state)
  (let* ((t  (time state)) (q (coordinate state)) (p (momentum state)))
    (if (tuple? p)
        (apply up
          (map (lambda (i)
                 (/ (- (H (up t q (sv-perturb p i linearise-eps)))
                       (H (up t q (sv-perturb p i (- linearise-eps)))))
                    (* 2.0 linearise-eps)))
               (sv-iota (dimension p))))
        (/ (- (H (up t q (+ p linearise-eps)))
              (H (up t q (- p linearise-eps))))
           (* 2.0 linearise-eps)))))

(define (lqr-list-perturb lst k h)
  (map (lambda (j x) (if (= j k) (+ x h) x)) (sv-iota (length lst)) lst))

(define (lqr-neg x)
  (if (tuple? x)
      (apply up (map (lambda (i) (- (ref x i))) (sv-iota (dimension x))))
      (- x)))

;;; ── make-controller ────────────────────────────────────────────────────
;;;
;;; Builds a feedback controller that simulates the closed-loop system.
;;; Returns a thunk; call it repeatedly to step the simulation by dt.
;;;
;;; H:  Hamiltonian (as for evolve-Hamiltonian)
;;; K:  gain matrix (m×n, list of rows)  u = −K·(x − x_eq)
;;; x0: equilibrium state (up t q p) — target operating point
;;; dt: time step
;;; B-fn: (up t q p) → list of m-vectors (input coupling in state space)
;;;       Each column of B maps a scalar input u_i to q̈ forcing.
;;;       For simplicity, B-fn can return a single list (scalar input).
;;;
;;; Returns a controller record (alist) with fields:
;;;   state    — current state (up t q p)
;;;   step!    — advance by dt, returns (list state control-input)
;;;   reset!   — reset to initial state s_init
(define (make-controller H K x-eq dt . opts)
  (let* ((n-steps (if (null? opts) 1000 (car opts)))
         ;; Internal mutable state held in a box (vector of 1)
         (state-box (vector x-eq))
         ;; Flatten equilibrium to vector
         (q-eq (coordinate x-eq))
         (p-eq (momentum    x-eq))
         (q-scalar? (not (tuple? q-eq)))
         (n  (if q-scalar? 1 (dimension q-eq)))
         (x-eq-vec
           (if q-scalar?
               (list (inexact q-eq) (inexact p-eq))
               (append (map (lambda (i) (inexact (ref q-eq i))) (sv-iota n))
                       (map (lambda (i) (inexact (ref p-eq i))) (sv-iota n)))))
         ;; Extract state vector from (up t q p)
         (state->vec
           (lambda (s)
             (let ((q (coordinate s)) (p (momentum s)))
               (if q-scalar?
                   (list (inexact q) (inexact p))
                   (append (map (lambda (i) (inexact (ref q i))) (sv-iota n))
                           (map (lambda (i) (inexact (ref p i))) (sv-iota n)))))))
         ;; Compute control: u = -K(x - x_eq)
         (compute-u
           (lambda (s)
             (let* ((x   (state->vec s))
                    (dx  (map - x x-eq-vec))
                    ;; u = -K dx  (K is m×2n, result is list of m scalars)
                    (u   (map (lambda (k-row)
                                (- (apply + (map * k-row dx))))
                              K)))
               u)))
         ;; Controlled Hamiltonian (with u applied as an additive momentum force)
         ;; For simplicity: single-input, force applied to p directly.
         (H-ctrl
           (lambda (s)
             ;; Return bare H value; control is injected in the evolution below
             (H s))))
    ;; The controller object
    (define (step!)
      (let* ((s   (vector-ref state-box 0))
             (u   (compute-u s))
             ;; Evolve one step using standard Störmer-Verlet with forcing
             ;; For this simulation, inject control as p ← p + u[0]*dt
             ;; (first-order forcing; adequate for small dt)
             (q   (coordinate s))
             (p   (momentum s))
             (t   (time s))
             ;; Half-step p update with control forcing
             (u0  (if (null? u) 0.0 (inexact (car u))))
             (p-h (if q-scalar?
                      (+ p (* 0.5 dt u0)
                         (* -0.5 dt (sv-grad-q H s)))
                      ;; multi-DOF: not implemented here — scalar only
                      p))
             ;; Full-step q
             (q-1 (if q-scalar?
                      (+ q (* dt p-h))
                      q))
             (s-h  (up (+ t (* 0.5 dt)) q-1 p-h))
             ;; Half-step p completion
             (p-1  (if q-scalar?
                       (+ p-h (* 0.5 dt u0)
                          (* -0.5 dt (sv-grad-q H s-h)))
                       p-h))
             (s1   (up (+ t dt) q-1 p-1)))
        (vector-set! state-box 0 s1)
        (list s1 u)))
    (define (reset! s-init)
      (vector-set! state-box 0 s-init))
    (define (current-state)
      (vector-ref state-box 0))
    (list (cons 'step!         step!)
          (cons 'reset!        reset!)
          (cons 'current-state current-state)
          (cons 'compute-u     compute-u))))

  )) ;; end begin, define-library

