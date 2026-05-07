;;; examples/mcp_math.scm — Symbolic CAS + numeric tower as an MCP server
;;;
;;; Tools:
;;;   diff        — symbolic differentiation: (∂ expr var)
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
;;;   ./build/curry examples/mcp_math.scm
;;;
;;; Claude Code config (~/.claude.json):
;;;   { "mcpServers": { "curry-math": {
;;;       "command": "/path/to/build/curry",
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


;;; ---- Resource ----

(mcp-resource "math://help"
  "Quick reference: available symbolic variables and expression syntax"
  (lambda (uri)
    (mcp-text
      "Symbolic variables (pre-declared): x y z t u v w a b c n k r s p q

Expression syntax (standard Scheme):
  arithmetic : (+ a b)  (* a b)  (- a b)  (/ a b)  (expt a n)
  constants  : 0  1  1/3  3.14  (write exact fractions as rationals)
  composition: (+ (* x x) (* 2 x) 1)  means x² + 2x + 1

Tool examples:
  diff       : {\"expression\": \"(expt x 3)\",        \"variable\": \"x\"}
  substitute : {\"expression\": \"(* x x)\",           \"variable\": \"x\",  \"value\": \"(+ y 1)\"}
  evaluate   : {\"expression\": \"(+ (* x x) y)\",     \"bindings\": \"((x . 3) (y . 4))\"}
  auto-diff  : {\"function\":   \"(lambda (x) (/ 1 x))\", \"point\": 2.0}
  taylor     : {\"expression\": \"(/ 1 (- 1 x))\",     \"variable\": \"x\",  \"order\": 5}")))


(mcp-serve "curry-math" "0.7.2")
