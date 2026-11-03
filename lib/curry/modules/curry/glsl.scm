;;; (curry glsl) — emit GLSL from symbolic CAS expressions
;;;
;;; Turns expressions built with curry's symbolic CAS (sym-var, ∂, simplify,
;;; ...) into GLSL source, so a formula derived symbolically in Scheme runs
;;; as-is in a gpu-* shader.  Two levels:
;;;
;;;   (sym->glsl expr var-map)
;;;       one expression -> one GLSL expression string.
;;;
;;;   (glsl-assignments targets var-map)
;;;       many expressions -> GLSL statements with common subexpressions
;;;       computed once into temporaries.  targets is a list of
;;;       (lvalue-string . expr); the result assigns each expr to its lvalue.
;;;       This matters for things like metric derivatives, where the same
;;;       sub-terms appear in dozens of components.
;;;
;;; var-map is an alist from sym-var NAME (a symbol) to the GLSL text that
;;; holds its value, e.g. '((t . "x.x") (r . "r") (M . "u.mass")).  A
;;; variable not in var-map raises an error, so a formula never silently
;;; refers to an undeclared GLSL name.  Unknown functions made with sym-fn
;;; can be mapped too: '((f . "my_glsl_function")).
;;;
;;; GLSL arithmetic is 32-bit float on most GPUs (and always on Metal).
;;; Integer powers up to 8 are expanded into multiplications (pow() is
;;; undefined for a negative base); x^(1/2) and x^(-1/2) become sqrt and
;;; inversesqrt.
;;;
;;; Usage:
;;;   (import (curry glsl))
;;;   (define r (sym-var 'r))
;;;   (sym->glsl (/ (* 2 (sym-var 'M)) r) '((r . "r") (M . "mass")))
;;;     ; => "((2.0 * mass) / r)"

(define-library (curry glsl)
  (import (scheme base) (scheme inexact) (scheme char) (scheme write))
  (export sym->glsl glsl-assignments glsl-float)
  (begin

    ;; curry's map runs in parallel above a small list length (see
    ;; map_par_threshold); the walks below update counters and hash tables
    ;; from inside the mapped procedure, so they must map sequentially.
    (define (smap f l)
      (let loop ((l l) (acc '()))
        (if (null? l) (reverse acc) (loop (cdr l) (cons (f (car l)) acc)))))

    (define (smap2 f a b)
      (let loop ((a a) (b b) (acc '()))
        (if (or (null? a) (null? b)) (reverse acc) (loop (cdr a) (cdr b) (cons (f (car a) (car b)) acc)))))

    ;; ---- numbers ----

    ;; A GLSL float literal for a real number (exact or inexact).
    (define (glsl-float x)
      (unless (and (number? x) (real? x))
        (error "sym->glsl: not a real number" x))
      (let ((v (inexact x)))
        (unless (and (= v v) (< (abs v) 3.4e38))
          (error "sym->glsl: number is NaN or out of float range" x))
        (let ((s (number->string v)))
          ;; "2." / "2" -> "2.0"; keep exponent forms such as "1e-10"
          (cond ((string-index-of s #\e) s)
                ((not (string-index-of s #\.)) (string-append s ".0"))
                ((char=? (string-ref s (- (string-length s) 1)) #\.) (string-append s "0"))
                (else s)))))

    (define (string-index-of s ch)
      (let loop ((i 0))
        (cond ((= i (string-length s)) #f)
              ((char=? (string-ref s i) ch) i)
              (else (loop (+ i 1))))))

    (define (wrap-negative s)
      (if (and (> (string-length s) 0) (char=? (string-ref s 0) #\-))
          (string-append "(" s ")")
          s))

    ;; ---- variables and functions ----

    (define (lookup-name name var-map what)
      (let ((e (assq name var-map)))
        (unless e
          (error (string-append "sym->glsl: no GLSL name given for " what) name))
        (unless (string? (cdr e))
          (error "sym->glsl: var-map values must be strings" e))
        (cdr e)))

    ;; ---- operators ----

    (define unary-functions
      '((sqrt . "sqrt") (sin . "sin") (cos . "cos") (tan . "tan") (exp . "exp")
        (log . "log") (abs . "abs") (sinh . "sinh") (cosh . "cosh") (tanh . "tanh")
        (asin . "asin") (acos . "acos") (atan . "atan") (asinh . "asinh")
        (acosh . "acosh") (atanh . "atanh") (sign . "sign")))

    (define reciprocal-functions '((cot . "tan") (sec . "cos") (csc . "sin")))

    (define (join strs sep)
      (if (null? strs) ""
          (let loop ((acc (car strs)) (rest (cdr strs)))
            (if (null? rest) acc (loop (string-append acc sep (car rest)) (cdr rest))))))

    ;; repeated multiplication for integer powers
    (define (power-string base n)
      (let ((b (wrap-negative base)))
        (let loop ((k 1) (acc b))
          (if (= k n) (string-append "(" acc ")") (loop (+ k 1) (string-append acc " * " b))))))

    ;; Emit one node, given its children already emitted as strings.
    ;; op is the operator symbol, args the CAS argument values (for exponent
    ;; inspection), strs the emitted child strings.
    (define (emit-node op args strs)
      (let ((n (length strs)))
        (cond
         ((eq? op '+) (string-append "(" (join (smap wrap-negative strs) " + ") ")"))
         ((eq? op '-)
          (if (= n 1)
              (string-append "(-" (wrap-negative (car strs)) ")")
              (string-append "(" (join (smap wrap-negative strs) " - ") ")")))
         ((eq? op 'neg) (string-append "(-" (wrap-negative (car strs)) ")"))
         ((eq? op '*) (string-append "(" (join (smap wrap-negative strs) " * ") ")"))
         ((eq? op '/)
          (if (= n 1)
              (string-append "(1.0 / " (wrap-negative (car strs)) ")")
              (string-append "(" (join (smap wrap-negative strs) " / ") ")")))
         ((eq? op 'expt)
          (let ((e (cadr args)) (base (car strs)))
            (cond
             ((and (exact-integer? e) (= e 0)) "1.0")
             ((and (exact-integer? e) (<= 1 e 8)) (power-string base e))
             ((and (exact-integer? e) (<= -8 e -1))
              (string-append "(1.0 / " (power-string base (- e)) ")"))
             ((and (number? e) (exact? e) (= e 1/2)) (string-append "sqrt(" base ")"))
             ((and (number? e) (exact? e) (= e -1/2)) (string-append "inversesqrt(" base ")"))
             (else (string-append "pow(" base ", " (cadr strs) ")")))))
         ((assq op unary-functions)
          => (lambda (e)
               (unless (= n 1) (error "sym->glsl: wrong number of arguments for" op))
               (string-append (cdr e) "(" (car strs) ")")))
         ((assq op reciprocal-functions)
          => (lambda (e) (string-append "(1.0 / " (cdr e) "(" (car strs) "))")))
         (else
          (error "sym->glsl: this CAS operation has no GLSL equivalent" op)))))

    ;; ---- tree walk ----

    ;; Leaf: number, variable, or sym-fn application.  Returns a string.
    (define (emit-leaf x var-map)
      (cond ((and (number? x) (real? x)) (glsl-float x))
            ((number? x) (error "sym->glsl: complex and hypercomplex numbers are not supported" x))
            ((sym-var? x) (lookup-name (sym-var-name x) var-map "variable"))
            (else (error "sym->glsl: not a symbolic expression or real number" x))))

    ;; Generic walk.  Returns (key . code): key is the node's full text with
    ;; no substitutions (its structural identity), code is the text to use
    ;; for it.  on-node receives (key code) for every interior node and
    ;; returns the code to use -- code itself for sym->glsl, a temporary's
    ;; name for a shared node in glsl-assignments.
    (define (walk x var-map on-node)
      (define (args-of x)
        (let loop ((i (- (sym-expr-nargs x) 1)) (acc '()))
          (if (< i 0) acc (loop (- i 1) (cons (sym-expr-arg x i) acc)))))
      (cond
       ((sym-fn? x)
        (let* ((kids (smap (lambda (a) (walk a var-map on-node)) (args-of x)))
               (fname (lookup-name (sym-fn-name x) var-map "function"))
               (call (lambda (strs) (string-append fname "(" (join strs ", ") ")")))
               (key (call (smap car kids))))
          (cons key (on-node key (call (smap cdr kids))))))
       ((sym-expr? x)
        (let* ((args (args-of x))
               (kids (smap (lambda (a) (walk a var-map on-node)) args))
               (key (emit-node (sym-expr-op x) args (smap car kids))))
          (cons key (on-node key (emit-node (sym-expr-op x) args (smap cdr kids))))))
       (else (let ((s (emit-leaf x var-map))) (cons s s)))))

    (define (sym->glsl expr var-map)
      (car (walk expr var-map (lambda (key code) code))))

    ;; ---- many expressions with common subexpression elimination ----

    ;; Core hash-table-ref takes a default VALUE, SRFI 69's a THUNK, and an
    ;; imported SRFI shim can replace the core binding; the two-argument
    ;; form on a key known to exist means the same in both.
    (define (href h k default)
      (if (hash-table-exists? h k) (hash-table-ref h k) default))

    ;; Count how often each distinct interior node occurs, then emit nodes
    ;; occurring more than once as `float <prefix>N = ...;` temporaries, in
    ;; the order they are first completed (children before parents).
    ;; Optional third argument: the temporary-name prefix (default "cse").
    (define (glsl-assignments targets var-map . opts)
      (let ((prefix (if (pair? opts) (car opts) "cse"))
            (counts (make-hash-table))
            (names (make-hash-table))
            (lines '())
            (k 0))
        (for-each
         (lambda (t)
           (unless (and (pair? t) (string? (car t)))
             (error "glsl-assignments: targets are (lvalue-string . expr)" t))
           (walk (cdr t) var-map
                 (lambda (key code)
                   (hash-table-set! counts key (+ 1 (href counts key 0)))
                   code)))
         targets)
        (let ((rhs
               (smap (lambda (t)
                      (cdr (walk (cdr t) var-map
                                 (lambda (key code)
                                   (cond
                                    ((hash-table-exists? names key) (hash-table-ref names key))
                                    ((> (href counts key 0) 1)
                                     (let ((name (string-append prefix (number->string k))))
                                       (set! k (+ k 1))
                                       (hash-table-set! names key name)
                                       (set! lines (cons (string-append "float " name " = " code ";") lines))
                                       name))
                                    (else code))))))
                    targets)))
          (join (append (reverse lines)
                        (smap2 (lambda (t r) (string-append (car t) " = " r ";")) targets rhs))
                "\n"))))))
