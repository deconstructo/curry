(define-library (srfi s210 multiple-values)
  (import (scheme base) (srfi s195 multiple-value-boxes))
  (export
    ; Syntax
    apply/mv call/mv list/mv vector/mv box/mv value/mv coarity
    set!-values with-values case-receive bind/mv
    ; Procedures
    list-values vector-values box-values value identity
    compose-left compose-right map-values
    bind/list bind/box bind
    ; Internal helpers referenced directly from exported macro expansions.
    ; Not part of the SRFI-210 API — exported only because curry's
    ; syntax-rules macros resolve template identifiers in the use-site
    ; environment rather than the definition-site environment.
    %value-mv-ref %sv-assign %cr-dispatch %cr-arity-matches? %chain-transducers
    %call-mv-run)
  (begin

    ; SRFI-210 layers convenience syntax/procedures for multiple-value
    ; programming on top of plain call-with-values -- nothing here needs
    ; VM support beyond what call-with-values (and, for box/mv, box-values,
    ; and bind/box, (srfi s195 multiple-value-boxes)) already provides.
    ;
    ; Known limitation shared with call-with-values itself (see
    ; src/compiler.c's compile_let_values doc comment): curry's
    ; call-with-values primitive invokes its consumer via a real nested C
    ; call rather than a genuine bytecode-level tail call, so none of the
    ; "tail-calls" wording in the forms below actually gets proper TCO --
    ; deep recursion through apply/mv, call/mv, with-values, bind/mv, etc.
    ; in a self-recursive loop's tail position will eventually hit curry's
    ; call-stack limit rather than looping forever. This is a VM-level gap
    ; tracked separately, not specific to this library.

    ; ── syntax ──────────────────────────────────────────────────────────

    (define-syntax apply/mv
      (syntax-rules ()
        ((_ operator operand ... producer)
         (call-with-values (lambda () producer)
           (lambda vals (apply operator operand ... vals))))))

    ; Deliberately NOT a recursive macro building nested (lambda vals ...)
    ; consumer lambdas, one per producer (the first attempt at this used
    ; exactly that and was wrong): curry's macro expansion does not
    ; hygienically rename a template-introduced binding per recursive
    ; expansion (the same gap found in (srfi s209 enums)'s registry
    ; design), so reusing the literal name `vals` at every nesting level
    ; means each deeper `(lambda vals ...)` silently shadows the outer
    ; one -- by the time the accumulated list is finally used, EVERY
    ; reference to `vals` resolves to the innermost (last producer's)
    ; binding, not each producer's own values. Confirmed by a failing
    ; test: (call/mv list (values 1) (values 2 3) (values 4)) returned
    ; (4 4 4) instead of (1 2 3 4). Sidestepped entirely by not
    ; introducing any named binding per producer at all -- each producer
    ; expression becomes a zero-argument thunk (no parameters to shadow
    ; anything), and the actual collection/concatenation of values
    ; happens in ordinary runtime code operating on a plain list of those
    ; thunks.
    (define (%call-mv-run consumer thunks)
      (apply consumer (apply append (map (lambda (th) (call-with-values th list)) thunks))))

    (define-syntax call/mv
      (syntax-rules ()
        ((_ consumer producer ...)
         (%call-mv-run consumer (list (lambda () producer) ...)))))

    (define-syntax list/mv
      (syntax-rules ()
        ((_ element ... producer)
         (call-with-values (lambda () producer)
           (lambda vals (append (list element ...) vals))))))

    (define-syntax vector/mv
      (syntax-rules ()
        ((_ element ... producer)
         (call-with-values (lambda () producer)
           (lambda vals (list->vector (append (list element ...) vals)))))))

    (define-syntax box/mv
      (syntax-rules ()
        ((_ element ... producer)
         (call-with-values (lambda () producer)
           (lambda vals (apply box (append (list element ...) vals)))))))

    (define (%value-mv-ref index lst)
      (if (and (exact-integer? index) (>= index 0) (< index (length lst)))
          (list-ref lst index)
          (error "value/mv: index out of range" index)))

    (define-syntax value/mv
      (syntax-rules ()
        ((_ index operand ... producer)
         (call-with-values (lambda () producer)
           (lambda vals (%value-mv-ref index (append (list operand ...) vals)))))))

    (define-syntax coarity
      (syntax-rules ()
        ((_ producer) (call-with-values (lambda () producer) (lambda vals (length vals))))))

    ; set!-values: rather than reconstructing `formals`' exact shape with
    ; fresh temp identifiers (as compile_let_values does in C, where the
    ; formals shape has to double as an actual lambda parameter list), the
    ; consumer lambda here takes a single fixed rest-arg name
    ; (`%sv-all-vals`, chosen obscure enough not to collide with real code)
    ; and %sv-assign walks `formals` positionally against that vals list
    ; directly, emitting ordinary (set! real-name ...) forms -- since the
    ; consumer lambda's only parameter is that one synthetic name, `x`/`y`/
    ; etc. inside its body still refer to the OUTER (to-be-mutated)
    ; bindings, not something newly shadowed.
    (define-syntax %sv-assign
      (syntax-rules ()
        ((_ () vals) (if #f #f))
        ((_ (x . rest) vals) (begin (set! x (car vals)) (%sv-assign rest (cdr vals))))
        ((_ x vals) (set! x vals))))

    (define-syntax set!-values
      (syntax-rules ()
        ((_ formals producer)
         (call-with-values (lambda () producer)
           (lambda %sv-all-vals (%sv-assign formals %sv-all-vals))))))

    (define-syntax with-values
      (syntax-rules ()
        ((_ producer consumer) (call-with-values (lambda () producer) consumer))))

    ; case-receive: each clause's formals shape is checked for arity
    ; compatibility with the actual produced value count, first match
    ; wins, then a genuine (lambda formals body ...) built from that same
    ; clause is applied to the full values list -- no shadowing concerns
    ; here (unlike let-values), since each clause is an independent,
    ; freshly-built, immediately-applied lambda.
    (define (%cr-arity-matches? formals vals)
      (let ((n (length vals)))
        (let loop ((f formals) (count 0))
          (cond ((null? f) (= count n))
                ((pair? f) (loop (cdr f) (+ count 1)))
                (else (>= n count))))))

    (define-syntax %cr-dispatch
      (syntax-rules ()
        ((_ vals) (error "case-receive: no clause matched the produced values" vals))
        ((_ vals (formals body ...) rest ...)
         (if (%cr-arity-matches? 'formals vals)
             (apply (lambda formals body ...) vals)
             (%cr-dispatch vals rest ...)))))

    (define-syntax case-receive
      (syntax-rules ()
        ((_ producer clause ...)
         (call-with-values (lambda () producer)
           (lambda %cr-all-vals (%cr-dispatch %cr-all-vals clause ...))))))

    (define-syntax bind/mv
      (syntax-rules ()
        ((_ producer) producer)
        ((_ producer transducer1 transducer2 ...)
         (call-with-values (lambda () producer)
           (lambda vals (bind/mv (apply transducer1 vals) transducer2 ...))))))

    ; ── procedures ──────────────────────────────────────────────────────

    (define (list-values lst)
      (unless (list? lst) (error "list-values: not a list" lst))
      (apply values lst))

    (define (vector-values vec)
      (unless (vector? vec) (error "vector-values: not a vector" vec))
      (apply values (vector->list vec)))

    (define (box-values b)
      (unless (box? b) (error "box-values: not a box" b))
      (unbox b))

    (define (value index . objs)
      (if (and (exact-integer? index) (>= index 0) (< index (length objs)))
          (list-ref objs index)
          (error "value: index out of range" index)))

    (define (identity . objs) (apply values objs))

    (define (compose-left . transducers)
      (for-each (lambda (t) (unless (procedure? t) (error "compose-left: not a procedure" t)))
                transducers)
      (lambda args
        (let loop ((ts transducers) (vals args))
          (if (null? ts)
              (apply values vals)
              (call-with-values (lambda () (apply (car ts) vals))
                (lambda new-vals (loop (cdr ts) new-vals)))))))

    (define (compose-right . transducers) (apply compose-left (reverse transducers)))

    (define (map-values proc)
      (unless (procedure? proc) (error "map-values: not a procedure" proc))
      (lambda args (apply values (map proc args))))

    (define (%chain-transducers vals transducers)
      (if (null? transducers)
          (apply values vals)
          (call-with-values (lambda () (apply (car transducers) vals))
            (lambda new-vals (%chain-transducers new-vals (cdr transducers))))))

    (define (bind/list lst . transducers)
      (unless (list? lst) (error "bind/list: not a list" lst))
      (for-each (lambda (t) (unless (procedure? t) (error "bind/list: not a procedure" t)))
                transducers)
      (%chain-transducers lst transducers))

    (define (bind/box b . transducers)
      (unless (box? b) (error "bind/box: not a box" b))
      (for-each (lambda (t) (unless (procedure? t) (error "bind/box: not a procedure" t)))
                transducers)
      (%chain-transducers (call-with-values (lambda () (unbox b)) list) transducers))

    (define (bind obj . transducers) (%chain-transducers (list obj) transducers))))
