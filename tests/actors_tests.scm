;;; Actor concurrency tests — requires (curry sync) for synchronization

(import (curry sync))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; Basic actor creation
;;; Spawn an actor that waits for a semaphore so we can test actor-alive?
(define sem0 (make-semaphore 0))
(define a1 (spawn (lambda () (sem-wait! sem0))))
(check "actor? yes"         (actor? a1) #t)
(check "actor? no"          (actor? 42) #f)
(check "actor-alive? live"  (actor-alive? a1) #t)
(sem-post! sem0)  ; let it finish

;;; Actor sets result via shared mutable variable; semaphore signals completion
;;; sync module names: sem-wait!, sem-post!, make-semaphore
(define result1 #f)
(define sem1 (make-semaphore 0))
(spawn (lambda ()
         (set! result1 (* 6 7))
         (sem-post! sem1)))
(sem-wait! sem1)
(check "actor computes result" result1 42)

;;; Actor that accumulates a sum
(define result2 #f)
(define sem2 (make-semaphore 0))
(spawn (lambda ()
         (let loop ((i 1) (acc 0))
           (if (> i 100)
               (begin (set! result2 acc) (sem-post! sem2))
               (loop (+ i 1) (+ acc i))))))
(sem-wait! sem2)
(check "actor loop sum 1..100" result2 5050)

;;; Multiple actors all posting to same semaphore
(define sem3 (make-semaphore 0))
(define n-actors 5)
(let loop ((i 0))
  (when (< i n-actors)
    (spawn (lambda () (sem-post! sem3)))
    (loop (+ i 1))))
(let loop ((i 0))
  (when (< i n-actors)
    (sem-wait! sem3)
    (loop (+ i 1))))
(check "multiple actors all complete" #t #t)

;;; send! doesn't crash
(define a2 (spawn (lambda () 'quick)))
(send! a2 'hello)
(check "send! no crash" #t #t)

;;; Mutex: protected shared counter
;;; sync module names: make-mutex, mutex-lock!, mutex-unlock!
(define mtx (make-mutex))
(define shared-count 0)
(define sem4 (make-semaphore 0))
(define n-incr 50)

(let loop ((i 0))
  (when (< i n-incr)
    (spawn (lambda ()
             (mutex-lock! mtx)
             (set! shared-count (+ shared-count 1))
             (mutex-unlock! mtx)
             (sem-post! sem4)))
    (loop (+ i 1))))

(let loop ((i 0))
  (when (< i n-incr)
    (sem-wait! sem4)
    (loop (+ i 1))))

(check "mutex protected counter" shared-count n-incr)

;;; Condvar signal/wait
;;; sync module names: make-condvar, cond-wait!, cond-signal!
(define cv-mtx  (make-mutex))
(define cv      (make-condvar))
(define cv-result #f)
(define sem5 (make-semaphore 0))

(spawn (lambda ()
         (mutex-lock! cv-mtx)
         (cond-wait! cv cv-mtx)
         (set! cv-result 'signaled)
         (mutex-unlock! cv-mtx)
         (sem-post! sem5)))

(spawn (lambda ()
         (mutex-lock! cv-mtx)
         (cond-signal! cv)
         (mutex-unlock! cv-mtx)))

(sem-wait! sem5)
(check "condvar signal/wait" cv-result 'signaled)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
