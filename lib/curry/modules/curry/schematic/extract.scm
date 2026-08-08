;;; (curry schematic extract) — extract commented definitions from Scheme
;;; source as s-expressive specifications.
;;;
;;; Ported from Evan Hanson's BSD-licensed schematic
;;; (https://git.foldling.org/schematic/), specifically src/schematic/
;;; extract.scm — see docs/reference/module-schematic.md for the scope
;;; of the port. Each specification has the form:
;;;
;;;   <specification> = (<comment> (<type> . <form>) ...)
;;;   <comment>       = string?
;;;   <form>          = any?
;;;   <type>          = 'procedure | 'syntax | 'constant | 'parameter
;;;                   | 'record | 'string | 'type | 'declaration
;;;
;;; Only reasonably simple definition styles are recognized (this is
;;; upstream's own caveat, not something the port relaxed): "let over
;;; lambda" idioms and anything not at the toplevel (outside a
;;; recognized library-wrapper form) are silently skipped.
;;;
;;; curry has no case-lambda, so extract-definitions takes its optional
;;; arguments the same way (curry csv)/(curry toml) do (a trailing
;;; `. opts` list) rather than upstream's case-lambda dispatch.
;;;
;;; This module writes its own small, local `match`/`match-case` —
;;; upstream's own tiny vector-marker pattern matcher (a pattern
;;; position wrapped in `#(...)` captures; anything else must match
;;; literally via `equal?`), not (curry matchable)'s full quasiquote-
;;; style matcher. It's kept separate deliberately: schematic-extract's
;;; own fixed, small pattern set is exactly what it was designed for,
;;; and reusing it here keeps this port's behavior traceable against
;;; upstream's own tests rather than needing every clause re-derived
;;; against a different pattern language's literal-vs-capture rules.

