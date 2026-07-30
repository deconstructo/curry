(define-library (srfi s18 multithreading)
  (import (scheme base) (curry sync))
  (export
    current-thread thread? make-thread thread-name
    thread-start! thread-yield! thread-sleep! thread-join! thread-terminate!
    thread-specific thread-specific-set!
    make-mutex mutex? mutex-lock! mutex-unlock!
    make-condition-variable condition-variable?
    condition-variable-signal! condition-variable-broadcast!
    join-timeout-exception? terminated-thread-exception?)
  (begin

    ; curry's only real-thread primitive is the actor system (spawn/send!/
    ; receive) — (curry sync)'s own comment says as much, and intentionally
    ; doesn't expose pthread_create. SRFI-18 threads here are a thin wrapper:
    ; make-thread stores a thunk (SRFI-18 threads are created suspended);
    ; thread-start! is what actually calls curry's `spawn`. Join and sleep
    ; are both implemented on top of a private mutex/condvar rendezvous —
    ; thread-sleep! in particular is just a cond-wait-timeout! that nothing
    ; ever signals, which pthread_cond_timedwait treats as a plain timed
    ; sleep. thread-terminate! is unsupported (curry's actors are detached
    ; pthreads with no cancellation hook) and always raises.

    (define-record-type <srfi18-thread>
      (%make-thread thunk name state)
      thread?
      (thunk %thread-thunk)
      (name  thread-name)
      (state %thread-state)) ; #(started? done? result mutex condvar specific)

    (define (make-thread thunk . name)
      (%make-thread thunk (if (pair? name) (car name) #f)
                    (vector #f #f #f (make-mutex) (make-condition-variable) #f)))

    (define (current-thread) #f) ; no reified handle for the calling thread/main

    (define (thread-specific t) (vector-ref (%thread-state t) 5))
    (define (thread-specific-set! t v) (vector-set! (%thread-state t) 5 v))

    (define (thread-start! t)
      (let ((state (%thread-state t)))
        (with-mutex (vector-ref state 3)
          (lambda ()
            (if (not (vector-ref state 0))
                (begin
                  (vector-set! state 0 #t)
                  (spawn (lambda ()
                           (let ((result ((%thread-thunk t))))
                             (with-mutex (vector-ref state 3)
                               (lambda ()
                                 (vector-set! state 2 result)
                                 (vector-set! state 1 #t)
                                 (condition-variable-broadcast! (vector-ref state 4)))))))))))
        t))

    (define (thread-join! t . rest)
      (let ((state (%thread-state t)))
        (with-mutex (vector-ref state 3)
          (lambda ()
            (let loop ()
              (if (not (vector-ref state 1))
                  (begin (cond-wait! (vector-ref state 4) (vector-ref state 3)) (loop))))
            (vector-ref state 2)))))

    (define (thread-sleep! seconds)
      (let ((mx (make-mutex)) (cv (make-condition-variable)))
        (with-mutex mx (lambda () (cond-wait-timeout! cv mx seconds)))))

    (define (thread-yield!) (thread-sleep! 0))

    (define (thread-terminate! t)
      (error "thread-terminate!: unsupported — curry's actor threads are detached and non-cancellable" t))

    (define (join-timeout-exception? obj) #f)
    (define (terminated-thread-exception? obj) #f)

    (define make-condition-variable make-condvar)
    (define condition-variable? condvar?)
    (define condition-variable-signal! cond-signal!)
    (define condition-variable-broadcast! cond-broadcast!)))
