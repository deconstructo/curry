;;; sicm_tests.scm — (curry sicm) module: SICM mechanics interface

(import (scheme base))
(import (scheme inexact))
(import (curry sicm))
(import (curry ode))

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

(define t (sym-var 't))
(define m (sym-var 'm))
(define k (sym-var 'k))
(define g (sym-var 'g))

;;; ════════════════════════════════════════════════════════════
;;; § 1  literal-function and D
;;; ════════════════════════════════════════════════════════════

(define q (literal-function 'q))

(check-infix "q(t)" (q t) "q(t)")
(check-infix "Dq(t)" ((D q) t) "q_t(t)")
(check-infix "D2q(t)" ((D (D q)) t) "q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 2  Gamma — path functor
;;; ════════════════════════════════════════════════════════════

(define local ((Gamma q) t))

(check "Gamma dimension"   (dimension local) 3)
(check-infix "time slot"   (time local)       "t")
(check-infix "coord slot"  (coordinate local) "q(t)")
(check-infix "veloc slot"  (velocity local)   "q_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 3  Lagrange-equations — free particle
;;;       L = ½ m qdot²  →  EOM: m·q'' = 0
;;; ════════════════════════════════════════════════════════════

(define eom-free ((Lagrange-equations (L-free-particle m)) q))

;;; Residual is -(m·q'') = 0
(check-infix "free EOM" (eom-free t) "-(m * q_t_t(t))")

;;; ════════════════════════════════════════════════════════════
;;; § 4  Lagrange-equations — harmonic oscillator
;;;       L = ½mqdot² − ½kq²  →  EOM: mq'' + kq = 0
;;; ════════════════════════════════════════════════════════════

(define eom-ho ((Lagrange-equations (L-harmonic m k)) q))

;;; Residual: -(k·q(t)) - m·q''(t) = 0
(check-infix "harmonic EOM" (eom-ho t) "-(k * q(t)) - m * q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 5  Lagrange-equations — uniform gravity
;;;       L = ½mqdot² − mgq  →  EOM: mq'' + mg = 0
;;; ════════════════════════════════════════════════════════════

(define eom-grav ((Lagrange-equations (L-uniform-acceleration m g)) q))

(check-infix "gravity EOM" (eom-grav t) "-(g * m) - m * q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 6  Energy (Legendre transform)
;;; ════════════════════════════════════════════════════════════

;;; Harmonic oscillator: E = ½m·qdot² + ½k·q²
(define E-ho ((Lagrangian->energy (L-harmonic m k)) local))
(check-infix "HO energy" (simplify (expand E-ho)) "1/2 * m * q_t(t)^2 + 1/2 * k * q(t)^2")

;;; Free particle: E = ½m·qdot²
(define E-free ((Lagrangian->energy (L-free-particle m)) local))
(check-infix "free energy" (simplify (expand E-free)) "1/2 * m * q_t(t)^2")

;;; ════════════════════════════════════════════════════════════
;;; § 7  compose and square
;;; ════════════════════════════════════════════════════════════

(check "compose f g" ((compose (lambda (x) (* x x)) (lambda (x) (+ x 1))) 3) 16)
(check "compose 3-way" ((compose car cdr cdr) '(1 2 3)) 3)
(check "square scalar" (square 5) 25)
(check "square up" (square (up 3 4)) 25)
(check "square up-3" (square (up 1 2 2)) 9)

;;; ════════════════════════════════════════════════════════════
;;; § 8  Gamma-bar — higher-order local tuple
;;; ════════════════════════════════════════════════════════════

(define local3 ((Gamma-bar q 3) t))
(check "Gamma-bar dim" (dimension local3) 5)     ; t + 3+1 derivs
(check-infix "Gamma-bar accel" (acceleration local3) "q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 9  Numerical verification
;;; ════════════════════════════════════════════════════════════

;;; ∂L/∂qdot for free particle with m=2 at qdot=3: expect 6
(define (L-2 local) (* 2 (square (velocity local))))
(check-num "dL/dqdot numeric" (((partial 2) L-2) (up 0.0 0.0 3.0)) 12.0 1e-12)

;;; Harmonic EOM at concrete local — just verify it runs without error
(define lo-num (up 0.0 1.0 0.0))
(define f-local ((Euler-Lagrange-operator (L-harmonic 1 1)) lo-num))
(check-num "EL-operator numeric HO" (inexact f-local) -1.0 1e-12)

;;; ════════════════════════════════════════════════════════════
;;; § 10  literal-function* — multi-argument symbolic functions
;;; ════════════════════════════════════════════════════════════

(define x (sym-var 'x))
(define y (sym-var 'y))
(define f2 (literal-function* 'f 2))

;;; Applied to two sym-vars it produces a symbolic expression
(check-infix "literal-function* 2-arg" (f2 x y) "f(x, y)")

;;; D differentiates a single-var literal-function*;
;;; subscript comes from the internal argument variable name (_arg0)
(define h (literal-function* 'h 1))
(check-infix "literal-function* 1-arg at t" (h t) "h(t)")
(check-infix "D of literal-function* 1-arg" ((D h) t) "h__arg0(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 11  square with down tuples
;;; ════════════════════════════════════════════════════════════

(check "square down-2" (square (down 3 4)) 25)
(check "square down-3" (square (down 1 2 2)) 9)

;;; ════════════════════════════════════════════════════════════
;;; § 12  Lagrangian->V and Lagrangian->T
;;;        For L-harmonic: V = ½kq², T = ½m·qdot²
;;; ════════════════════════════════════════════════════════════

(define local-ho ((Gamma q) t))

;;; V at the local tuple — should equal ½kq²
(define V-ho ((Lagrangian->V (L-harmonic m k)) local-ho))
(check-infix "Lagrangian->V HO" (simplify (expand V-ho))
             "1/2 * k * q(t)^2")

;;; T at the local tuple — should equal ½m·qdot²
(define T-ho ((Lagrangian->T (L-harmonic m k)) local-ho))
(check-infix "Lagrangian->T HO" (simplify (expand T-ho))
             "1/2 * m * q_t(t)^2")

;;; V of free particle = 0
(define V-free ((Lagrangian->V (L-free-particle m)) local-ho))
(check-infix "Lagrangian->V free" (simplify V-free) "0")

;;; T of free particle = ½m·qdot²
(define T-free ((Lagrangian->T (L-free-particle m)) local-ho))
(check-infix "Lagrangian->T free" (simplify (expand T-free))
             "1/2 * m * q_t(t)^2")

;;; ════════════════════════════════════════════════════════════
;;; § 13  make-Lagrangian
;;;        Build a custom T−V and verify Lagrange equations
;;; ════════════════════════════════════════════════════════════

;;; Simple pendulum (small angle): T = ½ml²qdot², V = ½mgl²q²  (linearised)
;;; EOM: ml²q'' + mgl²q = 0  →  q'' + g·q = 0
(define l (sym-var 'l))
(define (T-pendulum local)
  (* 1/2 m (square l) (square (velocity local))))
(define (V-pendulum q-coord)
  (* 1/2 m g (square l) (square q-coord)))

(define L-pendulum (make-Lagrangian T-pendulum
                                    (lambda (local)
                                      (V-pendulum (coordinate local)))))
(define eom-pendulum ((Lagrange-equations L-pendulum) q))
(check-infix "pendulum EOM"
             (eom-pendulum t)
             "-(g * l^2 * m * q(t)) - l^2 * m * q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 14  Euler-Lagrange-operator — additional Lagrangians
;;; ════════════════════════════════════════════════════════════

;;; Free particle at (t=0, q=1, qdot=0): ∂L/∂q=0, ∂L/∂qdot=0 → residual=0
(define lo-free (up 0.0 1.0 0.0))
(check-num "EL-operator free particle" ((Euler-Lagrange-operator (L-free-particle 2.0)) lo-free) 0.0 1e-12)

;;; Gravity at (t=0, q=0, qdot=0): ∂L/∂q − ∂L/∂qdot = −mg − 0 = −9.8
(check-num "EL-operator gravity" ((Euler-Lagrange-operator (L-uniform-acceleration 1.0 9.8)) (up 0.0 0.0 0.0)) -9.8 1e-12)

;;; ════════════════════════════════════════════════════════════
;;; § 15  Numeric Lagrange-equations
;;; ════════════════════════════════════════════════════════════

;;; Harmonic oscillator with m=1, k=4: EOM = -4q(t) - q''(t)
;;; At a concrete numeric path q(t)=sin(2t): q''=-4sin(2t), EOM ≡ 0
;;; We can't fully evaluate without a concrete path, but we can check
;;; that Lagrange-equations returns a callable for numeric Lagrangians.
(define (L-ho-numeric local)
  (- (* 0.5 (square (velocity local)))
     (* 2.0 (square (coordinate local)))))

(define (q-numeric t) (sin (* 2.0 t)))

;;; The EL residual for q(t)=sin(2t) under L=½qdot²−2q² should be ≈0
;;; because d²/dt²(sin(2t)) = -4sin(2t) and ∂L/∂q = -4q
;;; so EOM: -4q - q'' = -4sin - (-4sin) = 0
(define eom-numeric ((Lagrange-equations L-ho-numeric) q-numeric))
(check-num "numeric Lagrange-equations on exact solution" (eom-numeric 1.0) 0.0 1e-10)
(check-num "numeric Lagrange-equations at t=0.5" (eom-numeric 0.5) 0.0 1e-10)

;;; ════════════════════════════════════════════════════════════
;;; § 16  Multi-DOF: 2D harmonic oscillator Lagrange-equations
;;; ════════════════════════════════════════════════════════════

(define x-fn (literal-function 'x))
(define y-fn (literal-function 'y))
(define q-path-2d (lambda (tau) (up (x-fn tau) (y-fn tau))))

(define eom-2d-ho ((Lagrange-equations (L-harmonic-nd m k)) q-path-2d))
(define result-2d (eom-2d-ho t))

(check "2D HO EOM is down-tuple"  (down? result-2d) #t)
(check "2D HO EOM dimension"      (dimension result-2d) 2)
(check-infix "2D HO EOM x-component" (ref result-2d 0) "-(k * x(t)) - m * x_t_t(t)")
(check-infix "2D HO EOM y-component" (ref result-2d 1) "-(k * y(t)) - m * y_t_t(t)")

;;; 2D free particle: EOM = -m*x'' = 0, -m*y'' = 0
(define eom-2d-free ((Lagrange-equations (L-free-particle-nd m)) q-path-2d))
(define result-2d-free (eom-2d-free t))
(check-infix "2D free EOM x" (ref result-2d-free 0) "-(m * x_t_t(t))")
(check-infix "2D free EOM y" (ref result-2d-free 1) "-(m * y_t_t(t))")

;;; ════════════════════════════════════════════════════════════
;;; § 17  Kepler problem in polar coordinates
;;; ════════════════════════════════════════════════════════════

(define GM (sym-var 'GM))
(define r-fn   (literal-function 'r))
(define theta-fn (literal-function 'θ))
(define q-polar (lambda (tau) (up (r-fn tau) (theta-fn tau))))

(define eom-kepler ((Lagrange-equations (L-Kepler-polar m GM)) q-polar))
(define result-kepler (eom-kepler t))

(check-infix "Kepler radial EOM"
             (simplify (expand (ref result-kepler 0)))
             "m * r(t) * θ_t(t)^2 + -GM/r(t)^2 - m * r_t_t(t)")
(check-infix "Kepler angular EOM"
             (simplify (expand (ref result-kepler 1)))
             "-2 * m * r(t) * r_t(t) * θ_t(t) - m * r(t)^2 * θ_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 18  Hamiltonian mechanics — Legendre transform
;;; ════════════════════════════════════════════════════════════

;;; sym-vars for Hamiltonian state
(define q-sym (sym-var 'q))
(define p-sym (sym-var 'p))

;;; 1-DOF harmonic oscillator: H = p²/(2m) + ½kq²
(define H-ho-fn (Lagrangian->Hamiltonian (L-harmonic m k)))
(check-infix "1D HO Hamiltonian"
             (H-ho-fn (up t q-sym p-sym))
             "p^2/(2 * m) + 1/2 * k * q^2")

;;; 1-DOF free particle: H = p²/(2m)
(define H-free-fn (Lagrangian->Hamiltonian (L-free-particle m)))
(check-infix "1D free Hamiltonian"
             (H-free-fn (up t q-sym p-sym))
             "p^2/(2 * m)")

;;; ════════════════════════════════════════════════════════════
;;; § 19  Hamilton equations
;;; ════════════════════════════════════════════════════════════

(define H-state-1d (up t q-sym p-sym))
(define dH-1d ((Hamilton-equations H-ho-fn) H-state-1d))

(check-infix "1D HO dq/dt" (simplify (ref dH-1d 0)) "p/m")
(check-infix "1D HO dp/dt" (simplify (ref dH-1d 1)) "-(k * q)")

;;; 2-DOF case
(define qx (sym-var 'qx))
(define qy (sym-var 'qy))
(define px (sym-var 'px))
(define py (sym-var 'py))
(define H-2d-fn (Lagrangian->Hamiltonian (L-harmonic-nd m k)))
(define H-state-2d (up t (up qx qy) (down px py)))
(define dH-2d ((Hamilton-equations H-2d-fn) H-state-2d))

(check-infix "2D HO dqx/dt" (simplify (ref (ref dH-2d 0) 0)) "px/m")
(check-infix "2D HO dqy/dt" (simplify (ref (ref dH-2d 0) 1)) "py/m")
(check-infix "2D HO dpx/dt" (simplify (ref (ref dH-2d 1) 0)) "-(k * qx)")
(check-infix "2D HO dpy/dt" (simplify (ref (ref dH-2d 1) 1)) "-(k * qy)")

;;; ════════════════════════════════════════════════════════════
;;; § 20  Poisson bracket
;;; ════════════════════════════════════════════════════════════

(define pb-q (lambda (s) (coordinate s)))
(define pb-p (lambda (s) (momentum s)))

;;; {q, H} = dH/dp = p/m  (Hamilton's equation for q)
(check-infix "{q,H}=dq/dt"
             (simplify ((Poisson-bracket pb-q H-ho-fn) H-state-1d))
             "p/m")

;;; {p, H} = -dH/dq = -kq  (Hamilton's equation for p)
(check-infix "{p,H}=dp/dt"
             (simplify ((Poisson-bracket pb-p H-ho-fn) H-state-1d))
             "-(k * q)")

;;; Canonical commutation: {q, p} = 1
(check "{q,p}=1"
       ((Poisson-bracket pb-q pb-p) H-state-1d)
       1)

;;; ════════════════════════════════════════════════════════════
;;; § 21  momentum selector
;;; ════════════════════════════════════════════════════════════

(check "momentum selector" (momentum (up t q-sym p-sym)) p-sym)

;;; ════════════════════════════════════════════════════════════
;;; § 22  rotation-matrix-from-Euler — structure and orthogonality
;;; ════════════════════════════════════════════════════════════

;;; Identity at phi=theta=psi=0
(let ((R (rotation-matrix-from-Euler 0.0 0.0 0.0)))
  (check-num "R-Euler identity [0,0]" (mat-ref R 0 0) 1.0 1e-15)
  (check-num "R-Euler identity [0,1]" (mat-ref R 0 1) 0.0 1e-15)
  (check-num "R-Euler identity [1,0]" (mat-ref R 1 0) 0.0 1e-15)
  (check-num "R-Euler identity [1,1]" (mat-ref R 1 1) 1.0 1e-15)
  (check-num "R-Euler identity [2,2]" (mat-ref R 2 2) 1.0 1e-15))

;;; At theta=psi=0 the matrix reduces to Rz(phi).
(let* ((phi0 1.2)
       (R    (rotation-matrix-from-Euler phi0 0 0)))
  (check-num "R-Euler Rz [0,0]" (mat-ref R 0 0) (cos phi0)  1e-12)
  (check-num "R-Euler Rz [0,1]" (mat-ref R 0 1) (- (sin phi0)) 1e-12)
  (check-num "R-Euler Rz [1,0]" (mat-ref R 1 0) (sin phi0)  1e-12)
  (check-num "R-Euler Rz [1,1]" (mat-ref R 1 1) (cos phi0)  1e-12)
  (check-num "R-Euler Rz [2,2]" (mat-ref R 2 2) 1.0         1e-12))

;;; Orthogonality: R * R^T = I at arbitrary Euler angles.
(let* ((R   (rotation-matrix-from-Euler 1.1 0.7 0.3))
       (RRT (mat-mat-mul R (mat-transpose R))))
  (check-num "R*R^T [0,0]" (mat-ref RRT 0 0) 1.0 1e-12)
  (check-num "R*R^T [1,1]" (mat-ref RRT 1 1) 1.0 1e-12)
  (check-num "R*R^T [2,2]" (mat-ref RRT 2 2) 1.0 1e-12)
  (check-num "R*R^T [0,1]" (mat-ref RRT 0 1) 0.0 1e-12)
  (check-num "R*R^T [0,2]" (mat-ref RRT 0 2) 0.0 1e-12)
  (check-num "R*R^T [1,2]" (mat-ref RRT 1 2) 0.0 1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 23  Euler->omega-body
;;; ════════════════════════════════════════════════════════════

;;; Pure psi-dot spin (body z-axis): omega-body = (0, 0, psi-dot)
;;; regardless of Euler angles.
(let* ((state (up 0.0 (up 0.5 1.0 0.3) (up 0.0 0.0 2.0)))
       (omega (Euler->omega-body state)))
  (check-num "omega-body psi-spin w1" (ref omega 0) 0.0 1e-12)
  (check-num "omega-body psi-spin w2" (ref omega 1) 0.0 1e-12)
  (check-num "omega-body psi-spin w3" (ref omega 2) 2.0 1e-12))

;;; Pure theta-dot at psi=0: omega-body = (theta-dot, 0, 0).
(let* ((state (up 0.0 (up 0.5 1.0 0.0) (up 0.0 1.5 0.0)))
       (omega (Euler->omega-body state)))
  (check-num "omega-body theta-spin w1" (ref omega 0) 1.5 1e-12)
  (check-num "omega-body theta-spin w2" (ref omega 1) 0.0 1e-12)
  (check-num "omega-body theta-spin w3" (ref omega 2) 0.0 1e-12))

;;; phi-dot precession at theta=pi/2, psi=0: omega-body = (0, phi-dot, 0).
(let* ((pi/2  (/ (acos -1.0) 2))
       (state (up 0.0 (up 0.0 pi/2 0.0) (up 1.0 0.0 0.0)))
       (omega (Euler->omega-body state)))
  (check-num "omega-body phi-prec w1" (ref omega 0) 0.0 1e-12)
  (check-num "omega-body phi-prec w2" (ref omega 1) 1.0 1e-12)
  (check-num "omega-body phi-prec w3" (ref omega 2) 0.0 1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 24  Euler->omega-space
;;; ════════════════════════════════════════════════════════════

;;; Pure phi-dot spin (space z-axis): omega-space = (0, 0, phi-dot)
;;; regardless of Euler angles.
(let* ((state (up 0.0 (up 0.7 1.2 0.4) (up 3.0 0.0 0.0)))
       (omega (Euler->omega-space state)))
  (check-num "omega-space phi-spin w1" (ref omega 0) 0.0 1e-12)
  (check-num "omega-space phi-spin w2" (ref omega 1) 0.0 1e-12)
  (check-num "omega-space phi-spin w3" (ref omega 2) 3.0 1e-12))

;;; Pure theta-dot at phi=0: omega-space = (theta-dot, 0, 0).
(let* ((state (up 0.0 (up 0.0 1.2 0.4) (up 0.0 2.5 0.0)))
       (omega (Euler->omega-space state)))
  (check-num "omega-space theta-spin w1" (ref omega 0) 2.5 1e-12)
  (check-num "omega-space theta-spin w2" (ref omega 1) 0.0 1e-12)
  (check-num "omega-space theta-spin w3" (ref omega 2) 0.0 1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 25  T-rigid-body and L-rigid-body
;;; ════════════════════════════════════════════════════════════

;;; At theta=pi/2, psi=0, phi-dot=1: omega-body=(0,1,0).
;;; With B=2: T = ½·B·1² = 1.0
(let* ((pi/2  (/ (acos -1.0) 2))
       (state (up 0.0 (up 0.0 pi/2 0.0) (up 1.0 0.0 0.0)))
       (T     ((T-rigid-body 1.0 2.0 3.0) state)))
  (check-num "T-rigid-body phi-prec" T 1.0 1e-12))

;;; Pure psi-dot spin, psi-dot=2: omega-body=(0,0,2).
;;; With C=3: T = ½·C·4 = 6.0
(let* ((state (up 0.0 (up 0.5 1.0 0.3) (up 0.0 0.0 2.0)))
       (T     ((T-rigid-body 1.0 2.0 3.0) state)))
  (check-num "T-rigid-body psi-spin" T 6.0 1e-12))

;;; L-rigid-body = T-rigid-body (torque-free).
(let* ((pi/2  (/ (acos -1.0) 2))
       (state (up 0.0 (up 0.0 pi/2 0.0) (up 1.0 0.0 0.0))))
  (check-num "L-rigid-body = T" ((L-rigid-body 1.0 2.0 3.0) state)
             ((T-rigid-body 1.0 2.0 3.0) state) 1e-15))

;;; ════════════════════════════════════════════════════════════
;;; § 26  principal-value — angle normalisation
;;; ════════════════════════════════════════════════════════════

(let* ((pi     (acos -1.0))
       (norm   (principal-value pi)))
  (check-num "principal-value identity"   (norm 0.5)            0.5       1e-15)
  (check-num "principal-value wrap +"     (norm (* 3.0 pi))     pi        1e-12)
  (check-num "principal-value wrap -"     (norm (- (* 1.5 pi))) (* 0.5 pi) 1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 27  Euler's equations via ODE — rigid body dynamics
;;; ════════════════════════════════════════════════════════════

;;; Torque-free Euler equations in body frame:
;;;   A·dω1/dt = (B−C)·ω2·ω3
;;;   B·dω2/dt = (C−A)·ω3·ω1
;;;   C·dω3/dt = (A−B)·ω1·ω2
;;; State is a 3-list (omega1 omega2 omega3).
(define (euler-body-rhs A B C)
  (lambda (time-arg omega)
    (let ((w1 (list-ref omega 0))
          (w2 (list-ref omega 1))
          (w3 (list-ref omega 2)))
      (list (/ (* (- B C) w2 w3) A)
            (/ (* (- C A) w3 w1) B)
            (/ (* (- A B) w1 w2) C)))))

;;; Symmetric top (A=B=1, C=2): ω3 is an exact first integral.
;;; Initial omega=(0.1, 0, 1): ω3 must remain 1.0 throughout.
(let* ((omega-fin (ode-rk4 (euler-body-rhs 1.0 1.0 2.0)
                           '(0.1 0.0 1.0) 0.0 10.0 0.01)))
  (check-num "symmetric-top ω3 conserved"
             (list-ref omega-fin 2) 1.0 1e-6))

;;; Asymmetric top (A=1, B=2, C=3), wobbling-book initial condition.
;;; Both 2T = A·ω1²+B·ω2²+C·ω3² and |L|² = A²·ω1²+B²·ω2²+C²·ω3² are conserved.
(let* ((A 1.0) (B 2.0) (C 3.0)
       (w0 '(0.01 1.0 0.0))
       (omega-fin (ode-rk4 (euler-body-rhs A B C) w0 0.0 20.0 0.001))
       (w1 (list-ref omega-fin 0))
       (w2 (list-ref omega-fin 1))
       (w3 (list-ref omega-fin 2))
       (two-T-0  (+ (* A 0.01 0.01) (* B 1.0 1.0) (* C 0.0 0.0)))
       (two-T-f  (+ (* A w1 w1)    (* B w2 w2)    (* C w3 w3)))
       (L2-0     (+ (* A A 0.01 0.01) (* B B 1.0 1.0) (* C C 0.0 0.0)))
       (L2-f     (+ (* A A w1 w1)    (* B B w2 w2)    (* C C w3 w3))))
  (check-num "wobbling-book 2T conserved" two-T-f two-T-0 1e-5)
  (check-num "wobbling-book |L|² conserved" L2-f L2-0 1e-5))

;;; ════════════════════════════════════════════════════════════
;;; § 28  evolve-Hamiltonian — 1-DOF harmonic oscillator
;;; ════════════════════════════════════════════════════════════

;;; H = p²/(2m) + ½kq²  (m=k=1 → ω=1, T=2π)
(define (H-harmonic-1d m k)
  (lambda (state)
    (let ((q (coordinate state))
          (p (momentum state)))
      (+ (/ (* p p) (* 2.0 m))
         (* 0.5 k q q)))))

;;; Energy must be conserved to machine precision by symplectic integrator.
(let* ((H      (H-harmonic-1d 1.0 1.0))
       (s0     (up 0.0 1.0 0.0))
       (E0     (H s0))
       (states (evolve-Hamiltonian H s0 0.01 1000))
       (EN     (H (car (reverse states)))))
  (check-num "harmonic-1d energy conserved" EN E0 1e-5))

;;; After one full period T=2π the state must return to its initial value.
(let* ((pi     (acos -1.0))
       (H      (H-harmonic-1d 1.0 1.0))
       (s0     (up 0.0 1.0 0.0))
       (dt     0.001)
       (n      (exact (round (/ (* 2.0 pi) dt))))
       (states (evolve-Hamiltonian H s0 dt n))
       (sf     (car (reverse states))))
  (check-num "harmonic-1d returns to q0 after period" (coordinate sf) 1.0 1e-3)
  (check-num "harmonic-1d returns to p0 after period" (momentum sf)   0.0 1e-3))

;;; Phase portrait: after half period, q should be negated.
(let* ((pi     (acos -1.0))
       (H      (H-harmonic-1d 1.0 1.0))
       (s0     (up 0.0 1.0 0.0))
       (dt     0.001)
       (n      (exact (round (/ pi dt))))
       (states (evolve-Hamiltonian H s0 dt n))
       (sf     (car (reverse states))))
  (check-num "harmonic-1d q negated at half period" (coordinate sf) -1.0 1e-3)
  (check-num "harmonic-1d p≈0 at half period"       (momentum sf)    0.0 1e-3))

;;; ════════════════════════════════════════════════════════════
;;; § 29  evolve-Hamiltonian — nonlinear pendulum
;;; ════════════════════════════════════════════════════════════

;;; H = p²/(2m) − mg·cos(θ).  Energy must be bounded.
(define (H-pendulum m g)
  (lambda (state)
    (let ((theta (coordinate state))
          (p     (momentum state)))
      (+ (/ (* p p) (* 2.0 m))
         (* (- m) g (cos theta))))))

(let* ((H      (H-pendulum 1.0 9.8))
       (s0     (up 0.0 0.5 0.0))          ; small-angle IC
       (E0     (H s0))
       (states (evolve-Hamiltonian H s0 0.005 2000))
       (EN     (H (car (reverse states)))))
  (check-num "pendulum energy conserved (2000 steps)" EN E0 1e-4))

;;; Large amplitude (θ₀ = 2.5 rad, close to separatrix): still conserved.
(let* ((H      (H-pendulum 1.0 9.8))
       (s0     (up 0.0 2.5 0.0))
       (E0     (H s0))
       (states (evolve-Hamiltonian H s0 0.001 5000))
       (EN     (H (car (reverse states)))))
  (check-num "pendulum energy conserved (large angle)" EN E0 1e-5))

;;; ════════════════════════════════════════════════════════════
;;; § 30  evolve-Hamiltonian — 2-DOF harmonic oscillator
;;; ════════════════════════════════════════════════════════════

;;; H = (px²+py²)/2 + ½(x²+y²)  (uncoupled isotropic, m=k=1)
(define (H-harmonic-2d)
  (lambda (state)
    (let ((q  (coordinate state))
          (p  (momentum state)))
      (+ (* 0.5 (+ (* (ref p 0) (ref p 0)) (* (ref p 1) (ref p 1))))
         (* 0.5 (+ (* (ref q 0) (ref q 0)) (* (ref q 1) (ref q 1))))))))

(let* ((H      (H-harmonic-2d))
       (s0     (up 0.0 (up 1.0 0.5) (up 0.0 1.0)))
       (E0     (H s0))
       (states (evolve-Hamiltonian H s0 0.01 500))
       (EN     (H (car (reverse states)))))
  (check-num "harmonic-2d energy conserved" EN E0 1e-5))

;;; ════════════════════════════════════════════════════════════
;;; § 31  evolve-Lagrangian
;;; ════════════════════════════════════════════════════════════

;;; L-harmonic (m=k=1): L = ½qdot² − ½q².  Initial q=1, qdot=0 → p=0.
;;; Lagrangian and direct Hamiltonian paths must agree.
(let* ((m       1.0) (k 1.0)
       (L       (L-harmonic m k))
       (local0  (up 0.0 1.0 0.0))        ; t=0, q=1, qdot=0
       (dt      0.01)
       (n       200)
       ;; Hamiltonian path
       (H       (H-harmonic-1d m k))
       (states-H (evolve-Hamiltonian H (up 0.0 1.0 0.0) dt n))
       ;; Lagrangian path (converts to H internally)
       (states-L (evolve-Lagrangian L local0 dt n))
       (sf-H    (car (reverse states-H)))
       (sf-L    (car (reverse states-L))))
  (check-num "evolve-Lagrangian q matches" (coordinate sf-L) (coordinate sf-H) 1e-5)
  (check-num "evolve-Lagrangian p matches" (momentum sf-L)   (momentum sf-H)   1e-5))

;;; lagrangian->hamiltonian-state: p = ∂L/∂qdot = m·qdot = 1·2 = 2 for qdot=2.
(let* ((L   (L-harmonic 1.0 1.0))
       (loc (up 0.0 0.5 2.0))
       (hs  (lagrangian->hamiltonian-state L loc)))
  (check-num "lagrangian->hamiltonian-state p=m·qdot"
             (momentum hs) 2.0 1e-6))

;;; ════════════════════════════════════════════════════════════
;;; § 32  Hénon-Heiles: energy conservation
;;; ════════════════════════════════════════════════════════════

;;; H = ½(px²+py²) + ½(x²+y²) + x²y − ⅓y³
;;; Integrable below E≈1/12, chaotic above E≈1/6.

(define (H-henon-heiles)
  (lambda (state)
    (let* ((q  (coordinate state))
           (p  (momentum state))
           (x  (ref q 0)) (y  (ref q 1))
           (px (ref p 0)) (py (ref p 1)))
      (+ (* 0.5 (+ (* px px) (* py py)))
         (* 0.5 (+ (* x x) (* y y)))
         (* x x y)
         (* (/ -1.0 3.0) y y y)))))

;;; Regular regime E=1/24.
(let* ((H   (H-henon-heiles))
       (E0  (/ 1.0 24.0))
       ;; IC: x=0, y=0, px=0, py=sqrt(2E) — on the energy surface
       (py0 (sqrt (* 2.0 E0)))
       (s0  (up 0.0 (up 0.0 0.0) (up 0.0 py0)))
       (states (evolve-Hamiltonian H s0 0.005 4000))
       (EN  (H (car (reverse states)))))
  (check-num "Henon-Heiles energy conserved E=1/24" EN E0 1e-5))

;;; Mid-energy regime E=1/12 (onset of chaos).
(let* ((H   (H-henon-heiles))
       (E0  (/ 1.0 12.0))
       (py0 (sqrt (* 2.0 E0)))
       (s0  (up 0.0 (up 0.0 0.0) (up 0.0 py0)))
       (states (evolve-Hamiltonian H s0 0.005 4000))
       (EN  (H (car (reverse states)))))
  (check-num "Henon-Heiles energy conserved E=1/12" EN E0 1e-4))

;;; ════════════════════════════════════════════════════════════
;;; § 33  Poincaré section
;;; ════════════════════════════════════════════════════════════

;;; Regular orbit should produce crossings (non-empty section).
;;; IC: x=0.2, y=0, py=0; px from energy conservation.
;;; V(0.2,0) = ½(0.04+0)+0-0 = 0.02; E=1/12 → px=sqrt(2*(E-V)).
(let* ((H    (H-henon-heiles))
       (E0   (/ 1.0 12.0))
       (V0   0.02)
       (px0  (sqrt (* 2.0 (- E0 V0))))
       (s0   (up 0.0 (up 0.2 0.0) (up px0 0.0)))
       (pts  (poincare-section H s0 0.005 15000)))
  (check "poincare section non-empty" (> (length pts) 0) #t))

;;; All recorded y values should stay within the energy-bounded region.
;;; For E=1/12 the accessible region has |y| < ~0.8.
(let* ((H    (H-henon-heiles))
       (E0   (/ 1.0 12.0))
       (V0   0.02)
       (px0  (sqrt (* 2.0 (- E0 V0))))
       (s0   (up 0.0 (up 0.2 0.0) (up px0 0.0)))
       (pts  (poincare-section H s0 0.005 15000)))
  (check "poincare section y bounded"
         (let lp ((ps pts))
           (or (null? ps)
               (and (< (abs (car (car ps))) 0.8)
                    (lp (cdr ps)))))
         #t))

;;; stoermer-verlet-step: one explicit step of the pendulum.
;;; p should decrease (gravity pulling θ towards 0).
(let* ((H     (H-pendulum 1.0 9.8))
       (s0    (up 0.0 0.5 0.0))      ; θ=0.5, p=0
       (s1    (stoermer-verlet-step H 0.01 s0)))
  (check "stoermer-verlet-step advances time"
         (> (time s1) (time s0)) #t)
  (check-num "stoermer-verlet-step t" (time s1) 0.01 1e-12)
  ;; p should become negative: pendulum pulls θ toward 0
  (check "stoermer-verlet-step p sign" (< (momentum s1) 0.0) #t))

;;; ════════════════════════════════════════════════════════════
;;; § 34  Canonical transformations
;;; ════════════════════════════════════════════════════════════

;;; Identity transformation is trivially canonical.
(let* ((C  (lambda (s) s))
       (s0 (up 0.0 (up 1.0 0.5) (up 0.3 0.7))))
  (check "identity CT canonical (2-DOF)" (canonical? C s0) #t))

(let* ((C  (lambda (s) s))
       (s0 (up 0.0 0.8 1.2)))
  (check "identity CT canonical (1-DOF)" (canonical? C s0) #t))

;;; ── F->C ────────────────────────────────────────────────────────────────

;;; 1-DOF coordinate scaling: F(t,q) = α·q → Q=αq, P=p/α
(let* ((alpha 2.5)
       (C    (F->C (lambda (t q) (* alpha q))))
       (s0   (up 0.0 0.7 1.3))
       (sf   (C s0)))
  (check     "F->C scaling 1-DOF canonical" (canonical? C s0) #t)
  (check-num "F->C scaling Q=α·q" (coordinate sf) (* alpha (coordinate s0)) 1e-5)
  (check-num "F->C scaling P=p/α" (momentum sf)   (/ (momentum s0) alpha)   1e-5))

;;; 1-DOF negation: F(t,q) = -q → Q=-q, P=-p
(let* ((C   (F->C (lambda (t q) (- q))))
       (s0  (up 0.0 1.0 2.0))
       (sf  (C s0)))
  (check     "F->C negation canonical" (canonical? C s0) #t)
  (check-num "F->C negation Q=-q" (coordinate sf) (- (coordinate s0)) 1e-5)
  (check-num "F->C negation P=-p" (momentum sf)   (- (momentum s0))   1e-5))

;;; 2-DOF rectangular → polar is canonical (tested at non-degenerate point).
(let* ((C    (F->C rectangular->polar))
       (s0   (up 0.0 (up 1.0 0.5) (up 0.3 0.7))))
  (check "F->C rectangular->polar canonical" (canonical? C s0) #t))

;;; Verify rectangular→polar values: r=√(x²+y²), θ=atan2(y,x).
(let* ((C    (F->C rectangular->polar))
       (s0   (up 0.0 (up 3.0 4.0) (up 0.1 0.2)))
       (sf   (C s0)))
  (check-num "rectangular->polar r=5" (ref (coordinate sf) 0) 5.0 1e-5)
  (check-num "rectangular->polar θ"   (ref (coordinate sf) 1) (atan 4.0 3.0) 1e-5))

;;; ── Action-angle ────────────────────────────────────────────────────────

;;; action-angle is canonical for the harmonic oscillator.
(let* ((omega 1.0)
       (C     (action-angle omega))
       (s0    (up 0.0 1.0 0.5)))
  (check "action-angle canonical" (canonical? C s0) #t))

;;; I = (p² + ω²q²) / (2ω) = H/ω.  In new coordinates H = ω·I.
(let* ((omega 2.0)
       (C     (action-angle omega))
       (s0    (up 0.0 1.0 0.5))
       (H-old (+ (* 0.5 (momentum s0) (momentum s0))
                 (* 0.5 omega omega (coordinate s0) (coordinate s0))))
       (I     (momentum (C s0))))
  (check-num "action-angle I=H/ω" I (/ H-old omega) 1e-10))

;;; θ evolves correctly: (D (action-angle-state)) = 1 in units of ω.
;;; Verify θ at q=1, p=0 for ω=1: p=-ωq·... θ=atan2(0,ω)=0
(let* ((omega 1.0)
       (C     (action-angle omega))
       (sf    (C (up 0.0 1.0 0.0))))   ; q=1, p=0
  (check-num "action-angle θ at (q=1,p=0)" (coordinate sf) 0.0 1e-10))

;;; Verify action-angle θ at T/4: q→0, p→−1, θ=atan2(1,0)=π/2.
;;; Using a quarter-period avoids atan2 wrap-around at the full period.
(let* ((omega  1.0)
       (pi     (acos -1.0))
       (H      (lambda (s)
                 (+ (* 0.5 (momentum s) (momentum s))
                    (* 0.5 omega omega (coordinate s) (coordinate s)))))
       (C      (action-angle omega))
       (s0     (up 0.0 1.0 0.0))
       (nsteps (exact (round (/ (/ (* 2.0 pi) 4.0) 0.001))))
       (sf     (car (reverse (evolve-Hamiltonian H s0 0.001 nsteps)))))
  (check-num "action-angle θ at T/4 = π/2"
             (coordinate (C sf)) (/ pi 2) 1e-2))

;;; ── F2->CT ────────────────────────────────────────────────────────────────

;;; Identity generating function F2(t,q,P) = q·P → (Q,P) = (q,p).
(let* ((F2  (lambda (t q P) (* q P)))
       (C   (F2->CT F2))
       (s0  (up 0.0 0.8 1.5))
       (sf  (C s0)))
  (check     "F2 identity canonical"  (canonical? C s0) #t)
  (check-num "F2 identity Q=q" (coordinate sf) (coordinate s0) 1e-5)
  (check-num "F2 identity P=p" (momentum sf)   (momentum s0)   1e-5))

;;; Momentum-shift F2(t,q,P) = q·P + α·q² → P=p−2αq, Q=q.
(let* ((alpha 0.5)
       (F2   (lambda (t q P) (+ (* q P) (* alpha q q))))
       (C    (F2->CT F2))
       (s0   (up 0.0 1.0 2.0))
       (sf   (C s0))
       (q    (coordinate s0))
       (p    (momentum s0)))
  (check     "F2 momentum-shift canonical"    (canonical? C s0) #t)
  (check-num "F2 momentum-shift Q=q"          (coordinate sf) q                      1e-5)
  (check-num "F2 momentum-shift P=p−2αq"      (momentum sf)   (- p (* 2.0 alpha q))  1e-5))

;;; ── Composition ─────────────────────────────────────────────────────────

;;; Composition of canonical transformations is canonical.
(let* ((alpha  1.5)
       (C1    (F->C (lambda (t q) (* alpha q))))
       (C2    (action-angle 1.0))
       ;; compose: apply C1 first, then C2
       ;; But action-angle wants 1-DOF state, C1 returns 1-DOF state — fine.
       ;; Note: action-angle assumes H=½(p²+ω²q²) — we just check symplecticity.
       (C12   (lambda (s) (C2 (C1 s))))
       (s0    (up 0.0 0.7 1.3)))
  (check "composition of canonical CTs is canonical" (canonical? C12 s0) #t))

;;; ════════════════════════════════════════════════════════════
;;; §35  Lie transforms and perturbation theory (Ch 6–7)
;;; ════════════════════════════════════════════════════════════

;;; ── Numerical Poisson bracket ────────────────────────────────────────────

;;; Canonical relation: {q, p} = 1.
(let* ((q-fn (lambda (s) (coordinate s)))
       (p-fn (lambda (s) (momentum s)))
       (s0   (up 0.0 1.5 2.5)))
  (check-num "pb {q,p}=1"  ((pb q-fn p-fn) s0)  1.0 1e-5)
  (check-num "pb {p,q}=-1" ((pb p-fn q-fn) s0) -1.0 1e-5))

;;; Self-bracket vanishes: {H, H} = 0.
(let* ((H  (lambda (s)
              (+ (* 0.5 (momentum s) (momentum s))
                 (* 0.5 (coordinate s) (coordinate s)))))
       (s0 (up 0.0 1.0 2.0)))
  (check-num "pb {H,H}=0" ((pb H H) s0) 0.0 1e-8))

;;; ── Lie derivative ───────────────────────────────────────────────────────

;;; Free particle H = ½p²: L_H q = {q, H} = p.
(let* ((H  (lambda (s) (* 0.5 (momentum s) (momentum s))))
       (q  (lambda (s) (coordinate s)))
       (LD ((Lie-derivative H) q))
       (s0 (up 0.0 1.5 2.5)))
  (check-num "Lie-derivative L_H q = p" (LD s0) (momentum s0) 1e-5))

;;; ── Lie-transform series ─────────────────────────────────────────────────

;;; Generator W = p (coordinate momentum function).
;;; {H, p} = ω²q; {ω²q, p} = ω²; {ω², p} = 0 — series terminates at k=2.
;;; exp(ε L_p) H(q,p) = H(q+ε, p) exactly.
;;; Use n-terms=3 (k=0,1,2) to avoid nested-FD noise at k≥3.
(let* ((omega 1.0)
       (eps   0.3)
       (H     (lambda (s)
                (+ (* 0.5 (momentum s) (momentum s))
                   (* 0.5 omega omega (coordinate s) (coordinate s)))))
       (p-fn  (lambda (s) (momentum s)))
       (s0    (up 0.0 1.0 2.0))
       (LT-H  ((Lie-transform p-fn eps 3) H))   ; n-terms=3: k=0..2 only
       ;; Expected: H(q+eps, p)
       (q+e   (+ (coordinate s0) eps))
       (p     (momentum s0))
       (expected (+ (* 0.5 p p) (* 0.5 omega omega q+e q+e))))
  (check-num "Lie-transform exp(ε L_p) H = H(q+ε,p)" (LT-H s0) expected 1e-3))

;;; Generator W = q (coordinate function).
;;; L_q H = {H, q} = -p; L_q² H = {-p, q} = 1; L_q³ H = 0.
;;; exp(ε L_q) H(q,p) = ½p² - εp + ε²/2 exactly (free particle H = ½p²).
;;; Use n-terms=3 (k=0,1,2) to avoid nested-FD noise at k≥3.
(let* ((eps   0.2)
       (H     (lambda (s) (* 0.5 (momentum s) (momentum s))))
       (q-fn  (lambda (s) (coordinate s)))
       (s0    (up 0.0 1.5 3.0))
       (LT-H  ((Lie-transform q-fn eps 3) H))   ; n-terms=3: k=0..2 only
       (p     (momentum s0))
       (expected (+ (* 0.5 p p) (* -1.0 eps p) (* 0.5 eps eps))))
  (check-num "Lie-transform exp(ε L_q) H = ½p²-εp+ε²/2" (LT-H s0) expected 1e-6))

;;; Free particle: exp(ε L_H) q = q + εp (series terminates at k=1).
;;; L_H q = p; L_H² q = {p, H} = 0.
(let* ((eps  0.5)
       (H    (lambda (s) (* 0.5 (momentum s) (momentum s))))
       (q-fn (lambda (s) (coordinate s)))
       (s0   (up 0.0 1.5 2.0))
       (LT-q ((Lie-transform H eps 8) q-fn))
       (expected (+ (coordinate s0) (* eps (momentum s0)))))
  (check-num "Lie-transform free particle exp(ε L_H) q = q+εp" (LT-q s0) expected 1e-5))

;;; ── Secular averaging ────────────────────────────────────────────────────

;;; <cos θ> = 0: n=64 points, exact for frequency 1 < 32.
(let* ((cos-f   (lambda (s) (cos (coordinate s))))
       (avg-cos (secular-average cos-f 64))
       (s0      (up 0.0 0.0 1.5)))  ; t=0, θ=0 (ignored), I=1.5
  (check-num "secular-average <cos θ>=0" (avg-cos s0) 0.0 1e-10))

;;; <I²> = I²: momentum is angle-independent, passes through exactly.
(let* ((I-sq-f (lambda (s) (* (momentum s) (momentum s))))
       (avg    (secular-average I-sq-f 64))
       (I      2.5)
       (s0     (up 0.0 0.0 I)))
  (check-num "secular-average <I²>=I²" (avg s0) (* I I) 1e-10))

;;; ── Secular perturbation ─────────────────────────────────────────────────

;;; H0 = I, H1 = I² (angle-independent).  H_sec = I + ε·I².
(let* ((H0    (lambda (s) (momentum s)))
       (H1    (lambda (s) (* (momentum s) (momentum s))))
       (eps   0.1)
       (H-sec (secular-perturbation H0 H1 eps 64))
       (I     2.5)
       (s0    (up 0.0 1.0 I)))
  (check-num "secular-perturbation H0+ε<I²>"
             (H-sec s0) (+ I (* eps I I)) 1e-8))

;;; H1 = cos(2θ) averages to 0: H_sec = H0 = I.
(let* ((H0    (lambda (s) (momentum s)))
       (H1    (lambda (s) (cos (* 2.0 (coordinate s)))))
       (eps   0.1)
       (H-sec (secular-perturbation H0 H1 eps 64))
       (I     2.5)
       (s0    (up 0.0 1.0 I)))
  (check-num "secular-perturbation <cos 2θ>=0 => H_sec=I"
             (H-sec s0) I 1e-10))

;;; ════════════════════════════════════════════════════════════
;;; §36  Controller synthesis (Phase 16)
;;; ════════════════════════════════════════════════════════════

;;; ── Matrix library ──────────────────────────────────────────────────────

;;; lqr-mat-mul is correct (reuses ct-mat-mul logic).
(let* ((A '((1 2) (3 4)))
       (B '((5 6) (7 8)))
       (C (lqr-mat-mul A B)))
  (check-num "lqr-mat-mul [0,0]" (lqr-mat-ref C 0 0) 19.0 1e-10)
  (check-num "lqr-mat-mul [1,1]" (lqr-mat-ref C 1 1) 50.0 1e-10))

;;; lqr-mat-inv: 2×2 identity has inverse = identity.
(let* ((I  (lqr-mat-eye 2))
       (Ii (lqr-mat-inv I)))
  (check-num "lqr-mat-inv I[0,0]" (lqr-mat-ref Ii 0 0) 1.0 1e-10)
  (check-num "lqr-mat-inv I[0,1]" (lqr-mat-ref Ii 0 1) 0.0 1e-10))

;;; lqr-mat-inv: 2×2 known matrix.
(let* ((A  '((2.0 1.0) (5.0 3.0)))    ; det = 1
       (Ai (lqr-mat-inv A))
       (I  (lqr-mat-mul A Ai)))
  (check-num "lqr-mat-inv A*A⁻¹[0,0]" (lqr-mat-ref I 0 0) 1.0 1e-10)
  (check-num "lqr-mat-inv A*A⁻¹[1,0]" (lqr-mat-ref I 1 0) 0.0 1e-10))

;;; Lyapunov solver: A'X + XA = -M with A=-I, M=I → X=½I.
(let* ((A  '((-1.0 0.0) (0.0 -1.0)))  ; stable A = -I
       (M  '((1.0 0.0) (0.0 1.0)))    ; M = I
       (X  (lyapunov-solve A M)))      ; expect X = ½I
  (check-num "lyapunov-solve X[0,0]=0.5" (lqr-mat-ref X 0 0) 0.5 1e-8)
  (check-num "lyapunov-solve X[0,1]=0"   (lqr-mat-ref X 0 1) 0.0 1e-8)
  (check-num "lyapunov-solve X[1,1]=0.5" (lqr-mat-ref X 1 1) 0.5 1e-8))

;;; Verify solution satisfies equation: A'X + XA = -M.
(let* ((A  '((-2.0 1.0) (0.0 -3.0)))
       (M  '((1.0 0.0) (0.0 1.0)))
       (X  (lyapunov-solve A M))
       (At (lqr-mat-transpose A))
       (res (lqr-mat-add (lqr-mat-mul At X)
                         (lqr-mat-add (lqr-mat-mul X A) M))))
  (check-num "lyapunov residual[0,0]" (lqr-mat-ref res 0 0) 0.0 1e-8)
  (check-num "lyapunov residual[1,1]" (lqr-mat-ref res 1 1) 0.0 1e-8))

;;; ── LQR — continuous-time ───────────────────────────────────────────────

;;; Double integrator: ẋ₁=x₂, ẋ₂=u.  A=[[0,1],[0,0]], B=[[0],[1]].
;;; Q=I, R=[[1]].  Known optimal K = [1, √3] (LQR for double integrator).
(let* ((A '((0.0 1.0) (0.0 0.0)))
       (B '((0.0) (1.0)))
       (Q '((1.0 0.0) (0.0 1.0)))
       (R '((1.0)))
       ;; Initial K must stabilise A for policy iteration.
       ;; Start with K0 that places poles at -1: K0 = [[1 2]]
       (K (lqr-continuous A B Q R 200 1e-8 '((1.0 2.0))))
       ;; Verify closed-loop A = A - BK is stable (both eigenvalues negative real parts)
       ;; K should be close to [1, √3] ≈ [1, 1.732]
       (k1 (lqr-mat-ref K 0 0))
       (k2 (lqr-mat-ref K 0 1)))
  (check-num "lqr-continuous double-integrator K[0]≈1" k1 1.0 0.1)
  (check-num "lqr-continuous double-integrator K[1]≈√3" k2 (sqrt 3.0) 0.1))

;;; ── LQR — discrete-time ─────────────────────────────────────────────────

;;; Discrete double integrator (Euler, dt=0.1): A=[[1,0.1],[0,1]], B=[[0],[0.1]].
;;; Q=I, R=[[1]].  K should stabilise the system.
(let* ((dt 0.1)
       (A  `((1.0 ,dt) (0.0 1.0)))
       (B  `((0.0) (,dt)))
       (Q  '((1.0 0.0) (0.0 1.0)))
       (R  '((1.0)))
       (K  (lqr A B Q R))
       ;; Closed-loop A_cl = A - B*K
       (A_cl (lqr-mat-sub A (lqr-mat-mul B K)))
       ;; Compute spectral radius via |det| for 2×2 (both eigenvalues must be < 1)
       (a (lqr-mat-ref A_cl 0 0)) (b (lqr-mat-ref A_cl 0 1))
       (c (lqr-mat-ref A_cl 1 0)) (d (lqr-mat-ref A_cl 1 1))
       ;; Check |trace|<2 and det<1 as necessary stability conditions
       (tr   (+ a d))
       (detA (- (* a d) (* b c))))
  (check "lqr discrete closed-loop |tr|<2" (< (abs tr) 2.0) #t)
  (check "lqr discrete closed-loop det<1"  (< detA 1.0) #t))

;;; ── Linearise ────────────────────────────────────────────────────────────

;;; Harmonic oscillator H = ½(p² + ω²q²) at (q=0, p=0) — already linear.
;;; Expected A = [[0, 1], [-ω², 0]].
(let* ((omega 2.0)
       (H  (lambda (s)
              (+ (* 0.5 (momentum s) (momentum s))
                 (* 0.5 omega omega (coordinate s) (coordinate s)))))
       (s0 (up 0.0 0.0 0.0))   ; equilibrium (q=0,p=0)
       (A  (linearise H s0)))
  (check-num "linearise HO A[0,0]" (lqr-mat-ref A 0 0)  0.0 1e-4)
  (check-num "linearise HO A[0,1]" (lqr-mat-ref A 0 1)  1.0 1e-4)
  (check-num "linearise HO A[1,0]" (lqr-mat-ref A 1 0) (- (* omega omega)) 1e-3)
  (check-num "linearise HO A[1,1]" (lqr-mat-ref A 1 1)  0.0 1e-4))

;;; ── make-controller — closed-loop simulation ─────────────────────────────

;;; Inverted pendulum linearised at top (θ=0):
;;;   A = [[0,1],[g/l,0]],  B = [[0],[1/(ml²)]]  with g=9.81, l=1, m=1.
;;; LQR gain K stabilises the system.  Verify the controller object API.
(let* ((g 9.81) (l 1.0) (m 1.0)
       ;; Linearised matrices at upright equilibrium
       (A  `((0.0 1.0) (,(/ g l) 0.0)))
       (B  `((0.0) (,(/ 1.0 (* m l l)))))
       (Q  '((10.0 0.0) (0.0 1.0)))
       (R  '((1.0)))
       ;; Initial stabilising gain (poles at -1,-3)
       (K0 '((10.0 4.0)))
       (K  (lqr-continuous A B Q R 200 1e-8 K0))
       ;; Hamiltonian for numerical simulation: H = ½p²/m − m·g·l·cos(q)
       (H  (lambda (s)
              (- (* 0.5 (/ 1.0 (* m l l)) (momentum s) (momentum s))
                 (* m g l (cos (coordinate s))))))
       ;; Start slightly off-vertical
       (s0   (up 0.0 0.05 0.0))
       (x-eq (up 0.0 0.0  0.0))
       (ctrl (make-controller H K x-eq 0.01))
       (step! (cdr (assq 'step! ctrl))))
  ;; Run 200 steps (2 seconds) and verify pendulum stays near upright
  (let loop ((i 0) (s s0))
    (if (>= i 200) s
        (let ((r (step!)))
          (loop (+ i 1) (car r)))))
  (let* ((final-state ((cdr (assq 'current-state ctrl))))
         (theta (coordinate final-state)))
    (check "inverted-pendulum controller object has step!" (procedure? step!) #t)
    (check-num "inverted-pendulum stays near upright after 2s"
               (abs theta) 0.0 0.5)))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
