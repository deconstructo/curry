;;; (curry sync): read-write locks, once and barriers (A3).
;;;
;;; Results from spawned actors go through globals: a set! on a captured local
;;; does not reach the spawning thread (#448).

(import (scheme base) (curry sync))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))

;; Sleep a little, without a sleep procedure: a timed wait that nobody signals.
(define nap-mx (make-mutex))
(define nap-cv (make-condvar))
(define (nap seconds)
  (mutex-lock! nap-mx)
  (cond-wait-timeout! nap-cv nap-mx seconds)
  (mutex-unlock! nap-mx))

(define done (make-semaphore 0))
(define (wait-for n) (let loop ((i 0)) (when (< i n) (sem-wait! done) (loop (+ i 1)))))

;;; ---- rwlock ----
(define rw (make-rwlock))
(check "rwlock?" (rwlock? rw) #t)
(check "rwlock? on something else" (rwlock? (make-mutex)) #f)
(check "two readers at once"
       (begin (rwlock-read-lock! rw) (rwlock-read-lock! rw)
              (let ((w (rwlock-try-write-lock! rw)))
                (rwlock-unlock! rw) (rwlock-unlock! rw) w))
       #f)
(check "a writer excludes readers and writers"
       (begin (rwlock-write-lock! rw)
              (let ((r (list (rwlock-try-write-lock! rw))))
                (rwlock-unlock! rw) r))
       '(#f))
(check "free again" (let ((ok (rwlock-try-write-lock! rw))) (rwlock-unlock! rw) ok) #t)
(check "a reader excludes a writer"
       (begin (rwlock-read-lock! rw)
              (let ((w (rwlock-try-write-lock! rw))
                    (r (let ((r (rwlock-try-read-lock! rw))) (when r (rwlock-unlock! rw)) r)))
                (rwlock-unlock! rw) (list w r)))
       '(#f #t))
(check "a thread that holds the write lock cannot take it again (try variants)"
       (begin (rwlock-write-lock! rw)
              (let ((r (list (rwlock-try-read-lock! rw) (rwlock-try-write-lock! rw))))
                (rwlock-unlock! rw) r))
       '(#f #f))

;; From another thread.
(define other-result #f)
(define (in-actor thunk)
  (set! other-result #f)
  (spawn (lambda () (set! other-result (thunk)) (sem-post! done)))
  (wait-for 1)
  other-result)
(check "another thread cannot write while this one reads"
       (begin (rwlock-read-lock! rw)
              (let ((r (in-actor (lambda () (rwlock-try-write-lock! rw)))))
                (rwlock-unlock! rw) r))
       #f)
(check "another thread can read while this one reads"
       (begin (rwlock-read-lock! rw)
              (let ((r (in-actor (lambda () (let ((ok (rwlock-try-read-lock! rw)))
                                              (when ok (rwlock-unlock! rw)) ok)))))
                (rwlock-unlock! rw) r))
       #t)
(check "another thread cannot read while this one writes"
       (begin (rwlock-write-lock! rw)
              (let ((r (in-actor (lambda () (rwlock-try-read-lock! rw)))))
                (rwlock-unlock! rw) r))
       #f)

;; with-read-lock / with-write-lock
(check "with-write-lock returns the thunk's value"
       (with-write-lock rw (lambda () 'v)) 'v)
(check "and releases" (let ((ok (rwlock-try-write-lock! rw))) (rwlock-unlock! rw) ok) #t)
(check "with-read-lock: a second reader gets in"
       (with-read-lock rw (lambda () (let ((ok (rwlock-try-read-lock! rw))) (when ok (rwlock-unlock! rw)) ok)))
       #t)
(check "with-write-lock releases on a raise"
       (begin (guard (e (#t #f)) (with-write-lock rw (lambda () (raise 'x))))
              (let ((ok (rwlock-try-write-lock! rw))) (rwlock-unlock! rw) ok))
       #t)
(check "with-read-lock releases on a raise"
       (begin (guard (e (#t #f)) (with-read-lock rw (lambda () (raise 'x))))
              (let ((ok (rwlock-try-write-lock! rw))) (rwlock-unlock! rw) ok))
       #t)
(check "with-write-lock releases on a continuation escape"
       (begin (call/cc (lambda (k) (with-write-lock rw (lambda () (k 'out)))))
              (let ((ok (rwlock-try-write-lock! rw))) (rwlock-unlock! rw) ok))
       #t)
(check "with-read-lock rejects a non-procedure"
       (raises? (lambda () (with-read-lock rw 5))) #t)
(check "a forged handle is rejected"
       (raises? (lambda () (rwlock-read-lock! (cons 'rwlock 42)))) #t)

;; Contention: writers keep a pair equal; readers must never see it torn.
(define pair-a 0)
(define pair-b 0)
(define torn 0)
(define rwc (make-rwlock))
(define (writer n)
  (let loop ((i 0))
    (when (< i n)
      (with-write-lock rwc (lambda () (set! pair-a (+ pair-a 1)) (set! pair-b (+ pair-b 1))))
      (loop (+ i 1))))
  (sem-post! done))
(define (reader n)
  (let loop ((i 0))
    (when (< i n)
      (with-read-lock rwc (lambda () (unless (= pair-a pair-b) (set! torn (+ torn 1)))))
      (loop (+ i 1))))
  (sem-post! done))
(let loop ((i 0)) (when (< i 3) (spawn (lambda () (writer 2000))) (loop (+ i 1))))
(let loop ((i 0)) (when (< i 3) (spawn (lambda () (reader 2000))) (loop (+ i 1))))
(wait-for 6)
(check "readers never see a torn pair" torn 0)
(check "every write counted" (list pair-a pair-b) '(6000 6000))

;;; ---- once ----
(define o (make-once))
(define runs 0)
(check "once? " (once? o) #t)
(check "the first call runs the thunk" (once! o (lambda () (set! runs (+ runs 1)))) #t)
(check "the second does not" (once! o (lambda () (set! runs (+ runs 1)))) #f)
(check "once ran once" runs 1)
(check "once-done?" (once-done? o) #t)

(define o2 (make-once))
(check "a thunk that raises does not use it up"
       (list (guard (e (#t 'raised)) (once! o2 (lambda () (raise 'boom))))
             (once-done? o2)
             (once! o2 (lambda () 'second))
             (once-done? o2))
       '(raised #f #t #t))
(define o3 (make-once))
(check "a thunk that escapes does not use it up"
       (list (call/cc (lambda (k) (once! o3 (lambda () (k 'escaped)))))
             (once-done? o3)
             (once! o3 (lambda () 'again)))
       '(escaped #f #t))
(define o4 (make-once))
(check "once! from inside its own thunk raises, and the once is given back"
       (list (raises? (lambda () (once! o4 (lambda () (once! o4 (lambda () 1))))))
             (once-done? o4))
       '(#t #f))
(check "once! rejects a non-procedure" (raises? (lambda () (once! (make-once) 5))) #t)

;; Many threads, one run, one #t.
(define o5 (make-once))
(define o5-runs 0)
(define o5-ran 0)
(define o5-mx (make-mutex))
(let loop ((i 0))
  (when (< i 8)
    (spawn (lambda ()
             (let ((r (once! o5 (lambda () (nap 0.02) (set! o5-runs (+ o5-runs 1))))))
               (with-mutex o5-mx (lambda () (when r (set! o5-ran (+ o5-ran 1)))))
               (sem-post! done))))
    (loop (+ i 1))))
(wait-for 8)
(check "eight callers, the thunk ran once" o5-runs 1)
(check "and exactly one of them was told so" o5-ran 1)

;; A second caller waits until the thunk has finished.
(define o6 (make-once))
(define gate (make-semaphore 0))
(define o6-second-returned #f)
(define o6-second-saw-done #f)
(spawn (lambda () (once! o6 (lambda () (sem-wait! gate))) (sem-post! done)))
(nap 0.05)
(spawn (lambda ()
         (once! o6 (lambda () 'never))
         (set! o6-second-saw-done (once-done? o6))
         (set! o6-second-returned #t)
         (sem-post! done)))
(nap 0.1)
(check "the second caller is still waiting while the thunk runs" o6-second-returned #f)
(sem-post! gate)
(wait-for 2)
(check "it returns once the thunk is done" (list o6-second-returned o6-second-saw-done) '(#t #t))

;; A thunk that raises hands the once to a waiter.
(define o7 (make-once))
(define gate7 (make-semaphore 0))
(define o7-waiter-ran #f)
(spawn (lambda () (guard (e (#t #f)) (once! o7 (lambda () (sem-wait! gate7) (raise 'fail)))) (sem-post! done)))
(nap 0.05)
(spawn (lambda () (once! o7 (lambda () (set! o7-waiter-ran #t))) (sem-post! done)))
(nap 0.05)
(sem-post! gate7)
(wait-for 2)
(check "after a raise a waiting caller runs its thunk" (list o7-waiter-ran (once-done? o7)) '(#t #t))

;;; ---- barrier ----
(define b1 (make-barrier 1))
(check "barrier?" (barrier? b1) #t)
(check "a barrier of one returns #t at once" (barrier-wait! b1) #t)
(check "and again" (barrier-wait! b1) #t)
(check "barrier-parties" (barrier-parties (make-barrier 5)) 5)
(check "a barrier needs at least one thread" (raises? (lambda () (make-barrier 0))) #t)
(check "and a fixnum" (raises? (lambda () (make-barrier 'x))) #t)
(check "a forged handle is rejected" (raises? (lambda () (barrier-wait! (cons 'barrier 42)))) #t)

;; N threads, P phases: nobody starts phase p+1 before everybody finished phase p.
(define n-threads 4)
(define n-phases 6)
(define bar (make-barrier n-threads))
(define marks (make-vector n-threads -1))
(define barrier-violations 0)
(define serial-count 0)
(define bmx (make-mutex))
(define (worker id)
  (let loop ((p 0))
    (when (< p n-phases)
      (vector-set! marks id p)
      (let ((serial (barrier-wait! bar)))
        (with-mutex bmx (lambda ()
          (when serial (set! serial-count (+ serial-count 1)))
          (let check-all ((j 0))
            (when (< j n-threads)
              (when (< (vector-ref marks j) p) (set! barrier-violations (+ barrier-violations 1)))
              (check-all (+ j 1)))))))
      (barrier-wait! bar)            ; keep phase p's checks before phase p+1's writes
      (loop (+ p 1))))
  (sem-post! done))
(let loop ((i 0)) (when (< i n-threads) (let ((id i)) (spawn (lambda () (worker id)))) (loop (+ i 1))))
(wait-for n-threads)
(check "no thread ran ahead of the barrier" barrier-violations 0)
(check "one serial thread per use of the barrier (the first wait of each phase)" serial-count n-phases)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
