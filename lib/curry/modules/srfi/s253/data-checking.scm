;;; SRFI-253: Data (Type-)Checking.
;;;
;;; https://srfi.schemers.org/srfi-253/ -- portable argument/return-value
;;; validation: check-arg, values-checked, check-case, lambda-checked,
;;; case-lambda-checked, define-checked, define-record-type-checked.
;;;
;;; Any ordinary unary predicate works as a checker, so this already
;;; "catches" curry's full extended type system for free -- the numeric
;;; tower (bignum?, rational?, complex?, quaternion?, octonion?,
;;; multivector?, surreal?, symbolic?), quantum values (quantum?), and
;;; matrices/tensors/spinors/actors, all work as-is with lambda-checked/
;;; define-checked/etc. Two predicates were genuinely missing from
;;; curry's core before this SRFI could be used meaningfully across its
;;; whole type system -- bignum? and multivector? -- added alongside
;;; this module (src/builtins.c) rather than left as a silent gap.
;;;
;;; curry has no native case-lambda at all (not even as a core special
;;; form -- confirmed absent, and no SRFI-16 shim exists either), so
;;; case-lambda-checked below is self-contained: it dispatches on
;;; argument count itself via a single (lambda args ...) plus explicit
;;; arity testing, rather than expanding into calls to a case-lambda
;;; primitive that doesn't exist. Scoped to this module; implementing
;;; general-purpose case-lambda as its own R7RS feature is a separate,
;;; larger task not undertaken here.
;;;
;;; Errors use plain R7RS `error` (message + irritants), matching this
;;; codebase's own (srfi s145 assume) convention -- not R6RS's &assertion
;;; condition type the SRFI text recommends, since curry's condition
;;; system (CL-style, via (curry conditions)) doesn't use R6RS condition
;;; types at all and requiring that import just for this SRFI would be
;;; disproportionate.

