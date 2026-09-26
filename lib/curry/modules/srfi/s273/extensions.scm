;;; SRFI-273: Extensions to Data (Type-)Checking.
;;;
;;; https://srfi.schemers.org/srfi-273/ -- extends (srfi 253) with:
;;;   - an optional => (predicate ...) return-value-checking clause on
;;;     lambda-checked/case-lambda-checked/define-checked
;;;   - check-impl? -- an escape-hatch auxiliary syntax meaning "no
;;;     check here" wherever a predicate is expected (except inside
;;;     values-checked, where the SRFI itself says using it is an
;;;     error)
;;;   - define-check -- names/aliases a predicate
;;;   - declare-checked -- forward/cross-module check declaration
;;;   - define-values-checked -- define-values with per-value checking
;;;
;;; Backward compatible: every SRFI-253 form still works unchanged
;;; through this library -- lambda-checked/case-lambda-checked/
;;; define-checked with no => clause produce IDENTICAL expansions to
;;; SRFI-253's own macros (same check-arg calls, no extra call-with-
;;; values wrapper), just built by this file's own arg-processing
;;; rather than delegated to SRFI-253's original macros under a
;;; different name -- curry's `import`'s `rename` filter does not work
;;; for macro (define-syntax) bindings, only plain value bindings (see
;;; GitHub issue #257, found and filed while writing this file), so
;;; the usual "import the base SRFI's macro renamed, delegate to it
;;; for the common case" pattern isn't available here. This file's own
;;; %lc273-wrap/%lc273-wrap-rest branch on whether a return-check list
;;; was actually given (matching literal #f, the no-=>-clause
;;; sentinel) and skip the call-with-values wrapper entirely when not
;;; -- same zero-overhead shape SRFI-253's own lambda-checked produces.
;;;
;;; case-lambda-checked's arity dispatch (%clc273-dispatch/%clc273-try
;;; below) is a deliberate, verbatim COPY of SRFT-253's own %clc-
;;; dispatch/%clc-try (data-checking.scm), not a reimplementation from
;;; scratch and not an import (same rename limitation) -- copied
;;; specifically to inherit its already-reviewed fix for a real
;;; exponential macro-expansion blowup in clause count (see that
;;; file's own header comment: an earlier version spliced the full
;;; unexpanded recursive-dispatch form into every arity-mismatch
;;; branch, which independent security review measured as genuinely
;;; exponential -- an 8-clause x 4-parameter case-lambda-checked took
;;; over 2 minutes just to compile). Reusing that exact fixed shape
;;; here, rather than writing a new dispatcher that could reintroduce
;;; the same bug, is the point of copying it verbatim.
;;;
;;; Return-value checking (lambda-checked/case-lambda-checked/
;;; define-values-checked) is a REAL runtime check here, not the SRFI
;;; document's own minimal placeholder implementations (its reference
;;; define-values-checked, for instance, is literally plain
;;; define-values with no checking at all -- conforming as a minimal
;;; example, but not actually useful, and this codebase's own
;;; convention throughout every other SRFI here is a real
;;; implementation over a degenerate stub). declare-checked, by
;;; contrast, genuinely IS a no-op here, matching the SRFI's own
;;; reference implementation exactly -- introspecting a declared check
;;; (reading it back later, e.g. from a different module) is SRFI
;;; 283's job, a separate, not-yet-implemented SRFI; declare-checked
;;; on its own has no specified way to retrieve what it declared, so a
;;; no-op is the honest, spec-faithful implementation until 283 (or a
;;; curry-specific extension) gives it something to do.
;;;
;;; Return-value checking cannot reuse SRFI-253's own values-checked
;;; macro directly: values-checked pairs a FIXED, syntactically-known
;;; list of predicates against a fixed list of value EXPRESSIONS at
;;; macro-expansion time, but a checked procedure's actual number of
;;; returned values is only known at CALL time (via call-with-values).
;;; %lc273-check-return-values below is a small runtime loop instead,
;;; checking a runtime list of predicates against a runtime list of
;;; values -- correct for any arity, at the cost of one list-of-
;;; predicates allocation per call (predicates are ordinarily cheap
;;; variable references or small lambdas, so this is the same order of
;;; cost as the check-arg calls SRFI-253's own lambda-checked already
;;; performs per call).
;;;
;;; Non-normative "optimizable check patterns" from the SRFI text
;;; (disjoin/conjoin/complement from SRFI 235, cut from SRFI 26, every/
;;; vector-every from SRFI 1/43) are just ordinary predicate-combinator
;;; USES, not new bindings this SRFI defines -- nothing to implement
;;; here specifically. curry already has SRFI 26 (cut); SRFI 1's every
;;; works as a homogeneous-list check today. SRFI 235 (conjoin/disjoin/
;;; complement) and SRFI 43 (vector-every) are not yet implemented in
;;; curry as of this writing -- noted in docs/reference/srfi/s273.md
;;; rather than silently assumed, since the SRFI's own example
;;; (define-check positive-integer? (conjoin integer? positive?))
;;; needs SRFI 235 to run as written.

