;;; Phase 8 PDE prerequisites: symbolic functions + integral transforms
;;;
;;; Tests: T_SYMFN construction/predicates, chain-rule differentiation,
;;; Laplace transform (table + derivative property), inverse Laplace,
;;; Fourier transform (derivative property), and worked PDE examples.

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-infix label expr expected-str)
  (let ((got (sym->infix expr)))
    (if (equal? got expected-str)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " got \"") (display got)
               (display "\" expected \"") (display expected-str)
               (display "\"") (newline)
               (set! fail (+ fail 1))))))

(define (check-infix-contains label expr sub-str)
  (let ((got (sym->infix expr)))
    (define (contains? str)
      (let ((slen (string-length str))
            (sublen (string-length sub-str)))
        (let loop ((i 0))
          (cond ((> (+ i sublen) slen) #f)
                ((string=? (string-copy str i (+ i sublen)) sub-str) #t)
                (else (loop (+ i 1)))))))
    (if (contains? got)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " got \"") (display got)
               (display "\" expected to contain \"") (display sub-str)
               (display "\"") (newline)
               (set! fail (+ fail 1))))))

;;; =========================================================================
;;; Symbolic variables and functions
;;; (Note: "u" is an Akkadian keyword → use "phi", "psi", etc.)
;;; =========================================================================

