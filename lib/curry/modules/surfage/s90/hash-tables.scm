(define-library (surfage s90 hash-tables)
  ; Both (surfage s69 hash-tables) and (scheme base) export a binding named
  ; make-hash-table — s69's SRFI-69-conformant redefinition, and (scheme
  ; base)'s raw curry builtin (which takes an integer comparator mode, not
  ; a predicate procedure). curry's import merge doesn't error on this
  ; collision, and whichever wins depends on unrelated load-order details
  ; rather than anything in this file — silently bypassing s69's
  ; comparator validation when the builtin wins. Excluding make-hash-table
  ; from the (scheme base) import makes s69's version the only candidate,
  ; unconditionally.
  (import (surfage s69 hash-tables) (except (scheme base) make-hash-table))
  (export make-table)
  (begin
    ; SRFI-90 is a single procedure, make-table, layered on an SRFI-69
    ; table: a keyword-argument-flavored constructor using Gambit-style
    ; colon-suffixed marker symbols (test: hash: size: min-load: max-load:
    ; weak-keys: weak-values:). Gambit itself treats a colon-suffixed
    ; identifier as a self-evaluating keyword object at the reader level;
    ; curry has no such reader feature, so here they're ordinary symbols
    ; that must be quoted at the call site, e.g.
    ; (make-table 'test: eq? 'size: 100) rather than the unquoted
    ; (make-table test: eq? size: 100) shown in the SRFI text.
    ;
    ; Only `test:` actually affects behavior here (via s69's make-hash-table,
    ; which itself only honors eq?/eqv?/equal?/#f — anything else raises,
    ; same restriction as s69, since the underlying table has no notion of
    ; an arbitrary custom predicate). `hash:` is accepted but ignored: s69's
    ; make-hash-table doesn't take a caller-supplied hash function either.
    ; size:/min-load:/max-load:/weak-keys:/weak-values: are purely advisory
    ; per the SRFI itself ("implementations may ignore" them) and are
    ; parsed-and-discarded here.

    (define (%find-kw args kw)
      (cond ((null? args) #f)
            ((null? (cdr args)) #f)
            ((eq? (car args) kw) (cadr args))
            (else (%find-kw (cddr args) kw))))

    (define (make-table . args)
      (let ((test (%find-kw args 'test:)))
        (if test
            (make-hash-table test)
            (make-hash-table))))))
