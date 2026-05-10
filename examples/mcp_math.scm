;;; examples/mcp_math.scm — Symbolic CAS + numeric tower as an MCP server
;;;
;;; Tools:
;;;   diff        — symbolic differentiation: (∂ expr var)
;;;   integrate   — symbolic antiderivative / definite integral: (∫ expr var [a b])
;;;   frac-diff   — Caputo fractional derivative D^α[expr] wrt var
;;;   frac-int    — Riemann-Liouville fractional integral I^α[expr] wrt var
;;;   wirtinger   — Wirtinger derivatives ∂/∂z and ∂/∂z̄ for complex analysis
;;;   simplify    — algebraic simplification
;;;   substitute  — replace a variable with a value or expression
;;;   evaluate    — substitute multiple variables, reduce to a number
;;;   auto-diff   — numerical derivative via dual-number AD
;;;   taylor      — Taylor series expansion around a point
;;;
;;; Variable names x y z t u v w a b c n k r s p q are pre-declared symbolic.
;;; Write expressions in standard Scheme notation: (* x x), (expt x 3), (+ x y).
;;;
;;; Usage:
;;;   ./build-release/curry examples/mcp_math.scm
;;;
;;; Claude Code config (~/.claude.json):
;;;   { "mcpServers": { "curry-math": {
;;;       "command": "/path/to/build-release/curry",
;;;       "args":    ["/path/to/examples/mcp_math.scm"] } } }

(import (curry mcp))

;;; ---- Pre-declare symbolic variables ----
;;; After this, names like x, y, (expt x 2) etc. evaluate to symbolic objects.

(symbolic x y z t u v w a b c n k r s p q)


;;; ---- Helpers ----

(define (arg args name)
  (let ((p (assq name args)))
    (if p (cdr p) (error "missing argument" name))))

(define (arg? args name default)
  (let ((p (assq name args)))
    (if p (cdr p) default)))

;;; Parse a Scheme expression string and evaluate it in the current (symbolic) env.
(define (parse-expr str)
  (eval (read (open-input-string str))))

;;; Resolve a variable name string to its symbolic object.
(define (parse-var str)
  (let ((sym (string->symbol str)))
    (if (sym-var? (eval sym))
        (eval sym)
        (error (string-append "not a declared symbolic variable: " str)
               "use one of: x y z t u v w a b c n k r s p q"))))

;;; Format any Scheme value as a string.
(define (->str v)
  (let ((port (open-output-string)))
    (write v port)
    (get-output-string port)))

;;; Sum a list of values with +.
(define (sum-list lst)
  (if (null? lst) 0 (+ (car lst) (sum-list (cdr lst)))))


;;; ---- Tools ----

