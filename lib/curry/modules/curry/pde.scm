;;; (curry pde) — Partial Differential Equation solvers (numerical)
;;;
;;; šiddu ša tâmtim rabbīti — the tablet of the great sea
;;;
;;; Heat:    u_t  = α·u_xx          (method of lines + RK4)
;;; Wave:    u_tt = c²·u_xx         (leapfrog, second-order)
;;; Poisson: u_xx = f(x)            (Thomas tridiagonal algorithm)
;;;
;;; Plus a general method-of-lines framework (pde-mol) for arbitrary
;;; 1D time-dependent PDEs with a user-supplied RHS.
;;;
;;; Boundary conditions:
;;;   (bc-dirichlet left right)           — fixed endpoint values
;;;   (bc-neumann   left-deriv right-deriv) — fixed endpoint derivatives
;;;   (bc-periodic)                        — wrap-around
;;;
;;; Grid: (pde-linspace a b n) → vector of n equispaced flonum x values.
;;; All solution vectors are vectors of flonums.

(import (scheme base))
(import (scheme inexact))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 1  BOUNDARY CONDITIONS
;;;; ═══════════════════════════════════════════════════════════

(define (bc-dirichlet left right)
  (list 'dirichlet (inexact left) (inexact right)))

(define (bc-neumann left-deriv right-deriv)
  (list 'neumann (inexact left-deriv) (inexact right-deriv)))

(define (bc-periodic)
  '(periodic))

(define (bc-type bc) (car bc))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 2  GRID UTILITY
;;;; ═══════════════════════════════════════════════════════════

(define (pde-linspace a b n)
  (let* ((af (inexact a))
         (bf (inexact b))
         (h  (/ (- bf af) (- n 1)))
         (v  (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i n) v)
      (vector-set! v i (+ af (* i h))))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 3  INTERNAL VECTOR ARITHMETIC
;;;; ═══════════════════════════════════════════════════════════

(define (vec+ a b)
  (let* ((n (vector-length a)) (r (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i n) r)
      (vector-set! r i (+ (vector-ref a i) (vector-ref b i))))))

(define (vec- a b)
  (let* ((n (vector-length a)) (r (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i n) r)
      (vector-set! r i (- (vector-ref a i) (vector-ref b i))))))

(define (vec* s v)
  (let* ((sf (inexact s)) (n (vector-length v)) (r (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i n) r)
      (vector-set! r i (* sf (vector-ref v i))))))

(define (vec-inexact v)
  (let* ((n (vector-length v)) (r (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i n) r)
      (vector-set! r i (inexact (vector-ref v i))))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 4  FINITE-DIFFERENCE STENCILS
;;;; ═══════════════════════════════════════════════════════════

;;; ∂²u/∂x² via central differences. Returns a new vector length n.
;;; Boundary treatment depends on bc type.
(define (fd-laplacian-1d u dx bc)
  (let* ((n   (vector-length u))
         (dxf (inexact dx))
         (dx2 (* dxf dxf))
         (r   (make-vector n 0.0)))
    ;; Interior points
    (do ((i 1 (+ i 1))) ((= i (- n 1)))
      (vector-set! r i
        (/ (+ (vector-ref u (- i 1))
              (* -2.0 (vector-ref u i))
              (vector-ref u (+ i 1)))
           dx2)))
    ;; Boundaries
    (case (bc-type bc)
      ((dirichlet)
       ;; Boundaries are fixed — du/dt=0 there; leave r[0]=r[n-1]=0.0
       #f)
      ((neumann)
       ;; Ghost point via symmetric difference: u'(0)=g → u[-1]=u[1]-2dx·g
       (let ((gl (* 2.0 dxf (cadr  bc)))
             (gr (* 2.0 dxf (caddr bc))))
         (vector-set! r 0
           (/ (+ (- (vector-ref u 1) gl)
                 (* -2.0 (vector-ref u 0))
                 (vector-ref u 1))
              dx2))
         (vector-set! r (- n 1)
           (/ (+ (vector-ref u (- n 2))
                 (* -2.0 (vector-ref u (- n 1)))
                 (+ (vector-ref u (- n 2)) gr))
              dx2))))
      ((periodic)
       ;; u[-1]=u[n-2], u[n]=u[1]
       (vector-set! r 0
         (/ (+ (vector-ref u (- n 2))
               (* -2.0 (vector-ref u 0))
               (vector-ref u 1))
            dx2))
       (vector-set! r (- n 1)
         (/ (+ (vector-ref u (- n 2))
               (* -2.0 (vector-ref u (- n 1)))
               (vector-ref u 1))
            dx2))))
    r))

;;; ∂u/∂x via central differences (one-sided at boundaries for Dirichlet).
(define (fd-gradient-1d u dx bc)
  (let* ((n    (vector-length u))
         (dxf  (inexact dx))
         (2dxf (* 2.0 dxf))
         (r    (make-vector n 0.0)))
    (do ((i 1 (+ i 1))) ((= i (- n 1)))
      (vector-set! r i
        (/ (- (vector-ref u (+ i 1)) (vector-ref u (- i 1))) 2dxf)))
    (case (bc-type bc)
      ((dirichlet)
       (vector-set! r 0
         (/ (- (vector-ref u 1) (vector-ref u 0)) dxf))
       (vector-set! r (- n 1)
         (/ (- (vector-ref u (- n 1)) (vector-ref u (- n 2))) dxf)))
      ((neumann)
       (vector-set! r 0 (cadr bc))
       (vector-set! r (- n 1) (caddr bc)))
      ((periodic)
       (vector-set! r 0
         (/ (- (vector-ref u 1) (vector-ref u (- n 2))) 2dxf))
       (vector-set! r (- n 1)
         (/ (- (vector-ref u 1) (vector-ref u (- n 2))) 2dxf))))
    r))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 5  BOUNDARY ENFORCEMENT
;;;; ═══════════════════════════════════════════════════════════

(define (bc-enforce! u bc)
  (let ((n (vector-length u)))
    (case (bc-type bc)
      ((dirichlet)
       (vector-set! u 0 (cadr bc))
       (vector-set! u (- n 1) (caddr bc)))
      ((periodic)
       ;; Keep u[0]=u[n-1] by averaging (prevents drift)
       (let ((avg (* 0.5 (+ (vector-ref u 0) (vector-ref u (- n 1))))))
         (vector-set! u 0 avg)
         (vector-set! u (- n 1) avg)))
      (else #f))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 6  INTERNAL VECTOR RK4 STEPPER
;;;; ═══════════════════════════════════════════════════════════

(define (vec-rk4-step f t u h)
  (let* ((k1 (f t u))
         (h2 (* 0.5 h))
         (k2 (f (+ t h2) (vec+ u (vec* h2 k1))))
         (k3 (f (+ t h2) (vec+ u (vec* h2 k2))))
         (k4 (f (+ t h)  (vec+ u (vec* h  k3)))))
    (vec+ u (vec* (/ h 6.0)
                  (vec+ k1 (vec+ (vec* 2.0 k2)
                                 (vec+ (vec* 2.0 k3) k4)))))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 7  METHOD OF LINES — general 1D time-dependent PDE
;;;;
;;;; rhs: (lambda (t u-vector) ...) → du/dt vector
;;;; BCs are enforced on u after each full RK4 step.
;;;; u0 may contain exact numbers; converted to inexact internally.
;;;; ═══════════════════════════════════════════════════════════

(define (pde-mol/steps rhs u0 t0 t1 dt bc)
  (let* ((t0f (inexact t0))
         (t1f (inexact t1))
         (hf  (inexact dt))
         (u   (vec-inexact u0)))
    (bc-enforce! u bc)
    (let loop ((t t0f) (u u) (acc (list (cons t0f (vector-copy u)))))
      (if (>= t t1f)
          (reverse acc)
          (let* ((step (min hf (- t1f t)))
                 (u1   (vec-rk4-step rhs t u step)))
            (bc-enforce! u1 bc)
            (loop (+ t step) u1
                  (cons (cons (+ t step) (vector-copy u1)) acc)))))))

(define (pde-mol rhs u0 t0 t1 dt bc)
  (cdr (car (reverse (pde-mol/steps rhs u0 t0 t1 dt bc)))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 8  HEAT EQUATION:  u_t = α·u_xx
;;;; ═══════════════════════════════════════════════════════════

(define (pde-heat/steps alpha u0 a b nx t0 t1 dt bc)
  (let* ((dx  (/ (- (inexact b) (inexact a)) (- nx 1)))
         (alp (inexact alpha)))
    (pde-mol/steps
      (lambda (t u) (vec* alp (fd-laplacian-1d u dx bc)))
      u0 t0 t1 dt bc)))

(define (pde-heat alpha u0 a b nx t0 t1 dt bc)
  (cdr (car (reverse (pde-heat/steps alpha u0 a b nx t0 t1 dt bc)))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 9  WAVE EQUATION:  u_tt = c²·u_xx   (leapfrog)
;;;;
;;;; Leapfrog: u^{n+1} = 2·u^n - u^{n-1} + (c·dt)²·Δu^n
;;;; Ghost layer at t=-dt initialised to second-order accuracy:
;;;;   u^{-1} = u0 - dt·v0 + (c²dt²/2)·Δu0
;;;; Stability: CFL number r = c·dt/dx must satisfy r ≤ 1.
;;;; ═══════════════════════════════════════════════════════════

(define (pde-wave/steps c u0 v0 a b nx t0 t1 dt bc)
  (let* ((dx    (/ (- (inexact b) (inexact a)) (- nx 1)))
         (cf    (inexact c))
         (dtf   (inexact dt))
         (t0f   (inexact t0))
         (t1f   (inexact t1))
         (n     (vector-length u0))
         (u0f   (vec-inexact u0))
         (v0f   (vec-inexact v0))
         (lap0  (fd-laplacian-1d u0f dx bc))
         (c2dt2 (* cf cf dtf dtf))
         (u-ghost (make-vector n 0.0)))
    ;; Second-order ghost: u^{-1} = u0 - dt·v0 + (c²dt²/2)·Δu0
    (do ((i 0 (+ i 1))) ((= i n))
      (vector-set! u-ghost i
        (+ (vector-ref u0f i)
           (- (* dtf (vector-ref v0f i)))
           (* 0.5 c2dt2 (vector-ref lap0 i)))))
    (bc-enforce! u-ghost bc)
    (bc-enforce! u0f bc)
    (let loop ((t      t0f)
               (u-prev u-ghost)
               (u-curr u0f)
               (acc    (list (cons t0f (vector-copy u0f)))))
      (if (>= t t1f)
          (reverse acc)
          (let* ((lap    (fd-laplacian-1d u-curr dx bc))
                 (u-next (make-vector n 0.0))
                 (t-next (+ t dtf)))
            (do ((i 0 (+ i 1))) ((= i n))
              (vector-set! u-next i
                (+ (* 2.0 (vector-ref u-curr i))
                   (- (vector-ref u-prev i))
                   (* c2dt2 (vector-ref lap i)))))
            (bc-enforce! u-next bc)
            (loop t-next u-curr u-next
                  (cons (cons t-next (vector-copy u-next)) acc)))))))

(define (pde-wave c u0 v0 a b nx t0 t1 dt bc)
  (cdr (car (reverse (pde-wave/steps c u0 v0 a b nx t0 t1 dt bc)))))

;;;; ═══════════════════════════════════════════════════════════
;;;; § 10  POISSON 1D:  u_xx = f   (Thomas tridiagonal)
;;;;
;;;; Dirichlet BCs only.
;;;; f: procedure (x → number) or vector of length nx (values at
;;;;    all grid points; boundary entries are ignored).
;;;; x-vec: grid from pde-linspace.
;;;; Returns full solution vector of length nx.
;;;; ═══════════════════════════════════════════════════════════

(define (pde-poisson-1d f x-vec bc)
  (unless (eq? (bc-type bc) 'dirichlet)
    (error "pde-poisson-1d: requires Dirichlet boundary conditions"))
  (let* ((n     (vector-length x-vec))
         (left  (cadr  bc))
         (right (caddr bc))
         (dx    (- (vector-ref x-vec 1) (vector-ref x-vec 0)))
         (dx2   (* dx dx))
         (m     (- n 2))
         (fv    (make-vector m 0.0))
         (sol   (make-vector n 0.0)))
    ;; Evaluate f at interior grid points
    (do ((j 0 (+ j 1))) ((= j m))
      (let ((xi (vector-ref x-vec (+ j 1))))
        (vector-set! fv j
          (inexact (if (procedure? f)
                       (f xi)
                       (vector-ref f (+ j 1)))))))
    ;; Thomas forward sweep for tridiagonal [-2, 1, 1] system
    ;;   sub-diag=1, diag=-2, super-diag=1
    ;;   rhs[j] = dx²·f[j] - boundary corrections
    (let ((c* (make-vector m 0.0))
          (d* (make-vector m 0.0)))
      (let ((rhs (make-vector m 0.0)))
        (do ((j 0 (+ j 1))) ((= j m))
          (vector-set! rhs j
            (- (* dx2 (vector-ref fv j))
               (if (= j 0)       left  0.0)
               (if (= j (- m 1)) right 0.0))))
        ;; First row: a[0]=0, b[0]=-2, c[0]=1
        (vector-set! c* 0 (/ 1.0 -2.0))
        (vector-set! d* 0 (/ (vector-ref rhs 0) -2.0))
        (do ((j 1 (+ j 1))) ((= j m))
          (let* ((denom (- -2.0 (vector-ref c* (- j 1))))
                 (cj    (/ 1.0 denom))
                 (dj    (/ (- (vector-ref rhs j)
                              (vector-ref d* (- j 1)))
                           denom)))
            (vector-set! c* j cj)
            (vector-set! d* j dj)))
        ;; Back substitution
        (let ((interior (make-vector m 0.0)))
          (vector-set! interior (- m 1) (vector-ref d* (- m 1)))
          (do ((j (- m 2) (- j 1))) ((< j 0))
            (vector-set! interior j
              (- (vector-ref d* j)
                 (* (vector-ref c* j)
                    (vector-ref interior (+ j 1))))))
          ;; Assemble with boundary values
          (vector-set! sol 0 left)
          (do ((j 0 (+ j 1))) ((= j m))
            (vector-set! sol (+ j 1) (vector-ref interior j)))
          (vector-set! sol (- n 1) right))))
    sol))
