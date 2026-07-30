(define-library (srfi s158 generators-and-accumulators)
  (import (scheme base) (curry sync))
  (export
    generator make-coroutine-generator make-for-each-generator
    list->generator vector->generator string->generator
    make-range-generator make-iota-generator circular-generator
    generator->list generator->vector generator->string
    gtake gdrop gappend gcons* gmap gfilter gremove gzip gflatten
    generator-map->list generator-fold generator-for-each generator-each
    generator-count generator-any generator-every generator-find generator-length
    make-accumulator count-accumulator list-accumulator reverse-list-accumulator
    vector-accumulator sum-accumulator product-accumulator)
  (begin

    ;; A generator is a thunk: each call returns the next value, or the
    ;; eof-object once exhausted. All combinators below build directly on
    ;; that representation. make-coroutine-generator is the one exception —
    ;; turning an arbitrary producer procedure (that calls a `yield`) into a
    ;; generator genuinely needs suspend/resume, which curry's escape-only
    ;; continuations (setjmp/longjmp, no CPS) can't provide; it's built
    ;; instead on a real OS thread (curry's actor `spawn`) handed off via a
    ;; (curry sync) mutex/condvar rendezvous, one value at a time.

    (define (generator . vs)
      (lambda ()
        (if (null? vs) (eof-object)
            (let ((v (car vs))) (set! vs (cdr vs)) v))))

    (define (list->generator lst) (apply generator lst))
    (define (vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (vector-ref v i))) (set! i (+ i 1)) x)))))
    (define (string->generator s . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (string-length s)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((c (string-ref s i))) (set! i (+ i 1)) c)))))

    (define (make-range-generator start . rest)
      (let ((end (if (pair? rest) (car rest) #f))
            (step (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) 1))
            (i start))
        (lambda ()
          (if (and end (if (> step 0) (>= i end) (<= i end)))
              (eof-object)
              (let ((v i)) (set! i (+ i step)) v)))))

    (define (make-iota-generator count . rest)
      (let ((start (if (pair? rest) (car rest) 0))
            (step (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) 1)))
        (make-range-generator start (+ start (* count step)) step)))

    (define (circular-generator . vs)
      (let ((all vs) (rest vs))
        (lambda ()
          (if (null? all) (eof-object)
              (begin (if (null? rest) (set! rest all))
                     (let ((v (car rest))) (set! rest (cdr rest)) v))))))

    ;; ------------------------------------------------------------------
    ;; make-coroutine-generator — real-thread coroutine via mutex/condvar
    ;; ping-pong handoff (see header comment).
    ;; ------------------------------------------------------------------

    (define (make-coroutine-generator proc)
      (define mx (make-mutex))
      (define cv (make-condvar))
      ; state: #(value status turn) — status: 'pending | 'yielded | 'done
      ;                                turn:   'producer | 'consumer
      (define state (vector #f 'pending 'consumer))

      (define (wait-until pred)
        (let loop () (if (not (pred)) (begin (cond-wait! cv mx) (loop)))))

      (define (yield v)
        (with-mutex mx (lambda ()
          (vector-set! state 0 v)
          (vector-set! state 1 'yielded)
          (vector-set! state 2 'consumer)
          (cond-signal! cv)
          (wait-until (lambda () (eq? (vector-ref state 2) 'producer)))))
        #f)

      ; guard, not just a plain call: if `proc` raises, the "mark done and
      ; wake the consumer" block below must still run — otherwise a caller
      ; blocked in the generator thunk (or any later call) would wait on a
      ; turn that will never change, deadlocking forever.
      (spawn (lambda ()
               (with-mutex mx (lambda () (wait-until (lambda () (eq? (vector-ref state 2) 'producer)))))
               (guard (e (#t #f)) (proc yield))
               (with-mutex mx (lambda ()
                 (vector-set! state 1 'done)
                 (vector-set! state 2 'consumer)
                 (cond-signal! cv)))))

      (lambda ()
        (with-mutex mx (lambda ()
          (if (eq? (vector-ref state 1) 'done)
              (eof-object)
              (begin
                (vector-set! state 2 'producer)
                (cond-signal! cv)
                (wait-until (lambda () (eq? (vector-ref state 2) 'consumer)))
                (if (eq? (vector-ref state 1) 'done) (eof-object) (vector-ref state 0))))))))

    (define (make-for-each-generator for-each-proc collection)
      (make-coroutine-generator (lambda (yield) (for-each-proc yield collection))))

    ;; ------------------------------------------------------------------
    ;; Consumers
    ;; ------------------------------------------------------------------

    (define (generator->list gen . n)
      (let loop ((acc '()) (k (if (pair? n) (car n) #f)))
        (if (eqv? k 0)
            (reverse acc)
            (let ((v (gen)))
              (if (eof-object? v) (reverse acc) (loop (cons v acc) (and k (- k 1))))))))

    (define (generator->vector gen . n) (list->vector (apply generator->list gen n)))
    (define (generator->string gen . n) (list->string (apply generator->list gen n)))

    (define (generator-length gen) (length (generator->list gen)))

    (define (gtake gen n)
      (let ((remaining n))
        (lambda ()
          (if (<= remaining 0) (eof-object)
              (let ((v (gen)))
                (if (eof-object? v) (begin (set! remaining 0) v)
                    (begin (set! remaining (- remaining 1)) v)))))))

    (define (gdrop gen n)
      (let loop ((i 0)) (if (< i n) (begin (gen) (loop (+ i 1)))))
      gen)

    (define (gappend . gens)
      (let ((remaining gens))
        (lambda ()
          (let loop ()
            (if (null? remaining)
                (eof-object)
                (let ((v ((car remaining))))
                  (if (eof-object? v) (begin (set! remaining (cdr remaining)) (loop)) v)))))))

    (define (gcons* . args)
      (let ((vs (reverse (cdr (reverse args)))) (gen (car (reverse args))))
        (apply gappend (append (map (lambda (v) (generator v)) vs) (list gen)))))

    (define (gmap proc . gens)
      (lambda ()
        (let ((vs (map (lambda (g) (g)) gens)))
          (if (any-eof? vs) (eof-object) (apply proc vs)))))

    (define (any-eof? vs) (and (pair? vs) (or (eof-object? (car vs)) (any-eof? (cdr vs)))))

    (define (gfilter pred gen)
      (lambda ()
        (let loop ()
          (let ((v (gen)))
            (cond ((eof-object? v) v) ((pred v) v) (else (loop)))))))

    (define (gremove pred gen) (gfilter (lambda (v) (not (pred v))) gen))

    (define (gzip . gens) (apply gmap list gens))

    (define (gflatten gen-of-lists)
      (let ((buf '()))
        (lambda ()
          (let loop ()
            (cond ((pair? buf) (let ((v (car buf))) (set! buf (cdr buf)) v))
                  (else (let ((next (gen-of-lists)))
                          (cond ((eof-object? next) next)
                                (else (set! buf next) (loop))))))))))

    (define (generator-map->list proc gen)
      (let loop ((acc '()))
        (let ((v (gen)))
          (if (eof-object? v) (reverse acc) (loop (cons (proc v) acc))))))

    (define (generator-fold proc knil gen)
      (let loop ((acc knil))
        (let ((v (gen)))
          (if (eof-object? v) acc (loop (proc v acc))))))

    (define (generator-for-each proc gen)
      (let loop ()
        (let ((v (gen)))
          (if (not (eof-object? v)) (begin (proc v) (loop))))))

    (define generator-each generator-for-each)

    (define (generator-count pred gen)
      (generator-fold (lambda (v acc) (if (pred v) (+ acc 1) acc)) 0 gen))

    (define (generator-any pred gen)
      (let loop ()
        (let ((v (gen)))
          (cond ((eof-object? v) #f) ((pred v) #t) (else (loop))))))

    (define (generator-every pred gen)
      (let loop ()
        (let ((v (gen)))
          (cond ((eof-object? v) #t) ((not (pred v)) #f) (else (loop))))))

    (define (generator-find pred gen)
      (let loop ()
        (let ((v (gen)))
          (cond ((eof-object? v) #f) ((pred v) v) (else (loop))))))

    ;; ------------------------------------------------------------------
    ;; Accumulators — a procedure of one argument that updates internal
    ;; state as a side effect and returns the accumulated value so far.
    ;; ------------------------------------------------------------------

    (define (make-accumulator kons knil . finalizer)
      (let ((state knil) (fin (if (pair? finalizer) (car finalizer) (lambda (x) x))))
        (lambda (v)
          (if (eof-object? v)
              (fin state)
              (begin (set! state (kons v state)) (fin state))))))

    (define (count-accumulator) (make-accumulator (lambda (v acc) (+ acc 1)) 0))
    (define (list-accumulator) (make-accumulator cons '() reverse))
    (define (reverse-list-accumulator) (make-accumulator cons '()))
    (define (sum-accumulator) (make-accumulator + 0))
    (define (product-accumulator) (make-accumulator * 1))

    (define (vector-accumulator)
      (make-accumulator (lambda (v acc) (cons v acc)) '() (lambda (acc) (list->vector (reverse acc)))))))