(define-library (srfi s253 data-checking)
  (import (scheme base))
  (export check-arg values-checked check-case
          lambda-checked case-lambda-checked
          define-checked define-record-type-checked
          ;; Internal helpers an exported macro's own expansion reaches --
          ;; curry's syntax-rules is not hygienic across define-library
          ;; boundaries (see docs/reference/writing-a-module.md), so every
          ;; one of these must be exported too, even though none is meant
          ;; for direct external use.
          %values-checked %lambda-checked %clc-dispatch %clc-try
          %drtc-build-raw %drtc-finish %drtc-ctor-checks %drtc-check-each %drtc-mod-check)
  (begin

    ;; (check-arg predicate arg [caller]) -- returns arg on success
    ;; (matching the SRFI's own reference implementation, which returns
    ;; the checked value; the spec text itself only says "unspecified"
    ;; on success, so returning arg is conforming and more useful).
    (define (check-arg predicate arg . caller)
      (if (predicate arg)
          arg
          (apply error "check-arg: predicate failed" arg predicate caller)))

    ;; (values-checked (pred ...) val ...) -- each val is evaluated
    ;; exactly once, checked against the correspondingly-positioned
    ;; pred, then returned via (values ...). %values-checked walks both
    ;; lists in lockstep, accumulating already-evaluated temporaries so
    ;; no val expression is evaluated twice.
    (define-syntax values-checked
      (syntax-rules ()
        ((_ (pred ...) val ...)
         (%values-checked (pred ...) (val ...) ()))))

    (define-syntax %values-checked
      (syntax-rules ()
        ((_ () () (tmp ...))
         (values tmp ...))
        ((_ (pred1 pred2 ...) (val1 val2 ...) (tmp ...))
         (let ((t val1))
           (if (pred1 t)
               (%values-checked (pred2 ...) (val2 ...) (tmp ... t))
               (error "values-checked: predicate failed" t 'pred1))))))

    ;; (check-case value (pred body ...) ... [(else body ...)]) --
    ;; value is evaluated exactly once (bound to v on first entry;
    ;; every recursive step below re-passes v itself, an identifier
    ;; reference, never the original expression).
    (define-syntax check-case
      (syntax-rules (else)
        ((_ value)
         (error "check-case: no clause matched" value))
        ((_ value (else body ...))
         (begin body ...))
        ((_ value (pred body ...) clause ...)
         (let ((v value))
           (if (pred v)
               (begin body ...)
               (check-case v clause ...))))))

    ;; (lambda-checked formals body ...) -- formals is an ordinary
    ;; lambda formals list, except any element may be (name predicate)
    ;; instead of a bare name; such arguments are checked once, at
    ;; call time, before the body runs. Proper, improper (dotted rest),
    ;; and fully-variadic (bare symbol) formals are all supported.
    (define-syntax lambda-checked
      (syntax-rules ()
        ((_ formals body ...)
         (%lambda-checked formals () () body ...))))

    ;; Peels one formal at a time off `formals`, accumulating the real
    ;; (unchecked) parameter names in `name ...` and any generated
    ;; check-arg calls in `check ...`, until formals is either '()
    ;; (proper list, clause 1) or a bare identifier (rest arg, clause
    ;; 4). Clause order matters: the explicit (fname fpred) pattern
    ;; (clause 2) must come before the general bare-formal pattern
    ;; (clause 3), since a 2-element list also structurally matches
    ;; "(fname . more)" with more = (fpred) -- syntax-rules tries
    ;; clauses in order and the first match wins, so listing the more
    ;; specific pattern first is what makes this work.
    (define-syntax %lambda-checked
      (syntax-rules ()
        ((_ () (name ...) (check ...) body ...)
         (lambda (name ...) check ... body ...))
        ((_ ((fname fpred) . more) (name ...) (check ...) body ...)
         (%lambda-checked more (name ... fname) (check ... (check-arg fpred fname 'lambda-checked)) body ...))
        ((_ (fname . more) (name ...) (check ...) body ...)
         (%lambda-checked more (name ... fname) (check ...) body ...))
        ((_ rest (name ...) (check ...) body ...)
         (lambda (name ... . rest) check ... body ...))))

    ;; (case-lambda-checked (formals body ...) ...) -- like case-lambda,
    ;; dispatching on the actual call's argument count, with the same
    ;; (name predicate) checked-formal support as lambda-checked. Since
    ;; curry has no native case-lambda, this dispatches by hand: one
    ;; (lambda args ...) whose body tries each clause in turn, testing
    ;; whether `args`'s length matches that clause's arity (exact for a
    ;; proper formals list, "at least" for a dotted/rest one) and
    ;; destructuring+checking+evaluating its body if so.
    ;;
    ;; %clc-dispatch's `fail` continuation (the next clause to try, or
    ;; a final "no match" error) is wrapped in a zero-argument thunk,
    ;; created ONCE per clause, and only the tiny 2-token call
    ;; (fail-thunk) -- not the unexpanded recursive dispatch tree
    ;; itself -- gets spliced into %clc-try's several arity-mismatch
    ;; branches. This is deliberate, not incidental: an earlier version
    ;; spliced the full unexpanded (%clc-dispatch args rest ...) form
    ;; in directly at every branch, which independent security review
    ;; found and measured to be genuinely EXPONENTIAL in clause count
    ;; (base ~= 2p+1 for p parameters per clause -- an 8-clause x
    ;; 4-parameter case-lambda-checked form, entirely reasonable-looking
    ;; user code, took over 2 minutes to even finish compiling). The
    ;; thunk indirection makes expansion cost linear in (clauses x
    ;; parameters) again, at the cost of one small closure allocation
    ;; per clause per call -- a normal, bounded runtime cost, not a
    ;; macro-expansion-time compile bomb reachable from ordinary-looking
    ;; source.
    (define-syntax case-lambda-checked
      (syntax-rules ()
        ((_ clause ...)
         (lambda args (%clc-dispatch args clause ...)))))

    (define-syntax %clc-dispatch
      (syntax-rules ()
        ((_ args)
         (error "case-lambda-checked: no matching clause" args))
        ((_ args (formals body ...) rest ...)
         (let ((fail-thunk (lambda () (%clc-dispatch args rest ...))))
           (%clc-try formals args (begin body ...) (fail-thunk))))))

    (define-syntax %clc-try
      (syntax-rules ()
        ((_ () args-cursor body fail)
         (if (null? args-cursor) body fail))
        ((_ ((fname fpred) . more) args-cursor body fail)
         (if (pair? args-cursor)
             (let ((fname (car args-cursor)))
               (if (fpred fname)
                   (%clc-try more (cdr args-cursor) body fail)
                   fail))
             fail))
        ((_ (fname . more) args-cursor body fail)
         (if (pair? args-cursor)
             (let ((fname (car args-cursor)))
               (%clc-try more (cdr args-cursor) body fail))
             fail))
        ((_ rest args-cursor body fail)
         (let ((rest args-cursor)) body))))

    ;; (define-checked (name . formals) body ...) -- equivalent to
    ;; (define name (lambda-checked formals body ...)).
    ;; (define-checked name predicate value) -- checks value against
    ;; predicate once, at definition time, and binds name to it.
    ;; The two forms are unambiguous: the first's second component is
    ;; always a pair (a formals list with at least a name), the
    ;; second's is always a bare identifier.
    (define-syntax define-checked
      (syntax-rules ()
        ((_ (name . formals) body ...)
         (define name (lambda-checked formals body ...)))
        ((_ name predicate value)
         (define name (check-arg predicate value 'name)))))

    ;; (define-record-type-checked type-name (ctor-name ctor-arg ...)
    ;;   predicate (field-name field-pred accessor-name [modifier-name]) ...)
    ;;
    ;; Curry-specific scoping decision (the SRFI's own spec text is
    ;; terse about exact cross-referencing rules here): ctor-arg names
    ;; must appear in the SAME ORDER as the field-specs they check
    ;; against -- i.e. ctor-arg N is checked against field-spec N's
    ;; predicate, positionally, not matched by name. This is simpler
    ;; to implement correctly in portable syntax-rules (no compile-time
    ;; identifier-equality machinery needed) and matches how every
    ;; existing define-record-type call in this codebase is already
    ;; written (ctor args always positionally mirror the full field
    ;; list, in order).
    ;;
    ;; Only constructor arguments and modifier calls are checked, not
    ;; accessor reads: the field predicate already held at construction
    ;; or last modification time, so re-checking on every read would
    ;; just repeat a check that's already guaranteed to pass, for no
    ;; benefit -- accessors here are the plain, unwrapped ones curry's
    ;; own define-record-type already produces.
    ;;
    ;; The constructor and each modifier are defined via curry's own
    ;; define-record-type first (raw, unchecked), then immediately
    ;; captured under a fresh let-bound name and shadowed in place with
    ;; a checked wrapper via set! -- this needs no gensym/identifier-
    ;; concatenation (which portable syntax-rules can't do at all):
    ;; each field's/the constructor's own let-capture is a separate,
    ;; non-nested sibling form, so reusing the same literal template
    ;; identifier (e.g. %%drtc-orig) across several of them is safe.
    ;;
    ;; KNOWN LIMITATION (found by independent security review): between
    ;; define-record-type binding the raw constructor/modifiers under
    ;; their public names and the set! calls below replacing them with
    ;; checked wrappers, there is a real window during which the raw,
    ;; UNCHECKED constructor/modifiers are live under the public name in
    ;; GLOBAL_ENV. curry's actors are real OS threads sharing that same
    ;; global environment, so another already-running actor that calls
    ;; the constructor/a modifier by name during that exact window gets
    ;; the unchecked version -- a genuine, if narrow and short-lived,
    ;; validation bypass. Avoiding it entirely would need each field's
    ;; raw constructor/modifier bound under its OWN distinct temporary
    ;; name from the start (never touching the public name until fully
    ;; checked-and-ready), which needs generating N distinct fresh
    ;; identifiers for N mutable fields all within one define-record-
    ;; type call -- something portable syntax-rules genuinely cannot do
    ;; (no identifier concatenation, no gensym). Given that constraint,
    ;; this is documented rather than "fixed" with a redesign that would
    ;; trade a narrow race for a real risk of a new correctness bug:
    ;; evaluate every define-record-type-checked form before spawning
    ;; any actor that uses the type it defines.
    ;;
    ;; %drtc-build-raw pre-expands the whole (fname fpred acc [mod])
    ;; field-spec list into plain (fname acc [mod]) forms -- the shape
    ;; curry's own define-record-type understands -- BEFORE splicing
    ;; them into a define-record-type call, rather than embedding an
    ;; unexpanded per-field macro call directly in that call's own
    ;; field-spec position. define-record-type is compiled directly
    ;; (src/compiler.c's dedicated compile_define_record_type), not
    ;; itself a syntax-rules macro that would expand its own arguments
    ;; first -- an earlier version of this macro put an unexpanded
    ;; (%drtc-raw-field field-spec) call there directly, which
    ;; compile_define_record_type then read completely literally
    ;; (its own car -- the symbol %drtc-raw-field -- treated as if it
    ;; were an actual field name), silently producing a garbled record
    ;; type whose real accessors were never actually defined at all.
    (define-syntax define-record-type-checked
      (syntax-rules ()
        ((_ type-name (ctor-name ctor-arg ...) predicate field-spec ...)
         (%drtc-build-raw (field-spec ...) ()
           (%drtc-finish type-name (ctor-name ctor-arg ...) predicate (field-spec ...))))))

    (define-syntax %drtc-build-raw
      (syntax-rules ()
        ((_ () (rfield ...) (k-macro k-arg ...))
         (k-macro k-arg ... (rfield ...)))
        ((_ ((fname fpred acc) . more) (rfield ...) k)
         (%drtc-build-raw more (rfield ... (fname acc)) k))
        ((_ ((fname fpred acc mod) . more) (rfield ...) k)
         (%drtc-build-raw more (rfield ... (fname acc mod)) k))))

    (define-syntax %drtc-finish
      (syntax-rules ()
        ((_ type-name (ctor-name ctor-arg ...) predicate (field-spec ...) (rfield ...))
         (begin
           (define-record-type type-name (ctor-name ctor-arg ...) predicate rfield ...)
           (%drtc-ctor-checks ctor-name (ctor-arg ...) (field-spec ...))
           (%drtc-mod-check field-spec) ...))))

    (define-syntax %drtc-ctor-checks
      (syntax-rules ()
        ((_ ctor-name (ctor-arg ...) (field-spec ...))
         (let ((%%drtc-orig ctor-name))
           (set! ctor-name
             (lambda (ctor-arg ...)
               (%drtc-check-each (ctor-arg ...) (field-spec ...))
               (%%drtc-orig ctor-arg ...)))))))

    (define-syntax %drtc-check-each
      (syntax-rules ()
        ((_ () ()) (if #f #f))
        ((_ (arg1 . arg-rest) ((fname fpred . _) . field-rest))
         (begin (check-arg fpred arg1 'fname)
                (%drtc-check-each arg-rest field-rest)))))

    (define-syntax %drtc-mod-check
      (syntax-rules ()
        ((_ (fname fpred acc)) (if #f #f))
        ((_ (fname fpred acc mod))
         (let ((%%drtc-orig mod))
           (set! mod (lambda (r v) (check-arg fpred v 'mod) (%%drtc-orig r v)))))))))
