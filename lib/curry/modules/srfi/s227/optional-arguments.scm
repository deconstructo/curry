(define-library (srfi s227 optional-arguments)
  (import (scheme base))
  (export opt-lambda let-optionals let-optionals* default-object default-object?
    %opt-bind %opt-bind-optional)
  (begin

    (define %default-object-marker (list 'srfi-227-default-object))
    (define (default-object) %default-object-marker)
    (define (default-object? obj) (eq? obj %default-object-marker))

    ;; ------------------------------------------------------------------
    ;; SRFI-227 spells its markers `#!optional`/`#!rest`, but curry's reader
    ;; treats any `#!` as a shebang-style line comment regardless of where
    ;; it appears in the source (src/reader.c), not only on the file's first
    ;; line, so that literal syntax can't be read at all here. This module
    ;; uses `#:optional`/`#:rest` instead (curry's Guile/Racket-style
    ;; `#:keyword` reader syntax) — the only deviation from the SRFI text.
    ;; ------------------------------------------------------------------

    ;; %opt-bind and %opt-bind-optional are exported even though they are
    ;; implementation details, not part of the SRFI-227 API: curry's macro
    ;; expander only resolves a macro identifier referenced from another
    ;; macro's template if that identifier is exported by the library, so a
    ;; private (unexported) helper invoked transitively by opt-lambda at a
    ;; use site outside this file would fail with "unbound variable".
    (define-syntax %opt-bind-optional
      (syntax-rules (#:rest)
        ((_ args (#:rest rst) body ...)
         (let ((rst args)) body ...))
        ((_ args () body ...)
         (begin body ...))
        ((_ args ((name default) . more) body ...)
         (let ((name (if (pair? args) (car args) default))
               (%rest (if (pair? args) (cdr args) '())))
           (%opt-bind-optional %rest more body ...)))
        ((_ args (name . more) body ...)
         (let ((name (if (pair? args) (car args) (default-object)))
               (%rest (if (pair? args) (cdr args) '())))
           (%opt-bind-optional %rest more body ...)))
        ((_ args tail-var body ...)
         (let ((tail-var args)) body ...))))

    (define-syntax %opt-bind
      (syntax-rules (#:optional #:rest)
        ((_ args (#:optional . opt-formals) body ...)
         (%opt-bind-optional args opt-formals body ...))
        ((_ args (#:rest rst) body ...)
         (let ((rst args)) body ...))
        ((_ args () body ...)
         (begin body ...))
        ((_ args (req . rest-formals) body ...)
         (let ((req (car args)) (%rest (cdr args)))
           (%opt-bind %rest rest-formals body ...)))
        ((_ args rst body ...)
         (let ((rst args)) body ...))))

    ;; (opt-lambda (req ... #:optional opt-spec ... #:rest rst) body ...)
    ;; where each opt-spec is either NAME (defaults to (default-object) when
    ;; omitted) or (NAME default-expr). #:rest and the trailing dotted-tail
    ;; form are both accepted for the final rest parameter.
    (define-syntax opt-lambda
      (syntax-rules ()
        ((_ formals body ...)
         (lambda %opt-args (%opt-bind %opt-args formals body ...)))))

    ;; (let-optionals* rest-list ((var default) ... [. tail-var]) body ...)
    ;; rest-list is evaluated once; both forms bind sequentially (each
    ;; default-expr may refer to earlier vars) — curry does not distinguish
    ;; the two, unlike the reference implementation which forbids that in
    ;; plain let-optionals.
    (define-syntax let-optionals*
      (syntax-rules ()
        ((_ rest-expr spec body ...)
         (let ((%lo-args rest-expr))
           (%opt-bind-optional %lo-args spec body ...)))))

    (define-syntax let-optionals
      (syntax-rules ()
        ((_ rest-expr spec body ...)
         (let-optionals* rest-expr spec body ...))))))
