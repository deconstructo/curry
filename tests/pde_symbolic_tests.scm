;;; pde_symbolic_tests.scm — test suite for (curry pde-symbolic)
;;;
;;; Covers:
;;;   § 1  Eigenvalue/eigenfunction values and boundary conditions
;;;   § 2  Separation-of-variables modes satisfy their PDEs (numerical)
;;;   § 3  Fourier coefficient computation via symbolic integration
;;;   § 4  Method of characteristics — symbolic residual cancellation
;;;   § 5  Laplace-transform ODE reduction
;;;   § 6  Cross-validation: Fourier series vs. closed-form solutions

(import (scheme base))
(import (scheme inexact))
(import (curry pde-symbolic))

;;; ─── test harness ───────────────────────────────────────────
(define pass-count 0)
(define fail-count 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (set! pass-count (+ pass-count 1)))
      (begin
        (set! fail-count (+ fail-count 1))
        (display "FAIL: ") (display label) (newline)
        (display "  expected: ") (display expected) (newline)
        (display "  got:      ") (display got) (newline))))

(define tolerance 1e-9)

(define (check-approx label got expected)
  (let ((diff (abs (- (inexact got) (inexact expected)))))
    (if (< diff tolerance)
        (set! pass-count (+ pass-count 1))
        (begin
          (set! fail-count (+ fail-count 1))
          (display "FAIL: ") (display label) (newline)
          (display "  expected: ≈") (display (inexact expected)) (newline)
          (display "  got:      ") (display (inexact got)) (newline)
          (display "  |diff|:   ") (display diff) (newline)))))

;;; Substitute multiple sym-vars with corresponding values.
(define (subst-all expr vars vals)
  (if (null? vars)
      expr
      (subst-all (substitute expr (car vars) (car vals))
                 (cdr vars)
                 (cdr vals))))

;;; Approximate zero check on a symbolic residual at numeric point.
(define (check-residual-zero label residual vars vals)
  (check-approx label (pde-residual-at residual vars vals) 0.0))

(define pi (acos -1.0))

;;; ─── § 1  EIGENVALUE AND EIGENFUNCTION ───────────────────────