(mcp-tool "diff"
  "Symbolically differentiate an expression with respect to a variable.
Pre-declared variables: x y z t u v w a b c n k r s p q
Examples:
  diff(\"(* x x)\", \"x\")         -> (+ x x)   [= 2x]
  diff(\"(expt x 3)\", \"x\")      -> (* 3 (expt x 2))
  diff(\"(+ (* x x) (* 2 y))\", \"x\") -> (+ x x)"
  '((expression . ((type . "string") (description . "Scheme expression")))
    (variable   . ((type . "string") (description . "Variable to differentiate with respect to"))))
  (lambda (args)
    (let* ((expr   (parse-expr (arg args 'expression)))
           (var    (parse-var  (arg args 'variable)))
           (result (simplify (∂ expr var))))
      (mcp-text (->str result)))))


(mcp-tool "integrate"
  "Compute the symbolic antiderivative of an expression, or a definite integral.
Indefinite: integrate(expr, var) -> antiderivative F(x) such that dF/dx = f
Definite:   integrate(expr, var, lo, hi) -> F(hi) - F(lo)
Pre-declared variables: x y z t u v w a b c n k r s p q
Examples:
  integrate(\"(expt x 3)\", \"x\")              -> (/ (expt x 4) 4)
  integrate(\"(sin x)\", \"x\")                 -> (/ (- (cos x)) 1)
  integrate(\"(expt x 2)\", \"x\", 0, 3)        -> 9
  integrate(\"(+ (* 2 x) 1)\", \"x\", -1, 1)   -> 2"
  '((expression . ((type . "string") (description . "Scheme expression to integrate")))
    (variable   . ((type . "string") (description . "Integration variable")))
    (lo         . ((type . "number") (description . "Lower bound for definite integral") (default . "none")))
    (hi         . ((type . "number") (description . "Upper bound for definite integral") (default . "none"))))
  (lambda (args)
    (let* ((expr (parse-expr (arg args 'expression)))
           (var  (parse-var  (arg args 'variable)))
           (lo   (arg? args 'lo #f))
           (hi   (arg? args 'hi #f)))
      (if (and lo hi)
          (let* ((anti (∫ expr var))
                 (Fa   (simplify (substitute anti var lo)))
                 (Fb   (simplify (substitute anti var hi))))
            (mcp-text (->str (simplify (- Fb Fa)))))
          (mcp-text (->str (simplify (∫ expr var))))))))


(mcp-tool "frac-diff"
  "Compute the Caputo fractional derivative D^α[f(x)] of order α with respect to a variable.
For non-integer α this interpolates smoothly between f (α=0), f' (α=1), f'' (α=2), etc.
Rules: power law x^n → Γ(n+1)/Γ(n-α+1)·x^(n-α), exponential eigenfunction e^(λx) → λ^α·e^(λx),
linearity, composition D^α∘D^β = D^(α+β). Constants give 0 (Caputo convention).
Pre-declared variables: x y z t u v w a b c n k r s p q
Examples:
  frac-diff(\"(expt x 2)\", 0.5, \"x\") -> (* 1.50451 (expt x 1.5))
  frac-diff(\"(exp x)\", 0.5, \"x\")    -> (exp x)    [eigenfunction: 1^0.5 * eˣ]
  frac-diff(\"(expt x 3)\", 1, \"x\")   -> (* 3 (expt x 2))  [integer α = ordinary d/dx]
  frac-diff(\"(+ (expt x 2) x)\", 0.5, \"x\") -> sum of two power terms"
  '((expression . ((type . "string") (description . "Scheme expression")))
    (alpha      . ((type . "number") (description . "Fractional order α (may be non-integer, 0 ≤ α ≤ 20)")))
    (variable   . ((type . "string") (description . "Variable to differentiate with respect to"))))
  (lambda (args)
    (let* ((expr   (parse-expr (arg args 'expression)))
           (alpha  (exact->inexact (arg args 'alpha)))
           (var    (parse-var  (arg args 'variable)))
           (result (simplify (frac-diff expr alpha var))))
      (mcp-text (->str result)))))


(mcp-tool "frac-int"
  "Compute the Riemann-Liouville fractional integral I^α[f(x)] of order α.
For non-integer α this generalises repeated integration: I^1 = ∫, I^2 = ∬, etc.
Rules: power law x^n → Γ(n+1)/Γ(n+α+1)·x^(n+α), exponential e^(λx) → λ^(-α)·e^(λx),
linearity, constants c → c·x^α/Γ(α+1).
Supports definite form: I^α[f](b) - I^α[f](a).
Pre-declared variables: x y z t u v w a b c n k r s p q
Examples:
  frac-int(\"x\", 0.5, \"x\")           -> (* 0.752253 (expt x 1.5))
  frac-int(\"(expt x 2)\", 0.5, \"x\") -> (* 0.601802 (expt x 2.5))
  frac-int(\"(exp x)\", 0.5, \"x\")    -> (* 1.0 (exp x))  [λ=1: 1^(-0.5)·eˣ... wait, 1^-0.5=1]
  frac-int(\"(expt x 2)\", 0.5, \"x\", 0, 1) -> definite value"
  '((expression . ((type . "string") (description . "Scheme expression")))
    (alpha      . ((type . "number") (description . "Fractional order α (positive)")))
    (variable   . ((type . "string") (description . "Integration variable")))
    (lo         . ((type . "number") (description . "Lower bound for definite form") (default . "none")))
    (hi         . ((type . "number") (description . "Upper bound for definite form") (default . "none"))))
  (lambda (args)
    (let* ((expr  (parse-expr (arg args 'expression)))
           (alpha (exact->inexact (arg args 'alpha)))
           (var   (parse-var  (arg args 'variable)))
           (lo    (arg? args 'lo #f))
           (hi    (arg? args 'hi #f))
           (anti  (simplify (frac-int expr alpha var))))
      (if (and lo hi)
          (let* ((Fa (simplify (substitute anti var lo)))
                 (Fb (simplify (substitute anti var hi))))
            (mcp-text (->str (simplify (- Fb Fa)))))
          (mcp-text (->str anti))))))


(mcp-tool "wirtinger"
  "Compute Wirtinger derivatives ∂/∂z or ∂/∂z̄ for complex analysis.
Treats z and conj(z) as independent variables (Wirtinger calculus).
A function is holomorphic iff its ∂/∂z̄ derivative is zero.
direction: \"d\" for ∂/∂z (holomorphic part), \"dbar\" for ∂/∂z̄ (anti-holomorphic).
Rules: ∂z/∂z=1, ∂conj(z)/∂z=0; ∂conj(f)/∂z = conj(∂f/∂z̄); arithmetic chain rule.
Pre-declared variables: x y z t u v w a b c n k r s p q
Examples:
  wirtinger(\"(* z z)\", \"z\", \"d\")           -> (* 2 z)   [holomorphic]
  wirtinger(\"(expt z 3)\", \"z\", \"d\")        -> (* 3 (expt z 2))
  wirtinger(\"(conj z)\", \"z\", \"d\")          -> 0        [conj not holomorphic]
  wirtinger(\"(conj z)\", \"z\", \"dbar\")       -> 1
  wirtinger(\"(+ (* z z) (conj z))\", \"z\", \"dbar\") -> 1  [non-holomorphic part]"
  '((expression . ((type . "string") (description . "Scheme expression (may use conj, real-part, imag-part)")))
    (variable   . ((type . "string") (description . "Complex variable name")))
    (direction  . ((type . "string") (description . "\"d\" for ∂/∂z, \"dbar\" for ∂/∂z̄"))))
  (lambda (args)
    (let* ((expr (parse-expr (arg args 'expression)))
           (var  (parse-var  (arg args 'variable)))
           (dir  (arg args 'direction))
           (result (simplify
                     (if (equal? dir "dbar")
                         (wirtinger-dbar expr var)
                         (wirtinger-d    expr var)))))
      (mcp-text (->str result)))))


(mcp-tool "simplify"
  "Apply algebraic simplification rules to an expression.
Handles: x+0=x, x*1=x, x*0=0, x-x=0, and similar identities.
Example: simplify(\"(+ (* x 1) 0)\") -> x"
  '((expression . ((type . "string") (description . "Scheme expression to simplify"))))
  (lambda (args)
    (mcp-text (->str (simplify (parse-expr (arg args 'expression)))))))


(mcp-tool "substitute"
  "Replace a variable with a value or another expression.
Examples:
  substitute(\"(* x x)\", \"x\", \"3\")        -> 9
  substitute(\"(+ x y)\", \"x\", \"(* 2 z)\") -> (+ (* 2 z) y)"
  '((expression . ((type . "string") (description . "Scheme expression")))
    (variable   . ((type . "string") (description . "Variable to replace")))
    (value      . ((type . "string") (description . "Replacement value or expression"))))
  (lambda (args)
    (let* ((expr   (parse-expr (arg args 'expression)))
           (var    (parse-var  (arg args 'variable)))
           (val    (parse-expr (arg args 'value)))
           (result (simplify (substitute expr var val))))
      (mcp-text (->str result)))))


(mcp-tool "evaluate"
  "Evaluate a symbolic expression by substituting variables with numeric values.
Bindings are given as a Scheme association list.
Example:
  evaluate(\"(+ (* x x) y)\", \"((x . 3) (y . 4))\") -> 13"
  '((expression . ((type . "string") (description . "Symbolic Scheme expression")))
    (bindings   . ((type . "string") (description . "Alist of (variable . number), e.g. ((x . 2) (y . 3))"))))
  (lambda (args)
    (let* ((expr (parse-expr (arg args 'expression)))
           (binds (read (open-input-string (arg args 'bindings)))))
      (let ((result (let loop ((e expr) (bs binds))
                      (if (null? bs)
                          e
                          (let* ((pair (car bs))
                                 (var  (parse-var (symbol->string (car pair))))
                                 (val  (cdr pair)))
                            (loop (simplify (substitute e var val)) (cdr bs)))))))
        (mcp-text (->str result))))))


(mcp-tool "auto-diff"
  "Compute the numerical derivative of a function at a point using dual-number
automatic differentiation. No symbolic expression needed — works on any lambda.
Examples:
  auto-diff(\"(lambda (x) (* x x x))\", 4.0) -> 48   [3x² at x=4]
  auto-diff(\"(lambda (t) (+ (* t t) t))\",  2.0) -> 5    [2t+1 at t=2]"
  '((function . ((type . "string") (description . "Lambda expression, e.g. (lambda (x) (* x x))")))
    (point    . ((type . "number") (description . "Point at which to evaluate f'(x)"))))
  (lambda (args)
    (let* ((f  (parse-expr (arg args 'function)))
           (x0 (exact->inexact (arg args 'point))))
      (mcp-text (->str (auto-diff f x0))))))


(mcp-tool "taylor"
  "Compute the Taylor series of an expression around a point, to a given order.
Uses repeated symbolic differentiation: term_k = f^(k)(a) * (x-a)^k / k!
Works for any expression that can be differentiated symbolically.
Examples:
  taylor(\"(expt x 4)\", \"x\", 0, 5) -> (+ (* 4 (expt x 3)) (* 6 (* x x)) (* 4 x))
  taylor(\"(/ 1 (- 1 x))\", \"x\", 0, 3) -> (+ 1 x (* x x) (expt x 3))"
  '((expression . ((type . "string")  (description . "Scheme expression in the expansion variable")))
    (variable   . ((type . "string")  (description . "Expansion variable")))
    (point      . ((type . "number")  (description . "Expansion point a (Taylor series around x=a)") (default . 0)))
    (order      . ((type . "integer") (description . "Maximum term degree")                           (default . 4))))
  (lambda (args)
    (let* ((expr  (parse-expr (arg args 'expression)))
           (var   (parse-var  (arg args 'variable)))
           (a     (arg? args 'point 0))
           (n     (arg? args 'order 4))
           ; Build terms: f^(k)(a) * (x-a)^k / k!
           (terms (let loop ((k 0) (df expr) (kfact 1) (acc '()))
                    (if (> k n)
                        (reverse acc)
                        (let* ((dk   (substitute df var a))
                               (term (* dk (/ (expt (- var a) k) kfact))))
                          (loop (+ k 1)
                                (∂ df var)
                                (* kfact (+ k 1))
                                (cons term acc)))))))
      (mcp-text (->str (simplify (sum-list terms)))))))


(mcp-tool "quad"
  "Numerically integrate a function over [a, b] using adaptive Gauss-Kronrod G7K15 quadrature.
Works for any Scheme lambda (not just symbolically differentiable functions).
Handles smooth functions, mild singularities, and oscillatory integrands.
Optional tol: absolute error tolerance (default 1e-8).
Examples:
  quad(\"(lambda (x) (* x x))\", 0, 1)              -> 0.333333   [= 1/3]
  quad(\"(lambda (x) (sin x))\", 0, 3.14159)        -> 2.0
  quad(\"(lambda (x) (exp (* -1 (* x x))))\", -5, 5) -> 1.7725  [≈ √π]
  quad(\"(lambda (x) (/ 1 (+ 1 (* x x))))\", 0, 1)  -> 0.785398  [= π/4]"
  '((function . ((type . "string") (description . "Lambda: (lambda (x) ...)")))
    (lo       . ((type . "number") (description . "Lower bound")))
    (hi       . ((type . "number") (description . "Upper bound")))
    (tol      . ((type . "number") (description . "Error tolerance") (default . 1e-8))))
  (lambda (args)
    (let* ((f   (parse-expr (arg args 'function)))
           (a   (exact->inexact (arg args 'lo)))
           (b   (exact->inexact (arg args 'hi)))
           (tol (arg? args 'tol 1e-8)))
      (mcp-text (->str (quad f a b (exact->inexact tol)))))))


(mcp-tool "quad-frac-diff"
  "Numerical Grünwald-Letnikov fractional derivative D^α f(x₀) for any function f.
Use this when the symbolic frac-diff returns an unevaluated node (e.g. for sin, cos).
D^α f(x) ≈ h^{-α} Σ_{k=0}^{N} w_k f(x - k·h)  where h = x/N.
Optional n: number of discretisation steps (default 500; larger = more accurate but slower).
Examples:
  quad-frac-diff(\"sin\", 0.5, 1.0)                        -> 0.846 (D^0.5[sin] at x=1)
  quad-frac-diff(\"(lambda (x) (* x x))\", 0.5, 1.0)       -> ≈1.504  [matches symbolic]
  quad-frac-diff(\"(lambda (x) (exp x))\", 0.5, 1.0)        -> 2.718  [e^x eigenfunction]"
  '((function . ((type . "string") (description . "Lambda or named function (sin, cos, exp, …)")))
    (alpha    . ((type . "number") (description . "Fractional order α")))
    (point    . ((type . "number") (description . "Evaluation point x₀ > 0")))
    (n        . ((type . "integer") (description . "Discretisation steps") (default . 500))))
  (lambda (args)
    (let* ((f     (parse-expr (arg args 'function)))
           (alpha (exact->inexact (arg args 'alpha)))
           (x0    (exact->inexact (arg args 'point)))
           (n     (arg? args 'n 500)))
      (mcp-text (->str (quad-frac-diff f alpha x0 n))))))


(mcp-tool "quad-frac-int"
  "Numerical Riemann-Liouville fractional integral I^α f(x₀) for any function f.
I^α f(x) = (1/Γ(α)) ∫₀ˣ (x-t)^{α-1} f(t) dt
Uses a singularity-free substitution so the kernel (x-t)^{α-1} is handled exactly.
Use this when symbolic frac-int returns an unevaluated node (e.g. for sin, cos).
Optional nsub: number of G7K15 sub-intervals (default 32).
Examples:
  quad-frac-int(\"sin\", 0.5, 1.0)                   -> 0.6697  (I^0.5[sin] at x=1)
  quad-frac-int(\"(lambda (x) x)\", 0.5, 1.0)        -> 0.7523  [matches symbolic Γ(2)/Γ(2.5)]
  quad-frac-int(\"(lambda (x) (* x x))\", 0.5, 2.0)  -> exact power-law result"
  '((function . ((type . "string") (description . "Lambda or named function")))
    (alpha    . ((type . "number") (description . "Fractional order α > 0")))
    (point    . ((type . "number") (description . "Upper limit x₀ > 0")))
    (nsub     . ((type . "integer") (description . "G7K15 sub-intervals") (default . 32))))
  (lambda (args)
    (let* ((f     (parse-expr (arg args 'function)))
           (alpha (exact->inexact (arg args 'alpha)))
           (x0    (exact->inexact (arg args 'point)))
           (nsub  (arg? args 'nsub 32)))
      (mcp-text (->str (quad-frac-int f alpha x0 nsub))))))


;;; ---- Resource ----

(mcp-resource "math://help"
  "Quick reference: available tools, symbolic variables, and expression syntax"
  (lambda (uri)
    (mcp-text
      "Symbolic variables (pre-declared): x y z t u v w a b c n k r s p q

Expression syntax (standard Scheme):
  arithmetic  : (+ a b)  (* a b)  (- a b)  (/ a b)  (expt a n)
  trig/exp    : (sin x)  (cos x)  (tan x)  (exp x)  (log x)  (sqrt x)
  complex     : (conj z)  (real-part z)  (imag-part z)
  constants   : 0  1  1/3  3.14  (exact fractions as rationals)
  composition : (+ (* x x) (* 2 x) 1)  means x² + 2x + 1

Symbolic tools:
  diff          : {\"expression\": \"(expt x 3)\",          \"variable\": \"x\"}
  integrate     : {\"expression\": \"(expt x 3)\",          \"variable\": \"x\"}
  integrate     : {\"expression\": \"(expt x 2)\",          \"variable\": \"x\",  \"lo\": 0, \"hi\": 1}
  frac-diff     : {\"expression\": \"(expt x 2)\",          \"alpha\": 0.5,       \"variable\": \"x\"}
  frac-int      : {\"expression\": \"x\",                   \"alpha\": 0.5,       \"variable\": \"x\"}
  wirtinger     : {\"expression\": \"(* z z)\",             \"variable\": \"z\",  \"direction\": \"d\"}
  substitute    : {\"expression\": \"(* x x)\",             \"variable\": \"x\",  \"value\": \"(+ y 1)\"}
  evaluate      : {\"expression\": \"(+ (* x x) y)\",       \"bindings\": \"((x . 3) (y . 4))\"}
  auto-diff     : {\"function\":   \"(lambda (x) (/ 1 x))\",  \"point\": 2.0}
  taylor        : {\"expression\": \"(/ 1 (- 1 x))\",       \"variable\": \"x\",  \"order\": 5}
  simplify      : {\"expression\": \"(+ (* x 1) 0)\"}

Numerical tools (work for any lambda, including sin/cos/exp):
  quad          : {\"function\": \"(lambda (x) (sin x))\",  \"lo\": 0, \"hi\": 3.14159}
  quad-frac-diff: {\"function\": \"sin\",  \"alpha\": 0.5, \"point\": 1.0}
  quad-frac-int : {\"function\": \"sin\",  \"alpha\": 0.5, \"point\": 1.0}

Fractional calculus notes:
  D^α[xⁿ] = Γ(n+1)/Γ(n−α+1) · x^(n−α)   (Caputo; constants → 0)
  D^α[eˡˣ] = λ^α · eˡˣ                    (eigenfunction property)
  I^α[xⁿ] = Γ(n+1)/Γ(n+α+1) · x^(n+α)
  Composition: D^α ∘ D^β = D^(α+β)
  α=0 → identity, α=1 → d/dx, α=2 → d²/dx²
  For sin/cos/arbitrary f: use quad-frac-diff / quad-frac-int")))


(mcp-serve "curry-math" "0.7.4")
