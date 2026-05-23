;;; (curry pde-symbolic) — Symbolic PDE solving
;;;
;;; šiddu ša erṣetim u šamê — the tablet of earth and sky
;;;
;;; Tools for exact symbolic analysis of linear PDEs:
;;;
;;;   § 1  Sturm-Liouville eigenvalue problem (Dirichlet)
;;;   § 2  Separation of variables — heat, wave, Laplace equations
;;;   § 3  Fourier coefficient computation (symbolic integration)
;;;   § 4  Method of characteristics — transport, first-order linear PDEs
;;;   § 5  Laplace-transform ODE reduction
;;;   § 6  PDE residual computation (for verification)
;;;
;;; All procedures accept sym-var arguments and return symbolic expressions
;;; suitable for further manipulation via ∂, substitute, simplify, etc.
;;;
;;; Variable conventions:
;;;   x, y, t   — spatial/temporal sym-vars (caller-supplied)
;;;   n         — mode index (number or positive sym-var)
;;;   L         — domain length (number or positive sym-var)
;;;   alpha, c  — PDE parameters (numbers or sym-vars)

(import (scheme base))
(import (scheme inexact))

;;; ════════════════════════════════════════════════════════════
;;; § 1  STURM-LIOUVILLE EIGENVALUE PROBLEM (Dirichlet, [0,L])
;;;
;;; X'' + λX = 0,  X(0) = X(L) = 0
;;;   Eigenvalues:    λ_n = (nπ/L)²
;;;   Eigenfunctions: X_n(x) = sin(nπx/L)
;;;   Orthogonality:  ∫₀ᴸ X_m(x)·X_n(x)dx = (L/2)·δ_{mn}
;;; ════════════════════════════════════════════════════════════

;;; n-th Dirichlet eigenvalue (nπ/L)²
(define (pde-eigenvalue n L)
  (expt (/ (* n pi) L) 2))

;;; n-th Dirichlet eigenfunction sin(nπx/L)
(define (pde-eigenfunction n x L)
  (sin (* (/ (* n pi) L) x)))

;;; ════════════════════════════════════════════════════════════
;;; § 2  SEPARATION OF VARIABLES
;;; ════════════════════════════════════════════════════════════

;;; Heat equation  u_t = alpha·u_xx  on (0,L), Dirichlet BCs:
;;;   u_n(x,t) = sin(nπx/L) · exp(−alpha·(nπ/L)²·t)
(define (pde-heat-mode alpha L n x t)
  (* (pde-eigenfunction n x L)
     (exp (* (- alpha) (pde-eigenvalue n L) t))))

;;; Wave equation  u_tt = c²·u_xx  on (0,L), Dirichlet BCs:
;;; Each eigenfrequency ω_n = cnπ/L admits two linearly independent modes.
;;;   cos mode: sin(nπx/L)·cos(ω_n·t)
;;;   sin mode: sin(nπx/L)·sin(ω_n·t)
;;; General mode: A_n·(cos mode) + B_n·(sin mode)
(define (pde-wave-cos-mode c L n x t)
  (* (pde-eigenfunction n x L)
     (cos (* c (/ (* n pi) L) t))))

(define (pde-wave-sin-mode c L n x t)
  (* (pde-eigenfunction n x L)
     (sin (* c (/ (* n pi) L) t))))

;;; Laplace equation  u_xx + u_yy = 0  on (0,L)×(0,H),
;;;   u = 0 on x=0, x=L, y=0;  u arbitrary on y=H:
;;;   u_n(x,y) = sin(nπx/L) · sinh(nπy/L)
(define (pde-laplace-2d-mode n x y L)
  (* (pde-eigenfunction n x L)
     (sinh (* (/ (* n pi) L) y))))

;;; ════════════════════════════════════════════════════════════
;;; § 3  FOURIER COEFFICIENT COMPUTATION
;;;
;;; Given initial condition f(x), the Fourier coefficients that
;;; weight each eigenmode are:
;;;   Bₙ = (2/L) ∫₀ᴸ f(x)·sin(nπx/L) dx
;;;   Aₙ = (2/L) ∫₀ᴸ f(x)·cos(nπx/L) dx
;;;
;;; f-expr must be a symbolic expression in x.
;;; n and L may be numbers or sym-vars; x must be a sym-var.
;;; ════════════════════════════════════════════════════════════

;;; Fourier sine coefficient Bₙ = (2/L)·∫₀ᴸ f(x)·sin(nπx/L)dx
(define (pde-fourier-sin-coeff f-expr L n x)
  (* (/ 2 L)
     (∫ (* f-expr (pde-eigenfunction n x L)) x 0 L)))

;;; Fourier cosine coefficient Aₙ = (2/L)·∫₀ᴸ f(x)·cos(nπx/L)dx
(define (pde-fourier-cos-coeff f-expr L n x)
  (* (/ 2 L)
     (∫ (* f-expr (cos (* (/ (* n pi) L) x))) x 0 L)))