;;; Symbolic variables used throughout
(define sx (sym-var 'x))
(define sy (sym-var 'y))
(define st (sym-var 't))

;;; pde-eigenvalue n L = (nπ/L)²
(check-approx "eigenvalue n=1 L=pi is 1"
  (exact->inexact (subst-all (pde-eigenvalue (sym-var 'n) (sym-var 'L))
                              (list (sym-var 'n) (sym-var 'L))
                              (list 1 pi)))
  1.0)

(check-approx "eigenvalue n=2 L=1 is 4pi²"
  (exact->inexact (subst-all (pde-eigenvalue (sym-var 'n) (sym-var 'L))
                              (list (sym-var 'n) (sym-var 'L))
                              (list 2 1.0)))
  (* 4.0 pi pi))

(check-approx "eigenvalue n=3 L=2 is (3pi/2)²"
  (exact->inexact (subst-all (pde-eigenvalue (sym-var 'n) (sym-var 'L))
                              (list (sym-var 'n) (sym-var 'L))
                              (list 3 2.0)))
  (expt (* 3.0 pi 0.5) 2))

;;; Eigenfunction boundary conditions X_n(0) = 0, X_n(L) ≈ 0
(check-approx "eigenfunction X_n(0) = 0"
  (exact->inexact (subst-all (pde-eigenfunction (sym-var 'n) sx (sym-var 'L))
                              (list (sym-var 'n) sx (sym-var 'L))
                              (list 1 0.0 1.0)))
  0.0)

(check-approx "eigenfunction X_n(L) ≈ 0 for n=1, L=1"
  (exact->inexact (subst-all (pde-eigenfunction (sym-var 'n) sx (sym-var 'L))
                              (list (sym-var 'n) sx (sym-var 'L))
                              (list 1 1.0 1.0)))
  0.0)

;;; Eigenfunction interior value: X_1(π/2) at L=π → sin(π/2) = 1
(check-approx "eigenfunction X_1(pi/2) at L=pi is 1"
  (exact->inexact (subst-all (pde-eigenfunction (sym-var 'n) sx (sym-var 'L))
                              (list (sym-var 'n) sx (sym-var 'L))
                              (list 1 (* 0.5 pi) pi)))
  1.0)

;;; ─── § 2  HEAT MODE SATISFIES u_t = alpha·u_xx ───────────────

;;; Build heat mode and its residual symbolically
(define salpha (sym-var 'alpha 'positive))
(define sL     (sym-var 'L 'positive))
(define sn     (sym-var 'n 'positive))

(define heat-mode-sym (pde-heat-mode salpha sL sn sx st))
(define heat-residual-sym (pde-heat-residual salpha heat-mode-sym sx st))

;;; Verify residual ≈ 0 at several (alpha, L, n, x, t) points
(check-residual-zero
  "heat mode residual: alpha=0.1 L=1 n=1 x=0.3 t=0.5"
  heat-residual-sym
  (list salpha sL sn sx st)
  (list 0.1    1.0 1  0.3 0.5))

(check-residual-zero
  "heat mode residual: alpha=0.5 L=pi n=2 x=1.0 t=0.1"
  heat-residual-sym
  (list salpha sL sn sx st)
  (list 0.5    pi  2  1.0 0.1))

(check-residual-zero
  "heat mode residual: alpha=1 L=2 n=3 x=0.5 t=0.2"
  heat-residual-sym
  (list salpha sL sn sx st)
  (list 1.0    2.0 3  0.5 0.2))

;;; At t=0 the heat mode equals the eigenfunction sin(nπx/L)
(check-approx "heat mode at t=0 equals eigenfunction"
  (exact->inexact (subst-all heat-mode-sym
                              (list salpha sL sn sx st)
                              (list 0.3    1.0 1  0.4 0.0)))
  (exact->inexact (subst-all (pde-eigenfunction sn sx sL)
                              (list sn sx sL)
                              (list 1  0.4 1.0))))

;;; ─── § 2  WAVE MODES SATISFY u_tt = c²·u_xx ─────────────────

(define sc (sym-var 'c 'positive))
(define wcos-mode (pde-wave-cos-mode sc sL sn sx st))
(define wsin-mode (pde-wave-sin-mode sc sL sn sx st))
(define wave-res-cos (pde-wave-residual sc wcos-mode sx st))
(define wave-res-sin (pde-wave-residual sc wsin-mode sx st))

(check-residual-zero
  "wave cos mode residual: c=1 L=pi n=1 x=1 t=0.5"
  wave-res-cos
  (list sc  sL  sn sx  st)
  (list 1.0 pi  1  1.0 0.5))

(check-residual-zero
  "wave sin mode residual: c=2 L=1 n=1 x=0.5 t=0.3"
  wave-res-sin
  (list sc  sL  sn sx  st)
  (list 2.0 1.0 1  0.5 0.3))

;;; Wave cos mode at t=0 equals eigenfunction
(check-approx "wave cos mode at t=0 equals eigenfunction"
  (exact->inexact (subst-all wcos-mode
                              (list sc  sL  sn sx  st)
                              (list 1.5 1.0 2  0.3 0.0)))
  (exact->inexact (subst-all (pde-eigenfunction sn sx sL)
                              (list sn sx  sL)
                              (list 2  0.3 1.0))))

;;; ─── § 2  LAPLACE 2D MODE SATISFIES u_xx + u_yy = 0 ──────────

(define lap2d-mode (pde-laplace-2d-mode sn sx sy sL))
(define lap2d-res  (pde-laplace-residual lap2d-mode sx sy))

(check-residual-zero
  "Laplace 2D mode residual: n=1 L=1 x=0.5 y=0.3"
  lap2d-res
  (list sn sx  sy  sL)
  (list 1  0.5 0.3 1.0))

(check-residual-zero
  "Laplace 2D mode residual: n=2 L=pi x=1 y=0.5"
  lap2d-res
  (list sn sx  sy  sL)
  (list 2  1.0 0.5 pi))

;;; ─── § 3  FOURIER COEFFICIENT COMPUTATION ────────────────────

;;; B_1 for f(x)=sin(πx) on [0,1]: should be 1 (orthogonality)
;;; B_1 = 2·∫₀¹ sin(πx)·sin(πx)dx = 2·(1/2) = 1
(check-approx "Fourier B_1 for f=sin(πx), n=1, L=1 is 1"
  (exact->inexact (pde-fourier-sin-coeff (sin (* pi sx)) 1.0 1 sx))
  1.0)

;;; B_1 for f(x)=x on [0,1]: should be 2/π ≈ 0.6366
;;; B_1 = 2·∫₀¹ x·sin(πx)dx = 2·(1/π) = 2/π
(check-approx "Fourier B_1 for f=x, n=1, L=1 is 2/pi"
  (exact->inexact (pde-fourier-sin-coeff sx 1.0 1 sx))
  (/ 2.0 pi))

;;; B_2 for f(x)=x on [0,1]: should be −1/π ≈ −0.3183
;;; B_2 = 2·∫₀¹ x·sin(2πx)dx = 2·(−1/(2π)) = −1/π
(check-approx "Fourier B_2 for f=x, n=2, L=1 is -1/pi"
  (exact->inexact (pde-fourier-sin-coeff sx 1.0 2 sx))
  (/ -1.0 pi))

;;; ─── § 4  METHOD OF CHARACTERISTICS ─────────────────────────

;;; Transport equation: u_t + v·u_x = 0
;;; With v=2 (numeric), residual should simplify to 0 symbolically.
(define transport-u (pde-transport-general 2 sx st))
(define transport-res (pde-transport-residual 2 transport-u sx st))

(check "transport residual simplifies to 0 (v=2)"
  (simplify transport-res)
  0)

;;; Numerical spot-check: f evaluated at (x=1.5, t=0.5) with v=3
;;; u(x,t) = f(x−3t) satisfies u_t + 3·u_x = 0
(define transport-u3 (pde-transport-general 3 sx st))
(define transport-res3 (pde-transport-residual 3 transport-u3 sx st))

(check "transport residual simplifies to 0 (v=3)"
  (simplify transport-res3)
  0)

;;; First-order PDE  a·u_x + b·u_t = 0, general solution f(bx−at)
;;; With a=3, b=2: verify 3·u_x + 2·u_t = 0 symbolically
(define char-u (pde-characteristics-homogeneous 3 2 sx st))
(define char-res (pde-first-order-residual 3 2 0 char-u sx st))

(check "characteristics homogeneous residual = 0 (a=3 b=2)"
  (simplify char-res)
  0)

;;; First-order PDE  u_x + u_t = 2·u, solution f(x−t)·exp(2t)
;;; With a=1, b=1, c=2: verify u_x + u_t = 2·u
;;; Numeric check (symbolic has non-trivial sym-fn in residual)
(define decay-u (pde-characteristics-decay 1 1 2 sx st))
(define decay-res (pde-first-order-residual 1 1 2 decay-u sx st))

;;; The residual contains f and f' — they cancel symbolically
(check "characteristics decay residual = 0 (a=1 b=1 c=2)"
  (simplify decay-res)
  0)

;;; ─── § 5  LAPLACE-TRANSFORM ODE REDUCTION ────────────────────

(define ss (sym-var 's 'positive))

;;; Check the ODE coefficients for alpha·U_xx − s·U = −u₀
;;; pde-heat-laplace-ode alpha s u-init → (alpha -s -u-init)
(let* ((ode (pde-heat-laplace-ode salpha ss (sin sx)))
       (a-coeff (car ode))
       (s-coeff (cadr ode))
       (rhs     (caddr ode)))
  (check "Laplace ODE: alpha coefficient" a-coeff salpha)
  (check-approx "Laplace ODE: s-coefficient at s=2" (exact->inexact (substitute s-coeff ss 2)) -2.0)
  (check-approx "Laplace ODE: rhs at x=pi/2" (exact->inexact (substitute rhs sx (* 0.5 pi))) -1.0))

;;; The Laplace residual for a properly-defined U satisfies a second-order ODE.
;;; Verify the structure is correct: residual has three terms (alpha·U_xx, -s·U, u-init).
;;; We use a simple linear u-init = 0 so the ODE has the form alpha·U_xx - s·U = 0.
(let* ((sU (sym-fn 'U sx ss))
       (res (pde-heat-laplace-residual salpha ss sU sx 0)))
  ;; At specific numeric values the residual should be non-trivially structured
  ;; but equal to alpha*U_xx - s*U which for an ODE solution would be 0.
  ;; Here we just confirm it's not zero (no solution imposed yet):
  (check "Laplace residual is non-trivial (no solution imposed)"
    (equal? (simplify res) 0)
    #f))

;;; ─── § 6  CROSS-VALIDATION ───────────────────────────────────

;;; Heat equation: exact solution for u₀(x)=sin(πx) on [0,1] is
;;;   u(x,t) = sin(πx)·exp(−α·π²·t)  (only n=1 mode survives)
;;; Verify: (pde-heat-mode alpha 1 1 x t) evaluated numerically
;;; matches exp(−alpha·π²·t)·sin(π·x)
(let* ((al 0.1) (L 1.0) (n 1) (xv 0.4) (tv 0.3)
       (mode (pde-heat-mode salpha sL sn sx st))
       (got  (exact->inexact (subst-all mode
                               (list salpha sL sn sx st)
                               (list al     L  n  xv tv))))
       (expected (* (sin (* pi xv))
                    (exp (* (- al) pi pi tv)))))
  (check-approx "heat mode numeric value matches closed form (n=1)" got expected))

;;; Wave equation: exact solution for u₀=sin(πx/L), v₀=0 on [0,L] is
;;;   u(x,t) = sin(πx/L)·cos(cπt/L)  (n=1 cos mode, A₁=1, Bₙ=0)
;;; Verify pde-wave-cos-mode matches this at several points.
(let* ((cv 1.5) (Lv 2.0) (nv 1)
       (mode (pde-wave-cos-mode sc sL sn sx st)))
  (for-each
    (lambda (xv tv)
      (let ((got (exact->inexact (subst-all mode
                                   (list sc  sL  sn sx  st)
                                   (list cv  Lv  nv xv  tv))))
            (exp-val (* (sin (* pi xv (/ 1 Lv)))
                        (cos (* cv pi nv tv (/ 1 Lv))))))
        (check-approx
          (string-append "wave cos mode matches closed form x=" (number->string xv) " t=" (number->string tv))
          got exp-val)))
    '(0.3 0.7 1.0)
    '(0.1 0.3 0.5)))

;;; Summary
(newline)
(display "pde-symbolic: ")
(display pass-count)
(display " passed, ")
(display fail-count)
(display " failed")
(newline)
(when (> fail-count 0) (error "test failures"))