(define-library (curry schematic extract)
  (import (scheme base) (scheme read) (scheme write) (curry schematic read))
  (export extract-definitions)
  (begin

(define (%symbol-append . syms)
  (string->symbol (apply string-append (map symbol->string syms))))

;; `read`, but returns #f on any read error instead of raising.
(define (%read/guard port)
  (guard (_ (#t #f)) (read port)))

;; Reads the car of the pair on `port`, or #f if the read signals an
;; error or no pair is encountered. Does not handle multiline/datum
;; comments.
(define (%read-car port)
  (let skip-ws ()
    (let ((c (peek-char port)))
      (unless (or (eof-object? c) (not (memv c (list #\; #\space #\tab #\newline))))
        (let ((c2 (read-char port))) (when (char=? c2 #\;) (read-line port)))
        (skip-ws))))
  (if (eqv? (read-char port) #\() (%read/guard port) #f))

;; Splits `lst` at its first element found in `xs` (compared with
;; `member`), returning (values before-list after-list) — or
;; (values lst '()) if no split point is found.
(define (%list-split lst xs)
  (let loop ((a '()) (b lst))
    (cond ((null? b) (values lst b))
          ((member (car b) xs) (values (reverse a) (cdr b)))
          (else (loop (cons (car b) a) (cdr b))))))

;;; ---- A small vector-marker pattern matcher, local to this module ----
;;;
;;; A pattern position wrapped in `#(sub-pattern)` captures whatever's
;;; there (recursing into sub-pattern, so `#(x)` is the common "bind
;;; this whole position to x" case, but `#(a . b)` captures further
;;; structure too); every other pattern position must `equal?` the
;;; input directly. %match returns the list of captured values in
;;; left-to-right pattern order, or #f on a non-match.

(define (%match form pattern)
  (define captures '())
  (define (walk x p)
    (cond
      ((vector? p) (set! captures (cons x captures)) #t)
      ((and (pair? x) (pair? p)) (and (walk (car x) (car p)) (walk (cdr x) (cdr p))))
      (else (equal? x p))))
  (and (walk form pattern) (reverse captures)))

(define-syntax %match-case
  (syntax-rules (else =>)
    ((_ form) (if #f #t))
    ((_ form (else . body)) (begin . body))
    ((_ form (pattern => proc) . clauses)
     (let ((results (%match form (quote pattern))))
       (if results (apply proc results) (%match-case form . clauses))))
    ((_ form (pattern . body) . clauses)
     (if (%match form (quote pattern)) (begin . body) (%match-case form . clauses)))))

;;; ---- Definition extraction ----

(define (%extract-one comment code types out)
  (let ((in (open-input-string code)))
    (let loop ()
      (cond
        ((string=? comment "") #t)
        ;; %read/guard returns the eof-object at end of input, which is
        ;; not #f -- must be checked explicitly and treated as "stop",
        ;; not fed through as if it were a successfully read form (that
        ;; would loop forever, since re-reading an exhausted port keeps
        ;; returning the eof-object rather than erroring).
        ((eof-object? (peek-char in))
         (case (call-with-port (open-input-string code) %read-car)
           ((define-library))  ; R7RS.
           ((define-module))   ; Gauche.
           ((module))          ; CHICKEN.
           (else #f)))
        ((%read/guard in) =>
         (lambda (form)
           (%match-case form
             ((define (#(name) . #(args)) . #(_)) =>
              (lambda (name args _)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'procedure (cons name args)) out) (display ")" out) (newline out)))
             ((define #(name) (lambda #(args) . #(_))) =>
              (lambda (name args _)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'procedure (cons name args)) out) (display ")" out) (newline out)))
             ((define #(name) (case-lambda . #(clauses))) =>
              (lambda (name clauses)
                (display "(" out) (write comment out) (display " " out)
                (for-each (lambda (args) (write (cons 'procedure (cons name args)) out) (display " " out))
                          (map car clauses))
                (display ")" out) (newline out)))
             ((define #(name) (make-parameter . #(_))) =>
              (lambda (name _)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'parameter name) out) (display ")" out) (newline out)))
             ((define #(name) #(value)) =>
              (lambda (name value)
                (cond ((string? value)
                       (display "(" out) (write comment out) (display " " out)
                       (write (cons 'string name) out) (display ")" out) (newline out))
                      ((not (pair? value))
                       (display "(" out) (write comment out) (display " " out)
                       (write (cons 'constant name) out) (display ")" out) (newline out)))))
             ((define-syntax #(name) (syntax-rules #(_) . #(clauses))) =>
              (lambda (name _ clauses)
                (display "(" out) (write comment out) (display " " out)
                (for-each (lambda (args) (write (cons 'syntax (cons name args)) out) (display " " out))
                          (map cdar clauses))
                (display ")" out) (newline out)))
             ((define-syntax #(name) . #(_)) =>
              (lambda (name _)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'syntax name) out) (display ")" out) (newline out)))
             ((define-record-type #(name) #(make) #(pred) . #(fields)) =>
              (lambda (name make pred fields)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'record name) out) (display " " out)
                (write (cons 'procedure make) out) (display " " out)
                (write (cons 'procedure (list pred 'object)) out) (display " " out)
                (for-each (lambda (p) (write (cons 'procedure (list p name)) out) (display " " out))
                          (map cadr fields))
                (for-each (lambda (p) (when (pair? p) (write (cons 'procedure (list (car p) name 'value)) out) (display " " out)))
                          (map cddr fields))
                (display ")" out) (newline out)))
             ;; CHICKEN record shorthand: (define-record name field ...)
             ((define-record #(name) . #(fields)) =>
              (lambda (name fields)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'record name) out) (display " " out)
                (write (cons 'procedure (cons (%symbol-append 'make- name) fields)) out) (display " " out)
                (write (cons 'procedure (list (%symbol-append name '?) 'object)) out) (display " " out)
                (for-each (lambda (f) (write (cons 'procedure (list (%symbol-append name '- f) name)) out) (display " " out)) fields)
                (for-each (lambda (f) (write (cons 'procedure (list (%symbol-append name '- f '-set!) name 'value)) out) (display " " out)) fields)
                (display ")" out) (newline out)))
             ;; CHICKEN type syntax: (define-type name type)
             ((define-type #(name) #(type)) =>
              (lambda (name type)
                (when (eq? types #f)
                  (display "(" out) (write comment out) (display " " out)
                  (write (list 'type name type) out) (display ")" out) (newline out))))
             ;; CHICKEN type syntax: (: name type)
             ((: #(name) #(type)) =>
              (lambda (name type)
                (cond
                  ((eq? types #f) (loop))
                  ((eq? types #t)
                   (display "(" out) (write comment out) (display " " out)
                   (if (list? type)
                       (let-values (((arguments _) (%list-split type '(-> -->))))
                         (if (eq? type arguments)
                             (write (cons type name) out)
                             (write (cons 'procedure (cons name arguments)) out)))
                       (write (cons type name) out))
                   (display ")" out) (newline out)))))
             ((declare . #(declarations)) =>
              (lambda (declarations)
                (display "(" out) (write comment out) (display " " out)
                (write (cons 'declaration declarations) out) (display ")" out) (newline out))))
           (loop)))
        (else
         (case (call-with-port (open-input-string code) %read-car)
           ((define-library))  ; R7RS.
           ((define-module))   ; Gauche.
           ((module))          ; CHICKEN.
           (else #f)))))))

;; (extract-definitions)                                -> stdin, stdout, default prefixes/types
;; (extract-definitions input)                           -> input port, stdout
;; (extract-definitions input output)                    -> both ports given
;; (extract-definitions input output types)              -> types: #f (default forms) or #t (: declarations)
;; (extract-definitions input output types comment-prefixes)
(define (extract-definitions . opts)
  (let* ((input           (if (pair? opts) (car opts) (current-input-port)))
         (opts            (if (pair? opts) (cdr opts) '()))
         (output          (if (pair? opts) (car opts) (current-output-port)))
         (opts            (if (pair? opts) (cdr opts) '()))
         (types           (if (pair? opts) (car opts) #f))
         (opts            (if (pair? opts) (cdr opts) '()))
         (comment-prefixes (if (pair? opts) (car opts) (list ";;;" ";;"))))
    (port-fold-source-sections
      (lambda (comment code carry)
        (let* ((comment (if (pair? carry) (car carry) comment))
               (code     (if (pair? carry) (string-append (cdr carry) code) code))
               (rest     (%extract-one comment code types output)))
          (if (pair? rest) rest #f)))
      #f
      comment-prefixes
      input)))

  )) ;; end begin, define-library
