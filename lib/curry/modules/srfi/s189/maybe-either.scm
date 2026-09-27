;;; SRFI-189: Maybe and Either: optional container types.
;;;
;;; https://srfi.schemers.org/srfi-189/ -- two immutable container-type
;;; families for optional/error-carrying values. A Maybe is a Just
;;; (holding zero or more payload values) or the unique Nothing. An
;;; Either is a Right (success, zero or more payload values) or a Left
;;; (failure, zero or more payload values). Both payloads are genuinely
;;; variadic -- "a Just containing no objects is not the same as a
;;; Nothing: it represents success when there are no actual values to
;;; return" (the SRFI's own text) -- so every payload here is stored as
;;; a plain list and unwrapped via `apply`/`values`, never assumed to be
;;; exactly one value except where the spec itself says so (maybe-join,
;;; maybe-let*'s single-value claws, etc).
;;;
;;; Two genuinely underspecified corners, resolved here with the most
;;; literal reading of the spec's own text (each documented at its own
;;; definition below, not just here): maybe-unfold/either-unfold's
;;; unusual single-step-then-error shape (the spec's own prose, quoted
;;; verbatim in the comment there, really does describe exactly two
;;; stop? calls, not a general recursive loop), and maybe-let*-values'
;;; claw grammar (extended past maybe-let*'s single-value claws to allow
;;; a claw's own "var" position to be a formals list, mirroring R7RS
;;; let-values' own claw shape, since the SRFI's text says only "supports
;;; multiple value unpacking" without spelling out the exact grammar).

(define-library (srfi s189 maybe-either)
  (import (scheme base))
  (export
    ;; Types and predicates
    just nothing right left
    just? nothing? right? left? maybe? either?
    ;; just-payload/right-payload: not part of the SRFI's own surface,
    ;; but must be exported anyway -- maybe-let*/maybe-let*-values/
    ;; either-let*/either-let*-values are exported macros whose own
    ;; expansions reference these two directly, and curry's
    ;; syntax-rules isn't hygienic across define-library boundaries
    ;; (see docs/reference/writing-a-module.md); an importer using any
    ;; of those four forms would otherwise hit unbound-variable the
    ;; first time they're actually used, not at import time.
    just-payload right-payload
    maybe= either=
    ;; Conversions between the two families
    list->just list->left list->right
    maybe->either either->maybe either-swap
    ;; Accessors
    maybe-ref either-ref maybe-ref/default either-ref/default
    ;; Monadic operations
    maybe-join either-join maybe-compose either-compose maybe-bind either-bind
    ;; Sequence-flavored operations
    maybe-length either-length
    maybe-filter maybe-remove either-filter either-remove
    maybe-sequence either-sequence
    ;; Protocol conversions
    maybe->list either->list list->maybe list->either
    maybe->truth either->truth truth->maybe truth->either
    maybe->list-truth either->list-truth list-truth->maybe list-truth->either
    maybe->generation either->generation generation->maybe generation->either
    maybe->values either->values values->maybe values->either
    maybe->two-values two-values->maybe
    exception->either
    ;; Map / for-each / fold / unfold
    maybe-map either-map maybe-for-each either-for-each
    maybe-fold either-fold maybe-unfold either-unfold
    ;; Syntax
    maybe-if maybe-and either-and maybe-or either-or
    maybe-let* either-let* maybe-let*-values either-let*-values
    either-guard
    ;; Trivalent logic
    tri-not tri=? tri-and tri-or tri-merge)
  (begin

    ;; ── Types ────────────────────────────────────────────────────────────────
    ;; Every payload is a plain list of zero or more values, never
    ;; assumed to have exactly one element except where noted.

    (define-record-type <just> (%make-just payload) just? (payload just-payload))
    (define-record-type <nothing-type> (%make-nothing) nothing?)
    (define %the-nothing (%make-nothing))
    (define (nothing) %the-nothing)
    (define (just . objs) (%make-just objs))

    (define-record-type <right> (%make-right payload) right? (payload right-payload))
    (define-record-type <left> (%make-left payload) left? (payload left-payload))
    (define (right . objs) (%make-right objs))
    (define (left . objs) (%make-left objs))

    (define (maybe? obj) (or (just? obj) (nothing? obj)))
    (define (either? obj) (or (right? obj) (left? obj)))

    ;; list-copy: these containers are documented as immutable, so the
    ;; payload must not alias a list the caller can still mutate after
    ;; construction (a real bug, found by review: without copying,
    ;; `(set-car! lst 999)` after `(list->just lst)` silently mutated the
    ;; already-constructed Just's own payload).
    (define (list->just lst) (%make-just (list-copy lst)))
    (define (list->left lst) (%make-left (list-copy lst)))
    (define (list->right lst) (%make-right (list-copy lst)))

    ;; ── Conversions between Maybe and Either ────────────────────────────────

    (define (maybe->either m . objs)
      (if (just? m) (%make-right (just-payload m)) (%make-left objs)))
    (define (either->maybe e)
      (if (right? e) (%make-just (right-payload e)) (nothing)))
    (define (either-swap e)
      (if (right? e) (%make-left (right-payload e)) (%make-right (left-payload e))))

    ;; ── Equality ─────────────────────────────────────────────────────────────

    (define (%payload=? equal p1 p2)
      (and (= (length p1) (length p2))
           (let loop ((a p1) (b p2))
             (or (null? a) (and (equal (car a) (car b)) (loop (cdr a) (cdr b)))))))

    (define (%maybe-pair=? equal m1 m2)
      (cond
        ((and (nothing? m1) (nothing? m2)) #t)
        ((and (just? m1) (just? m2)) (%payload=? equal (just-payload m1) (just-payload m2)))
        (else #f)))
    (define (%either-pair=? equal e1 e2)
      (cond
        ((and (right? e1) (right? e2)) (%payload=? equal (right-payload e1) (right-payload e2)))
        ((and (left? e1) (left? e2)) (%payload=? equal (left-payload e1) (left-payload e2)))
        (else #f)))

    (define (maybe= equal . maybes)
      (or (null? maybes) (null? (cdr maybes))
          (and (%maybe-pair=? equal (car maybes) (cadr maybes))
               (apply maybe= equal (cdr maybes)))))
    (define (either= equal . eithers)
      (or (null? eithers) (null? (cdr eithers))
          (and (%either-pair=? equal (car eithers) (cadr eithers))
               (apply either= equal (cdr eithers)))))

    ;; ── Accessors ────────────────────────────────────────────────────────────

    (define (maybe-ref m failure . opt)
      (let ((success (if (pair? opt) (car opt) values)))
        (if (just? m) (apply success (just-payload m)) (failure))))
    ;; Either's failure branch is called ON the Left's own payload
    ;; (unlike maybe-ref's zero-arg failure) -- this is the spec's own
    ;; stated asymmetry, not an inconsistency in this implementation.
    (define (either-ref e failure . opt)
      (let ((success (if (pair? opt) (car opt) values)))
        (if (right? e) (apply success (right-payload e)) (apply failure (left-payload e)))))

    (define (maybe-ref/default m . defaults)
      (if (just? m) (apply values (just-payload m)) (apply values defaults)))
    (define (either-ref/default e . defaults)
      (if (right? e) (apply values (right-payload e)) (apply values defaults)))

    ;; ── Monadic operations ───────────────────────────────────────────────────

    (define (maybe-join m)
      (if (nothing? m)
          m
          (let ((p (just-payload m)))
            (if (and (pair? p) (null? (cdr p)) (maybe? (car p)))
                (car p)
                (error "maybe-join: expected a Just of exactly one Maybe value" m)))))
    (define (either-join e)
      (if (left? e)
          e
          (let ((p (right-payload e)))
            (if (and (pair? p) (null? (cdr p)) (either? (car p)))
                (car p)
                (error "either-join: expected a Right of exactly one Either value" e)))))

    (define (maybe-compose . procs)
      (if (null? procs)
          (error "maybe-compose: requires at least one procedure")
          (lambda args
            (let loop ((procs (cdr procs)) (m (apply (car procs) args)))
              (cond
                ((not (maybe? m)) (error "maybe-compose: a monadic function did not return a Maybe" m))
                ((or (null? procs) (nothing? m)) m)
                (else (loop (cdr procs) (apply (car procs) (just-payload m)))))))))
    (define (either-compose . procs)
      (if (null? procs)
          (error "either-compose: requires at least one procedure")
          (lambda args
            (let loop ((procs (cdr procs)) (e (apply (car procs) args)))
              (cond
                ((not (either? e)) (error "either-compose: a monadic function did not return an Either" e))
                ((or (null? procs) (left? e)) e)
                (else (loop (cdr procs) (apply (car procs) (right-payload e)))))))))

    (define (maybe-bind m . procs)
      (if (nothing? m) m (apply (apply maybe-compose procs) (just-payload m))))
    (define (either-bind e . procs)
      (if (left? e) e (apply (apply either-compose procs) (right-payload e))))

    ;; ── Sequence-flavored operations ─────────────────────────────────────────

    (define (maybe-length m) (if (just? m) 1 0))
    (define (either-length e) (if (right? e) 1 0))

    (define (maybe-filter pred m)
      (if (and (just? m) (apply pred (just-payload m))) m (nothing)))
    (define (maybe-remove pred m)
      (if (and (just? m) (not (apply pred (just-payload m)))) m (nothing)))
    (define (either-filter pred e . objs)
      (if (and (right? e) (apply pred (right-payload e))) e (apply left objs)))
    (define (either-remove pred e . objs)
      (if (and (right? e) (not (apply pred (right-payload e)))) e (apply left objs)))

    ;; mappable/map: map's own signature is (map proc mappable) -- e.g.
    ;; passing curry's own `map` and a list. Each element is itself a
    ;; Maybe/Either; a Nothing/Left anywhere short-circuits the whole
    ;; traversal to that object; every Just/Right's unwrapped payload is
    ;; passed to `aggregator` (default `list`).
    ;;
    ;; The mapping callback below is a PURE function of its own single
    ;; argument, with no shared mutable state across calls -- required
    ;; because `map` is caller-supplied and, if it's curry's own core
    ;; `map`, auto-parallelizes across worker threads above 8 elements;
    ;; a callback that closed over shared mutable state to track
    ;; short-circuiting (an earlier draft of this file did exactly that)
    ;; would be exactly the hazard independent review already found and
    ;; fixed once this session in (srfi 225)'s own dict-for-each.
    ;; Finding the first failure and re-collecting the successes is a
    ;; separate, genuinely sequential pass over map's own already-
    ;; computed return value -- assumed here to be a proper list (true
    ;; for the common case of a list `mappable` and the ordinary list
    ;; `map`; a `mappable`/`map` pair that returns some other collection
    ;; shape, e.g. vector-map's own vector, isn't specifically supported
    ;; by this second pass).
    ;; Returns either the first Nothing/Left found in lst (unwrapped, as
    ;; a short-circuit signal), or a PLAIN, un-wrapped list of every
    ;; element unchanged -- maybe-sequence/either-sequence do the actual
    ;; just/right wrapping exactly once, after this returns. (Wrapping
    ;; the intermediate accumulator itself in `just`/`right` here would
    ;; be a real bug: `(just some-list)` makes some-list a single
    ;; PAYLOAD VALUE -- payload becomes (list some-list), one element --
    ;; not "a Just whose payload IS some-list's own elements".)
    (define (%maybe-collect-failure lst)
      (cond
        ((null? lst) '())
        ((nothing? (car lst)) (car lst))
        (else (let ((rest (%maybe-collect-failure (cdr lst))))
                (if (nothing? rest) rest (cons (car lst) rest))))))
    (define (%either-collect-failure lst)
      (cond
        ((null? lst) '())
        ((left? (car lst)) (car lst))
        (else (let ((rest (%either-collect-failure (cdr lst))))
                (if (left? rest) rest (cons (car lst) rest))))))

    (define (maybe-sequence mappable map . opt)
      (let* ((aggregator (if (pair? opt) (car opt) list))
             (result (%maybe-collect-failure
                       (map (lambda (elt) (if (nothing? elt) elt (apply aggregator (just-payload elt)))) mappable))))
        (if (nothing? result) result (just result))))
    (define (either-sequence mappable map . opt)
      (let* ((aggregator (if (pair? opt) (car opt) list))
             (result (%either-collect-failure
                       (map (lambda (elt) (if (left? elt) elt (apply aggregator (right-payload elt)))) mappable))))
        (if (left? result) result (right result))))

    ;; ── Protocol conversions ─────────────────────────────────────────────────
    ;; Six protocols, each translating a Maybe/Either payload to/from a
    ;; different "how does ordinary Scheme code spell success/failure"
    ;; convention -- list (empty = failure), truth (#f = failure),
    ;; list-truth (#f = failure, else a list of values), generation
    ;; (eof-object = failure), values (zero returned values = failure),
    ;; two-values (a second #f flag = failure), plus the exception
    ;; protocol (a raised condition = failure).

    ;; Copying both on the way in (list->maybe/list->either, matching
    ;; list->just/list->left/list->right above) and on the way out
    ;; (maybe->list/either->list) -- an extracted list handed back to
    ;; caller code is just as capable of being mutated afterward as a
    ;; caller-supplied one is of being mutated before construction, and
    ;; both would otherwise corrupt this "immutable" container's own
    ;; internal state the same way.
    (define (maybe->list m) (if (just? m) (list-copy (just-payload m)) '()))
    (define (either->list e) (if (right? e) (list-copy (right-payload e)) '()))
    (define (list->maybe lst) (if (null? lst) (nothing) (%make-just (list-copy lst))))
    (define (list->either lst . objs) (if (null? lst) (apply left objs) (%make-right (list-copy lst))))

    (define (maybe->truth m) (if (just? m) (car (just-payload m)) #f))
    (define (either->truth e) (if (right? e) (car (right-payload e)) #f))
    (define (truth->maybe obj) (if obj (just obj) (nothing)))
    (define (truth->either obj . objs) (if obj (right obj) (apply left objs)))

    (define (maybe->list-truth m) (if (just? m) (list-copy (just-payload m)) #f))
    (define (either->list-truth e) (if (right? e) (list-copy (right-payload e)) #f))
    (define (list-truth->maybe lst-or-f) (if lst-or-f (%make-just (list-copy lst-or-f)) (nothing)))
    (define (list-truth->either lst-or-f . objs)
      (if lst-or-f (%make-right (list-copy lst-or-f)) (apply left objs)))

    (define (maybe->generation m) (if (just? m) (car (just-payload m)) (eof-object)))
    (define (either->generation e) (if (right? e) (car (right-payload e)) (eof-object)))
    (define (generation->maybe obj) (if (eof-object? obj) (nothing) (just obj)))
    (define (generation->either obj . objs) (if (eof-object? obj) (apply left objs) (right obj)))

    (define (maybe->values m) (if (just? m) (apply values (just-payload m)) (values)))
    (define (either->values e) (if (right? e) (apply values (right-payload e)) (values)))
    (define (values->maybe producer)
      (call-with-values producer (lambda vs (if (null? vs) (nothing) (list->just vs)))))
    (define (values->either producer . objs)
      (call-with-values producer (lambda vs (if (null? vs) (apply left objs) (list->right vs)))))

    (define (maybe->two-values m) (if (just? m) (values (car (just-payload m)) #t) (values #f #f)))
    (define (two-values->maybe producer)
      (call-with-values producer (lambda (v ok?) (if ok? (just v) (nothing)))))

    (define (exception->either pred thunk)
      (guard (e (#t (if (pred e) (left e) (raise e))))
        (call-with-values thunk right)))

    ;; ── Map / for-each / fold ────────────────────────────────────────────────

    (define (maybe-map proc m) (if (just? m) (%make-just (call-with-values (lambda () (apply proc (just-payload m))) list)) m))
    (define (either-map proc e) (if (right? e) (%make-right (call-with-values (lambda () (apply proc (right-payload e))) list)) e))
    (define (maybe-for-each proc m) (if (just? m) (apply proc (just-payload m))) (if #f #f))
    (define (either-for-each proc e) (if (right? e) (apply proc (right-payload e))) (if #f #f))

    (define (maybe-fold kons nil m) (if (just? m) (apply kons (append (just-payload m) (list nil))) nil))
    (define (either-fold kons nil e) (if (right? e) (apply kons (append (right-payload e) (list nil))) nil))

    ;; maybe-unfold/either-unfold: the spec's own text (quoted verbatim
    ;; here, since it reads unusually restrictively and is easy to
    ;; mis-paraphrase): "If stop? returns true on seeds, a Nothing / a
    ;; Left of seeds is returned. Otherwise, successor is applied to
    ;; seeds. If stop? returns false on the results of successor, it is
    ;; an error. But if the second call to stop? returns true, mapper is
    ;; applied to seeds and the results are wrapped in a Just/Right and
    ;; returned." This is NOT a general recursive unfold loop (unlike
    ;; SRFI-1's own `unfold`) -- stop? is called at most twice total.
    ;; mapper is applied to the ORIGINAL seeds, not the successor's
    ;; result -- confirmed against the SRFI's own reference
    ;; implementation (`(mapper (car seeds))`, successor's own return
    ;; value used only to satisfy the second stop? check and otherwise
    ;; discarded), not the "applied to seeds means the updated ones"
    ;; reading this file's own first draft used (a real bug, found by
    ;; review, since fixed).
    (define (maybe-unfold stop? mapper successor . seeds)
      (if (apply stop? seeds)
          (nothing)
          (let ((seeds2 (call-with-values (lambda () (apply successor seeds)) list)))
            (if (not (apply stop? seeds2))
                (error "maybe-unfold: successor's result does not satisfy stop? on the second check" seeds2)
                (list->just (call-with-values (lambda () (apply mapper seeds)) list))))))
    (define (either-unfold stop? mapper successor . seeds)
      (if (apply stop? seeds)
          (apply left seeds)
          (let ((seeds2 (call-with-values (lambda () (apply successor seeds)) list)))
            (if (not (apply stop? seeds2))
                (error "either-unfold: successor's result does not satisfy stop? on the second check" seeds2)
                (list->right (call-with-values (lambda () (apply mapper seeds)) list))))))

    ;; ── Syntax ───────────────────────────────────────────────────────────────

    (define-syntax maybe-if
      (syntax-rules ()
        ((_ maybe-expr just-expr nothing-expr)
         (let ((m maybe-expr))
           (cond ((just? m) just-expr)
                 ((nothing? m) nothing-expr)
                 (else (error "maybe-if: expected a Maybe" m)))))))

    (define-syntax maybe-and
      (syntax-rules ()
        ((_ e) e)
        ((_ e1 e2 ...) (let ((t e1)) (if (nothing? t) t (maybe-and e2 ...))))))
    (define-syntax either-and
      (syntax-rules ()
        ((_ e) e)
        ((_ e1 e2 ...) (let ((t e1)) (if (left? t) t (either-and e2 ...))))))
    (define-syntax maybe-or
      (syntax-rules ()
        ((_ e) e)
        ((_ e1 e2 ...) (let ((t e1)) (if (just? t) t (maybe-or e2 ...))))))
    (define-syntax either-or
      (syntax-rules ()
        ((_ e) e)
        ((_ e1 e2 ...) (let ((t e1)) (if (right? t) t (either-or e2 ...))))))

    ;; A claw is a bound identifier, (expr), or (var expr) -- each
    ;; unwraps a single-valued Just/Right, short-circuiting the whole
    ;; form to the Nothing/Left it hit otherwise. (var expr)/bare-id
    ;; claws require the unwrapped Just/Right to hold EXACTLY one value
    ;; (maybe-let*-values, below, relaxes this).
    (define-syntax maybe-let*
      (syntax-rules ()
        ((_ () body1 body2 ...) (let () body1 body2 ...))
        ((_ ((var expr) rest ...) body ...)
         (let ((t expr))
           (if (nothing? t) t
               (let ((p (just-payload t)))
                 (if (or (null? p) (pair? (cdr p)))
                     (error "maybe-let*: claw must produce exactly one value" t)
                     (let ((var (car p))) (maybe-let* (rest ...) body ...)))))))
        ((_ ((expr) rest ...) body ...)
         (let ((t expr)) (if (nothing? t) t (maybe-let* (rest ...) body ...))))
        ((_ (id rest ...) body ...)
         (let ((t id))
           (if (nothing? t) t
               (let ((p (just-payload t)))
                 (if (or (null? p) (pair? (cdr p)))
                     (error "maybe-let*: claw must produce exactly one value" t)
                     (let ((id (car p))) (maybe-let* (rest ...) body ...)))))))))

    (define-syntax either-let*
      (syntax-rules ()
        ((_ () body1 body2 ...) (let () body1 body2 ...))
        ((_ ((var expr) rest ...) body ...)
         (let ((t expr))
           (if (left? t) t
               (let ((p (right-payload t)))
                 (if (or (null? p) (pair? (cdr p)))
                     (error "either-let*: claw must produce exactly one value" t)
                     (let ((var (car p))) (either-let* (rest ...) body ...)))))))
        ((_ ((expr) rest ...) body ...)
         (let ((t expr)) (if (left? t) t (either-let* (rest ...) body ...))))
        ((_ (id rest ...) body ...)
         (let ((t id))
           (if (left? t) t
               (let ((p (right-payload t)))
                 (if (or (null? p) (pair? (cdr p)))
                     (error "either-let*: claw must produce exactly one value" t)
                     (let ((id (car p))) (either-let* (rest ...) body ...)))))))))

    ;; maybe-let*-values / either-let*-values: same claw shapes, but a
    ;; (var expr) claw's var may also be a formals list (proper or
    ;; dotted), binding ALL of the unwrapped Just/Right's payload values
    ;; via apply -- the SRFI's own text says only "supports multiple
    ;; value unpacking" without spelling out the exact claw grammar;
    ;; mirroring R7RS let-values' own claw shape is the natural reading.
    (define-syntax maybe-let*-values
      (syntax-rules ()
        ((_ () body1 body2 ...) (let () body1 body2 ...))
        ((_ ((formals expr) rest ...) body ...)
         (let ((t expr))
           (if (nothing? t) t
               (apply (lambda formals (maybe-let*-values (rest ...) body ...)) (just-payload t)))))
        ((_ ((expr) rest ...) body ...)
         (let ((t expr)) (if (nothing? t) t (maybe-let*-values (rest ...) body ...))))
        ((_ (id rest ...) body ...)
         (let ((t id))
           (if (nothing? t) t
               (apply (lambda id (maybe-let*-values (rest ...) body ...)) (just-payload t)))))))

    (define-syntax either-let*-values
      (syntax-rules ()
        ((_ () body1 body2 ...) (let () body1 body2 ...))
        ((_ ((formals expr) rest ...) body ...)
         (let ((t expr))
           (if (left? t) t
               (apply (lambda formals (either-let*-values (rest ...) body ...)) (right-payload t)))))
        ((_ ((expr) rest ...) body ...)
         (let ((t expr)) (if (left? t) t (either-let*-values (rest ...) body ...))))
        ((_ (id rest ...) body ...)
         (let ((t id))
           (if (left? t) t
               (apply (lambda id (either-let*-values (rest ...) body ...)) (right-payload t)))))))

    (define-syntax either-guard
      (syntax-rules ()
        ((_ pred-expr body ...)
         (exception->either pred-expr (lambda () body ...)))))

    ;; ── Trivalent logic: a Maybe wrapping a single boolean ──────────────────

    (define (tri-not m)
      (if (nothing? m) m (just (not (car (just-payload m))))))

    (define (%tri-value m) (and (just? m) (car (just-payload m))))

    ;; "Just #t if all the maybes are true or if all are false. Otherwise,
    ;; if any maybe is Nothing or any two maybes have different
    ;; (trivalent) truth values, returns Just #f." Any Nothing ANYWHERE
    ;; -- including every argument being Nothing -- means Just #f, no
    ;; exception for "all Nothing" (confirmed against the SRFI's own
    ;; reference implementation; a real bug, found by review, in an
    ;; earlier draft of this file treated "all Nothing" as agreement and
    ;; returned Just #t).
    (define (%any-nothing? ms) (and (pair? ms) (or (nothing? (car ms)) (%any-nothing? (cdr ms)))))
    (define (tri=? . ms)
      (cond
        ((null? ms) (just #t))
        ((%any-nothing? ms) (just #f))
        (else (let ((first-val (%tri-value (car ms))))
                (just (let loop ((rest (cdr ms)))
                        (or (null? rest)
                            (and (eq? (%tri-value (car rest)) first-val) (loop (cdr rest))))))))))

    (define (tri-and . ms)
      (let loop ((ms ms))
        (cond
          ((null? ms) (just #t))
          ((nothing? (car ms)) (car ms))
          ((eq? (%tri-value (car ms)) #f) (car ms))
          (else (loop (cdr ms))))))

    (define (tri-or . ms)
      (let loop ((ms ms))
        (cond
          ((null? ms) (just #f))
          ((nothing? (car ms)) (car ms))
          ((eq? (%tri-value (car ms)) #f) (loop (cdr ms)))
          (else (car ms)))))

    (define (tri-merge . ms)
      (let loop ((ms ms))
        (cond
          ((null? ms) (nothing))
          ((just? (car ms)) (car ms))
          (else (loop (cdr ms))))))))