;;; ════════════════════════════════════════════════════════════
;;; § 4  METHOD OF CHARACTERISTICS
;;;
;;; For a·u_x + b·u_t = 0 (constant a,b ≠ 0):
;;;   Characteristics satisfy dx/b = dt/a → b·x − a·t = const
;;;   General solution: u(x,t) = f(b·x − a·t)
;;;
;;; Interpretation: u is constant along lines with slope dt/dx = b/a
;;; in the (x,t) plane (the characteristics).
;;; ════════════════════════════════════════════════════════════

;;; Transport equation u_t + v·u_x = 0:
;;;   General solution: u(x,t) = f(x − v·t)
;;; Returns (fn-apply f (- x (* v t))) with f = (sym-fn 'f ξ).
(define (pde-transport-general v x t)
  (let* ((xi (sym-var 'xi))
         (f  (sym-fn 'f xi)))
    (fn-apply f (- x (* v t)))))

;;; First-order PDE  a·u_x + b·u_t = 0:
;;;   General solution: u(x,t) = f(b·x − a·t)
(define (pde-characteristics-homogeneous a b x t)
  (let* ((xi (sym-var 'xi))
         (f  (sym-fn 'f xi)))
    (fn-apply f (- (* b x) (* a t)))))

;;; First-order PDE  a·u_x + b·u_t = c·u  (constant a,b,c, b ≠ 0):
;;;   Along each characteristic s ↦ (as+x₀, bs+t₀):
;;;     du/ds = c·u  →  u = u₀·exp(c·s)
;;;   With s = t/b:  u(x,t) = f(b·x − a·t)·exp(c·t/b)
(define (pde-characteristics-decay a b c x t)
  (let* ((xi (sym-var 'xi))
         (f  (sym-fn 'f xi)))
    (* (fn-apply f (- (* b x) (* a t)))
       (exp (* (/ c b) t)))))

;;; ════════════════════════════════════════════════════════════
;;; § 5  LAPLACE-TRANSFORM ODE REDUCTION
;;;
;;; Applying L{·} in t to u_t = alpha·u_xx with IC u(x,0)=u₀(x):
;;;   L{u_t} = s·U(x,s) − u₀(x)
;;;   → s·U − u₀ = alpha·U_xx
;;;   → alpha·U_xx − s·U = −u₀(x)   [ODE in x for each s]
;;;
;;; This reduces the heat PDE to a family of second-order linear ODEs.
;;; ════════════════════════════════════════════════════════════

;;; Returns '(alpha-coeff s-coeff rhs) for the ODE alpha·U_xx − s·U = rhs
;;; where rhs = −u-init-expr (the negated initial condition).
(define (pde-heat-laplace-ode alpha s u-init-expr)
  (list alpha (- s) (- u-init-expr)))

;;; Symbolic residual of the transformed heat equation:
;;; alpha·U_xx(x,s) − s·U(x,s) + u-init(x), using U as a sym-fn.
;;; A solution U satisfies this = 0.
(define (pde-heat-laplace-residual alpha s U x u-init-expr)
  (let* ((Uapp (fn-apply U x s))
         (Uxx  (∂ (∂ Uapp x) x)))
    (+ (* alpha Uxx) (* (- s) Uapp) u-init-expr)))

;;; ════════════════════════════════════════════════════════════
;;; § 6  PDE RESIDUAL COMPUTATION
;;;
;;; Each procedure returns the symbolic residual of its PDE.
;;; A proposed solution u is valid iff the residual simplifies to 0.
;;; Use pde-residual-at for numerical spot-checks.
;;; ════════════════════════════════════════════════════════════

;;; Residual of  u_t − alpha·u_xx  (heat equation)
(define (pde-heat-residual alpha u x t)
  (- (∂ u t) (* alpha (∂ (∂ u x) x))))

;;; Residual of  u_tt − c²·u_xx  (wave equation)
(define (pde-wave-residual c u x t)
  (- (∂ (∂ u t) t) (* c c (∂ (∂ u x) x))))

;;; Residual of  u_t + v·u_x  (transport equation)
(define (pde-transport-residual v u x t)
  (+ (∂ u t) (* v (∂ u x))))

;;; Residual of  u_xx + u_yy  (2D Laplace equation)
(define (pde-laplace-residual u x y)
  (+ (∂ (∂ u x) x) (∂ (∂ u y) y)))

;;; Residual of  a·u_x + b·u_t − c·u  (first-order linear PDE)
(define (pde-first-order-residual a b c u x t)
  (- (+ (* a (∂ u x)) (* b (∂ u t))) (* c u)))

;;; Numerically evaluate a symbolic residual by substituting var/val pairs.
;;; vars: list of sym-vars; vals: corresponding list of numbers.
;;; Returns inexact number — should be near 0 for a valid solution.
(define (pde-residual-at residual vars vals)
  (let loop ((e residual) (vs vars) (ws vals))
    (if (null? vs)
        (exact->inexact e)
        (loop (substitute e (car vs) (car ws))
              (cdr vs)
              (cdr ws)))))