(define-library (srfi s273 extensions)
  (import (scheme base))
  (import (only (srfi s253 data-checking) check-arg))
  (export
    check-arg check-impl?
    lambda-checked case-lambda-checked define-checked
    define-check declare-checked define-values-checked
    ;; Internal helpers an exported macro's own expansion reaches --
    ;; curry's syntax-rules is not hygienic across define-library
    ;; boundaries (see docs/reference/writing-a-module.md), so every
    ;; one of these must be exported too, even though none is meant
    ;; for direct external use.
    %lc273-check-impl-marker
    %lc273-args %lc273-wrap %lc273-wrap-rest
    %lc273-normalize-preds %lc273-check-return-values
    %clc273-normalize %clc273-dispatch %clc273-try)
  (begin

    ;; check-impl? is recognized purely as a syntax-rules LITERAL
    ;; (like `else` in `cond`) everywhere a predicate is expected in
    ;; this file's macros -- it is never called as a procedure. This
    ;; definition exists only so a stray top-level reference to the
    ;; bare identifier (e.g. a user checking whether it's bound) does
    ;; something reasonable rather than raising unbound-variable; using
    ;; it as an actual procedure is not part of what this SRFI
    ;; specifies.
    (define (check-impl? . args)
      (error (string-append
               "check-impl?: this is auxiliary syntax, recognized only inside "
               "lambda-checked/case-lambda-checked/define-checked/"
               "declare-checked's check positions -- it cannot be called "
               "directly, and using it inside values-checked is an error per "
               "the SRFI text")
             args))

    ;; ── define-check ─────────────────────────────────────────────────────────
    ;; (define-check name predicate) -- names/aliases predicate. Matches
    ;; the SRFI's own reference implementation exactly.
    (define-syntax define-check
      (syntax-rules ()
        ((_ name predicate) (define name predicate))))

    ;; ── declare-checked ──────────────────────────────────────────────────────
    ;; A genuine no-op, matching the SRFI's own reference implementation
    ;; -- see this file's header comment for why that's the honest
    ;; choice here rather than inventing an unspecified introspection
    ;; API. Still real syntax (not just documentation): it parses and
    ;; accepts every shape the SRFI specifies, so declare-checked forms
    ;; are valid, harmless, and forward-compatible wherever they appear.
    (define-syntax declare-checked
      (syntax-rules (=>)
        ((_ name predicate) (if #f #f))
        ((_ (name . args) => (rpred ...)) (if #f #f))
        ((_ (name . args)) (if #f #f))))

    ;; ── Return-value checking machinery, shared by lambda-checked/
    ;; case-lambda-checked/define-values-checked ──────────────────────────────

    ;; What a check-impl?-tagged return predicate becomes at runtime --
    ;; curry has no implementation-specific static check to substitute,
    ;; so "no check" is modeled as an always-true predicate.
    (define (%lc273-check-impl-marker . args) #t)

    ;; Replaces each (check-impl? _)-tagged entry in a predicate list
    ;; with %lc273-check-impl-marker, leaving every other entry
    ;; untouched, and produces a single runtime list expression.
    (define-syntax %lc273-normalize-preds
      (syntax-rules (check-impl?)
        ((_ () (out ...)) (list out ...))
        ((_ ((check-impl? _) . more) (out ...))
         (%lc273-normalize-preds more (out ... %lc273-check-impl-marker)))
        ((_ (p . more) (out ...))
         (%lc273-normalize-preds more (out ... p)))))

    ;; Checks a runtime list of values against a runtime list of
    ;; predicates, positionally, raising on the first mismatch (wrong
    ;; count, or a predicate returning #f); returns vals unchanged on
    ;; success so the caller can splice it straight into (apply values
    ;; ...).
    (define (%lc273-check-return-values preds vals caller)
      (let loop ((ps preds) (vs vals))
        (cond
          ((and (null? ps) (null? vs)) vals)
          ((or (null? ps) (null? vs))
           (error "wrong number of return values for => checks" caller
                  (length preds) (length vals)))
          ((not ((car ps) (car vs)))
           (error "return-value check failed" caller (car vs)))
          (else (loop (cdr ps) (cdr vs))))))

    ;; ── lambda-checked (extended) ────────────────────────────────────────────
    ;; (lambda-checked formals [=> (predicate ...)] body ...)
    ;; With no => clause, expands identically to SRFI-253's own
    ;; lambda-checked (same check-arg calls, no call-with-values
    ;; wrapper) -- #f is the internal sentinel meaning "no return check
    ;; requested", recognized by %lc273-wrap/%lc273-wrap-rest below.
    (define-syntax lambda-checked
      (syntax-rules (=>)
        ((_ formals => (rpred ...) body ...)
         (%lc273-args formals () () (rpred ...) body ...))
        ((_ formals body ...)
         (%lc273-args formals () () #f body ...))))

    ;; Peels one formal at a time off `formals` (identical shape/order
    ;; to SRFI-253's own %lambda-checked, plus recognizing
    ;; (fname (check-impl? _)) as an explicitly-unchecked formal),
    ;; accumulating real parameter names and check-arg calls, until
    ;; formals is either '() (proper list) or a bare identifier (rest
    ;; arg) -- then hands off to %lc273-wrap/%lc273-wrap-rest to build
    ;; the final lambda with the return-check (if any) wrapped around
    ;; its body.
    (define-syntax %lc273-args
      (syntax-rules (check-impl?)
        ((_ () (name ...) (check ...) rp body ...)
         (%lc273-wrap (name ...) (check ...) rp body ...))
        ((_ ((fname (check-impl? _)) . more) (name ...) (check ...) rp body ...)
         (%lc273-args more (name ... fname) (check ...) rp body ...))
        ((_ ((fname fpred) . more) (name ...) (check ...) rp body ...)
         (%lc273-args more (name ... fname)
                      (check ... (check-arg fpred fname 'lambda-checked)) rp body ...))
        ((_ (fname . more) (name ...) (check ...) rp body ...)
         (%lc273-args more (name ... fname) (check ...) rp body ...))
        ((_ rest (name ...) (check ...) rp body ...)
         (%lc273-wrap-rest rest (name ...) (check ...) rp body ...))))

    (define-syntax %lc273-wrap
      (syntax-rules ()
        ((_ (name ...) (check ...) #f body ...)
         (lambda (name ...) check ... body ...))
        ((_ (name ...) (check ...) (rpred ...) body ...)
         (lambda (name ...)
           check ...
           (call-with-values
             (lambda () body ...)
             (lambda vals
               (apply values
                 (%lc273-check-return-values
                   (%lc273-normalize-preds (rpred ...) ()) vals 'lambda-checked))))))))

    (define-syntax %lc273-wrap-rest
      (syntax-rules ()
        ((_ rest (name ...) (check ...) #f body ...)
         (lambda (name ... . rest) check ... body ...))
        ((_ rest (name ...) (check ...) (rpred ...) body ...)
         (lambda (name ... . rest)
           check ...
           (call-with-values
             (lambda () body ...)
             (lambda vals
               (apply values
                 (%lc273-check-return-values
                   (%lc273-normalize-preds (rpred ...) ()) vals 'lambda-checked))))))))

    ;; ── case-lambda-checked (extended) ───────────────────────────────────────
    ;; (case-lambda-checked (formals [=> (predicate ...)] body ...) ...)
    ;; Each clause is normalized independently: a clause with => gets
    ;; its body wrapped with the same runtime return-value check
    ;; lambda-checked uses; the resulting plain-shaped clause list is
    ;; then dispatched by %clc273-dispatch/%clc273-try -- a deliberate
    ;; verbatim copy of SRFI-253's own hardened dispatcher (see this
    ;; file's header comment).
    (define-syntax case-lambda-checked
      (syntax-rules ()
        ((_ clause ...)
         (%clc273-normalize (clause ...) ()))))

    (define-syntax %clc273-normalize
      (syntax-rules (=>)
        ((_ () (out ...)) (lambda args (%clc273-dispatch args out ...)))
        ((_ ((formals => (rpred ...) body ...) . more) (out ...))
         (%clc273-normalize more
           (out ... (formals
                      (call-with-values
                        (lambda () body ...)
                        (lambda vals
                          (apply values
                            (%lc273-check-return-values
                              (%lc273-normalize-preds (rpred ...) ())
                              vals 'case-lambda-checked))))))))
        ((_ (clause . more) (out ...))
         (%clc273-normalize more (out ... clause)))))

    ;; %clc273-dispatch is a verbatim copy of SRFI-253's %clc-dispatch
    ;; (data-checking.scm) under a new name -- see this file's header
    ;; comment for why this is a deliberate copy, not an import-and-
    ;; rename (curry issue #257) or an independent reimplementation.
    ;;
    ;; %clc273-try is that same copy PLUS exactly one addition: a
    ;; (fname (check-impl? _)) pattern, mirroring %lc273-args's own
    ;; identical pattern for lambda-checked/define-checked (found
    ;; missing here by independent review -- SRFI-253's original
    ;; dispatcher predates check-impl? entirely, so without this,
    ;; (check-impl? _) in a case-lambda-checked clause's own argument-
    ;; check position was treated as a literal predicate expression and
    ;; called as one, unconditionally raising instead of skipping the
    ;; check as intended). This one addition cannot reintroduce the
    ;; exponential-blowup bug the original fix (see the header comment
    ;; on SRFI-253's own case-lambda-checked) closed: it's the same
    ;; O(1)-per-formal shape as the pre-existing bare-fname clause
    ;; immediately below it (just triggered by a different pattern),
    ;; touches `fail` the same way every other clause here already
    ;; does (a single reference to the already-built thunk, never
    ;; re-expanding it), and adds no new recursion or duplication.
    (define-syntax %clc273-dispatch
      (syntax-rules ()
        ((_ args)
         (error "case-lambda-checked: no matching clause" args))
        ((_ args (formals body ...) rest ...)
         (let ((fail-thunk (lambda () (%clc273-dispatch args rest ...))))
           (%clc273-try formals args (begin body ...) (fail-thunk))))))

    (define-syntax %clc273-try
      (syntax-rules (check-impl?)
        ((_ () args-cursor body fail)
         (if (null? args-cursor) body fail))
        ((_ ((fname (check-impl? _)) . more) args-cursor body fail)
         (if (pair? args-cursor)
             (let ((fname (car args-cursor)))
               (%clc273-try more (cdr args-cursor) body fail))
             fail))
        ((_ ((fname fpred) . more) args-cursor body fail)
         (if (pair? args-cursor)
             (let ((fname (car args-cursor)))
               (if (fpred fname)
                   (%clc273-try more (cdr args-cursor) body fail)
                   fail))
             fail))
        ((_ (fname . more) args-cursor body fail)
         (if (pair? args-cursor)
             (let ((fname (car args-cursor)))
               (%clc273-try more (cdr args-cursor) body fail))
             fail))
        ((_ rest args-cursor body fail)
         (let ((rest args-cursor)) body))))

    ;; ── define-checked (extended) ────────────────────────────────────────────
    ;; (define-checked (name . formals) [=> (predicate ...)] body ...)
    ;; (define-checked name predicate value) -- unchanged from SRFI-253:
    ;; checks value against predicate once, at definition time.
    (define-syntax define-checked
      (syntax-rules (=>)
        ((_ (name . formals) => (rpred ...) body ...)
         (define name (lambda-checked formals => (rpred ...) body ...)))
        ((_ (name . formals) body ...)
         (define name (lambda-checked formals body ...)))
        ((_ name predicate value)
         (define name (check-arg predicate value 'name)))))

    ;; ── define-values-checked ────────────────────────────────────────────────
    ;; (define-values-checked (var ...) (check ...) form)
    ;; Like define-values, but the values form produces are checked
    ;; positionally against check ... before binding -- a REAL
    ;; implementation, not the SRFI text's own unchecked reference
    ;; example (see this file's header comment).
    (define-syntax define-values-checked
      (syntax-rules ()
        ((_ (var ...) (chk ...) form)
         (define-values (var ...)
           (call-with-values
             (lambda () form)
             (lambda vals
               (apply values
                 (%lc273-check-return-values
                   (%lc273-normalize-preds (chk ...) ()) vals 'define-values-checked))))))))

  )) ;; end begin, define-library
