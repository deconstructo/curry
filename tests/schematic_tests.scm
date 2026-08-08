;;; Schematic module tests — (curry schematic read/extract/format/markdown/wiki)

(import (curry schematic read) (curry schematic extract) (curry schematic format)
        (curry schematic markdown) (curry schematic wiki))

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

(define (run-string proc src . opts)
  (let ((out (open-output-string)))
    (apply proc (open-input-string src) out opts)
    (get-output-string out)))

;;; ---- (curry schematic read) ----

(check "port-fold-source-sections basic"
  (reverse
    (port-fold-source-sections
      (lambda (doc code acc) (cons (cons doc code) acc))
      '()
      (list ";;;" ";;")
      (open-input-string ";; doc one\n(define a 1)\n\n;; doc two\n(define b 2)\n")))
  (list (cons "doc one" "(define a 1)") (cons "doc two" "(define b 2)")))

(check "port-fold-source-sections multi-line comment"
  (reverse
    (port-fold-source-sections
      (lambda (doc code acc) (cons (cons doc code) acc))
      '()
      (list ";;;" ";;")
      (open-input-string ";; line one\n;; line two\n(define a 1)\n")))
  (list (cons "line one\nline two" "(define a 1)")))

(check "port-fold-source-sections code before comment"
  (reverse
    (port-fold-source-sections
      (lambda (doc code acc) (cons (cons doc code) acc))
      '()
      (list ";;;" ";;")
      (open-input-string "(define x 1)\n\n;; doc\n(define y 2)\n")))
  (list (cons "" "(define x 1)") (cons "doc" "(define y 2)")))

;; kons is called once even for a fully empty document (with docs="" and
;; code="") -- an unconditional final call at eof, not something the
;; port relaxed; upstream's own read.scm has the identical structure.
(check "port-fold-source-sections empty input still invokes kons once"
  (port-fold-source-sections (lambda (doc code acc) (+ acc 1)) 0 (list ";;;" ";;") (open-input-string ""))
  1)

;;; ---- (curry schematic extract) ----

(define (extract-one src . opts)
  (let ((out (open-output-string)))
    (apply extract-definitions (open-input-string src) out opts)
    (read (open-input-string (get-output-string out)))))

(check "extract procedure"
  (extract-one ";; Adds two numbers.\n(define (add a b) (+ a b))\n")
  (list "Adds two numbers." (cons 'procedure '(add a b))))

(check "extract lambda-form procedure"
  (extract-one ";; A lambda.\n(define f (lambda (x) x))\n")
  (list "A lambda." (cons 'procedure '(f x))))

(check "extract constant"
  (extract-one ";; Pi.\n(define pi 3.14)\n")
  (list "Pi." (cons 'constant 'pi)))

(check "extract string"
  (extract-one ";; A greeting.\n(define greeting \"hi\")\n")
  (list "A greeting." (cons 'string 'greeting)))

(check "extract parameter"
  (extract-one ";; Verbosity.\n(define verbose (make-parameter #f))\n")
  (list "Verbosity." (cons 'parameter 'verbose)))

(check "extract syntax"
  (extract-one ";; A macro.\n(define-syntax my-if (syntax-rules () ((_ c t e) (cond (c t) (else e)))))\n")
  (list "A macro." (list 'syntax 'my-if 'c 't 'e)))

(check "extract record"
  (extract-one ";; A point.\n(define-record-type <point> (make-point x y) point? (x point-x) (y point-y set-point-y!))\n")
  (list "A point."
        (cons 'record '<point>)
        (list 'procedure 'make-point 'x 'y)
        (list 'procedure 'point? 'object)
        (list 'procedure 'point-x '<point>)
        (list 'procedure 'point-y '<point>)
        (list 'procedure 'set-point-y! '<point> 'value)))

(check "extract skips uncommented definitions"
  (let ((out (open-output-string)))
    (extract-definitions (open-input-string "(define x 1)\n") out)
    (get-output-string out))
  "")

(check "extract handles define-library wrapper"
  (extract-one "(define-library (foo)\n(export x)\n(begin\n;; A def.\n(define x 1)\n))\n")
  (list "A def." (cons 'constant 'x)))

;;; ---- (curry schematic format) ----

(check "format basic reindent"
  (run-string format-scheme "(define(f x)\n(+ x 1))\n")
  "(define(f x)\n  (+ x 1))\n")

(check "format cond indentation"
  (run-string format-scheme "(cond\n((> x 0) 1)\n(else 2))\n")
  "(cond\n  ((> x 0) 1)\n  (else 2))\n")

(check "format let aligns bindings"
  (run-string format-scheme "(let ((a 1)\n(b 2))\n(+ a b))\n")
  "(let ((a 1)\n      (b 2))\n  (+ a b))\n")

(check "format preserves string contents with parens/quotes"
  (run-string format-scheme "(display \"a (b) \\\"c\\\"\")\n")
  "(display \"a (b) \\\"c\\\"\")\n")

(check "format bracket-parentheses option"
  (let ((out (open-output-string)))
    (parameterize ((bracket-parentheses? #t))
      (format-scheme (open-input-string "(let ([a 1])\n(+ a 1))\n") out))
    (get-output-string out))
  "(let ([a 1])\n  (+ a 1))\n")

;;; ---- (curry schematic markdown) ----

(check "markdown basic"
  (run-string scheme->markdown ";; A doc.\n(define x 1)\n")
  "A doc.\n\n    (define x 1)\n\n")

;;; ---- (curry schematic wiki) ----

(check "wiki basic procedure tag"
  (run-string scheme->wiki ";; Adds two.\n(define (add a b) (+ a b))\n")
  "<procedure>(add a b)</procedure>\n\nAdds two.\n\n")

;; 'declaration is written as (cons 'declaration declarations) where
;; declarations is the WHOLE cdr of the (declare ...) form (a list),
;; so the emitted form is itself a one-element list wrapping the
;; original declaration -- matching upstream's own emit-specification
;; call for this clause exactly, not something the port changed.
(check "wiki declaration uses pre-line, not a tag"
  (run-string scheme->wiki ";; A declaration.\n(declare (usual-integrations))\n")
  " declaration ((usual-integrations))\n\nA declaration.\n\n")

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