(define x     (sym-var 'x))
(define y     (sym-var 'y))
(define t     (sym-var 't))
(define s     (sym-var 's))
(define w     (sym-var 'w))
(define alpha (sym-var 'alpha))
(define c-v   (sym-var 'c))

(define phi  (sym-fn 'phi x t))      ; phi(x,t) — heat/wave unknown
(define psi  (sym-fn 'psi x y t))    ; psi(x,y,t) — 2D unknown
(define gg   (sym-fn 'g))            ; g — no declared params

;;; =========================================================================
;;; T_SYMFN — construction and predicates
;;; =========================================================================

(check "sym-fn? sym-fn"     (sym-fn? phi)      #t)
(check "sym-fn? sym-var"    (sym-fn? x)         #f)
(check "sym-fn? number"     (sym-fn? 42)        #f)
(check "sym-fn-name phi"    (sym-fn-name phi)   'phi)
(check "sym-fn-name psi"    (sym-fn-name psi)   'psi)
(check "sym-fn-name g"      (sym-fn-name gg)    'g)

;;; =========================================================================
;;; Application — fn-apply and direct call syntax
;;; =========================================================================

(define phi-xt  (fn-apply phi x t))
(define psi-xyt (fn-apply psi x y t))

(check "symbolic? apply"     (symbolic? phi-xt)   #t)
(check "sym-expr? apply"     (sym-expr?  phi-xt)  #t)
(check-infix "fn-apply phi(x,t)"   phi-xt   "phi(x, t)")
(check-infix "fn-apply psi(x,y,t)" psi-xyt  "psi(x, y, t)")

;;; Direct call syntax creates the same node
(check "direct call = fn-apply" (equal? (phi x t) phi-xt) #t)

;;; =========================================================================
;;; Differentiation — chain rule over arguments
;;; =========================================================================

(check-infix "∂phi/∂t"       (∂ (phi x t) t)                "phi_t(x, t)")
(check-infix "∂phi/∂x"       (∂ (phi x t) x)                "phi_x(x, t)")
(check-infix "∂²phi/∂t²"     (∂ (∂ (phi x t) t) t)          "phi_t_t(x, t)")
(check-infix "∂²phi/∂x²"     (∂ (∂ (phi x t) x) x)          "phi_x_x(x, t)")
(check-infix "∂²phi/∂x∂t"    (∂ (∂ (phi x t) x) t)          "phi_x_t(x, t)")
(check-infix "∂psi/∂y"       (∂ (psi x y t) y)              "psi_y(x, y, t)")

;;; No-dependency: ∂phi(x,t)/∂y = 0
(check "∂phi(x,t)/∂y = 0"    (∂ (phi x t) y) 0)

;;; Chain rule: ∂/∂x phi(2x, t) = 2·phi_x(2x, t)
(check-infix "chain ∂phi(2x,t)/∂x"
             (simplify (∂ (phi (* 2 x) t) x))
             "2 * phi_x(2 * x, t)")

;;; =========================================================================
;;; Laplace transform — standard table
;;; =========================================================================

(check-infix "L{3}"          (laplace 3              t s)  "3/s")
(check-infix "L{alpha}"      (laplace alpha           t s)  "alpha/s")
(check-infix "L{t}"          (laplace t               t s)  "1/s^2")
(check-infix "L{t^2}"        (laplace (expt t 2)      t s)  "2/s^3")
(check-infix "L{t^3}"        (laplace (expt t 3)      t s)  "6/s^4")
(check-infix "L{e^t}"        (laplace (exp t)         t s)  "1/(s - 1)")
(check-infix "L{e^{at}}"     (laplace (exp (* alpha t)) t s)  "1/(s - alpha)")
(check-infix "L{sin(t)}"     (laplace (sin t)         t s)  "1/(1 + s^2)")
(check-infix "L{cos(t)}"     (laplace (cos t)         t s)  "s/(1 + s^2)")
(check-infix "L{sin(3t)}"    (laplace (sin (* 3 t))   t s)  "3/(9 + s^2)")
(check-infix "L{cos(2t)}"    (laplace (cos (* 2 t))   t s)  "s/(4 + s^2)")
(check-infix "L{sinh(t)}"    (laplace (sinh t)         t s)  "1/(s^2 - 1)")
(check-infix "L{cosh(t)}"    (laplace (cosh t)         t s)  "s/(s^2 - 1)")

;;; Linearity
(check "L{f+g} symbolic"
       (symbolic? (laplace (+ (sin t) (exp t)) t s)) #t)
(check-infix-contains "L{3·e^t} contains 1/(s-1)"
       (laplace (* 3 (exp t)) t s)  "1/(s - 1)")

;;; =========================================================================
;;; Laplace — symbolic function objects and derivative property
;;; =========================================================================

;;; L{phi(x,t)} = L_phi(x,s)
(check-infix "L{phi(x,t)}"
             (laplace (fn-apply phi x t) t s)
             "L_phi(x, s)")

;;; L{phi_t(x,t)} = s·L_phi(x,s) − phi(x,0)
(check-infix "L{phi_t}"
             (laplace (∂ (phi x t) t) t s)
             "s * L_phi(x, s) - phi(x, 0)")

;;; L{phi_x(x,t)} — x-derivative unaffected by t-transform
(check-infix "L{phi_x}"
             (laplace (∂ (phi x t) x) t s)
             "L_phi_x(x, s)")

;;; L{phi_xx(x,t)}
(check-infix "L{phi_xx}"
             (laplace (∂ (∂ (phi x t) x) x) t s)
             "L_phi_x_x(x, s)")

;;; L{α·phi_xx} — constant factor passes through
(check-infix "L{α·phi_xx}"
             (laplace (* alpha (∂ (∂ (phi x t) x) x)) t s)
             "alpha * L_phi_x_x(x, s)")

;;; =========================================================================
;;; Inverse Laplace — table
;;; =========================================================================

(check-infix "L⁻¹{1/s}"      (ilaplace (/ 1 s)            s t)  "1")
(check-infix "L⁻¹{1/s²}"     (ilaplace (/ 1 (expt s 2))   s t)  "t")
(check-infix "L⁻¹{1/s³}"     (ilaplace (/ 1 (expt s 3))   s t)  "1/2 * t^2")
(check-infix "L⁻¹{1/(s-1)}"  (ilaplace (/ 1 (- s 1))      s t)  "exp(t)")
(check-infix "L⁻¹{1/(s-a)}"  (ilaplace (/ 1 (- s alpha))  s t)  "exp(alpha * t)")

;;; Round-trips
(check-infix "round-trip sin(t)"
             (ilaplace (laplace (sin t) t s) s t)   "sin(t)")
(check-infix "round-trip cos(t)"
             (ilaplace (laplace (cos t) t s) s t)   "cos(t)")
(check-infix "round-trip e^t"
             (ilaplace (laplace (exp t)  t s) s t)  "exp(t)")
(check-infix "round-trip sin(3t)"
             (ilaplace (laplace (sin (* 3 t)) t s) s t)  "sin(3 * t)")

;;; =========================================================================
;;; Fourier transform
;;; =========================================================================

;;; F{phi(x,t)} in t → F_phi(x,w)
(check-infix "F{phi(x,t)}"
             (fourier (fn-apply phi x t) t w)   "F_phi(x, w)")

;;; F{phi_x(x,t)} in t — x-derivative unaffected
(check-infix "F{phi_x}"
             (fourier (∂ (phi x t) x) t w)     "F_phi_x(x, w)")

;;; F{phi_xx(x,t)} in t — x-derivative unaffected
(check-infix "F{phi_xx}"
             (fourier (∂ (∂ (phi x t) x) x) t w)  "F_phi_x_x(x, w)")

;;; F{phi_t(x,t)} in t → iw·F_phi(x,w)  (derivative property)
(check-infix-contains "F{phi_t} contains F_phi"
             (fourier (∂ (phi x t) t) t w)  "F_phi")
(check-infix "F{phi_t} = iw·F_phi"
             (fourier (∂ (phi x t) t) t w)  "0+1i * w * F_phi(x, w)")

;;; Linearity
(check "F{phi+phi} symbolic"
       (symbolic? (simplify (fourier (+ (phi x t) (phi x t)) t w)))  #t)

;;; =========================================================================
;;; Heat equation:  phi_t = α·phi_xx
;;; Laplace in t:   s·Φ(x,s) − phi(x,0) = α·Φ_xx(x,s)
;;; =========================================================================

(let* ((lhs (laplace (∂ (phi x t) t) t s))
       (rhs (laplace (* alpha (∂ (∂ (phi x t) x) x)) t s)))
  (check-infix "heat eq LHS"   lhs  "s * L_phi(x, s) - phi(x, 0)")
  (check-infix "heat eq RHS"   rhs  "alpha * L_phi_x_x(x, s)"))

;;; =========================================================================
;;; Wave equation:  phi_tt = c²·phi_xx
;;; L{phi_tt} = s·L{phi_t} − phi_t(x,0) = s·(s·Φ − phi(x,0)) − phi_t(x,0)
;;; =========================================================================

(let* ((phi-tt (∂ (∂ (phi x t) t) t))
       (L-tt   (laplace phi-tt t s)))
  (check "L{phi_tt} symbolic"  (symbolic? L-tt)  #t)
  ;; Unwrap one level: L{phi_tt} = s·L{phi_t} − phi_t(x,0)
  (check-infix "L{phi_tt}"  L-tt
               "s * L_phi_t(x, s) - phi_t(x, 0)"))

;;; =========================================================================
;;; Schrödinger (free particle):  i·phi_t = phi_xx  (in units where ℏ²/2m=1)
;;; Fourier in x transforms phi_xx to −k²·Φ(k,t); phi_t stays as Φ_t(k,t)
;;; =========================================================================

(let* ((kk     (sym-var 'k))
       (chi    (sym-fn 'chi x t))     ; chi = wave-function
       (F-chi-t   (fourier (∂ (chi x t) t) x kk))
       (F-chi-xx  (fourier (∂ (∂ (chi x t) x) x) x kk)))
  ;; Fourier in x: x-derivatives fire derivative property
  (check "F{chi_t,x} symbolic"   (symbolic? F-chi-t)  #t)
  (check "F{chi_xx,x} symbolic"  (symbolic? F-chi-xx) #t)
  ;; chi_t has d_param=t, t_var=x → simple case → F_chi_t(k,t)
  (check-infix "F{chi_t} in x"   F-chi-t  "F_chi_t(k, t)")
  ;; chi_xx has d_param=x, t_var=x → derivative property fires → iw*F{chi_x}
  (check-infix-contains "F{chi_xx} in x contains F_chi" F-chi-xx "F_chi"))

;;; =========================================================================
;;; Summary
;;; =========================================================================

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
