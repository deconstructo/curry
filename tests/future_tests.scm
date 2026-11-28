;;; (curry future): futures on the work-stealing pool (A3).
;;;
;;; Results from spawned actors go through globals: a set! on a captured local
;;; does not reach the spawning thread (#448).

(import (scheme base) (curry sync) (curry future))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (raised thunk) (guard (e (#t (list 'raised (if (error-object? e) (error-object-message e) e)))) (thunk)))
(define (range n) (let loop ((i (- n 1)) (acc '())) (if (< i 0) acc (loop (- i 1) (cons i acc)))))
(define (sum l) (apply + l))
(define (filter p l) (cond ((null? l) (quote ())) ((p (car l)) (cons (car l) (filter p (cdr l)))) (else (filter p (cdr l)))))

;;; ---- the basics ----
(check "future-get returns the thunk's value" (future-get (pool-submit (lambda () (+ 1 2)))) 3)
(check "future?" (list (future? (pool-submit (lambda () 1))) (future? 5) (future? (vector 1 2))) '(#t #f #f))
(check "future-get can be asked again"
       (let ((f (pool-submit (lambda () 'v)))) (list (future-get f) (future-get f))) '(v v))
(check "future-done? after future-get"
       (let ((f (pool-submit (lambda () 'v)))) (future-get f) (future-done? f)) #t)
(check "a list comes back whole"
       (future-get (pool-submit (lambda () (list 1 2 3)))) '(1 2 3))
(check "pool-submit rejects a non-procedure (it raises)"
       (car (raised (lambda () (pool-submit 5)))) 'raised)
(check "future-get rejects a non-future" (car (raised (lambda () (future-get 5)))) 'raised)
(check "future-get rejects a forged future"
       (car (raised (lambda () (future-get (vector '%future 0 0 0 0 0 0)))))
       'raised)

;;; ---- exceptions ----
(check "an exception in the thunk is raised by future-get"
       (raised (lambda () (future-get (pool-submit (lambda () (raise 'boom)))))) '(raised boom))
(check "as often as it is asked"
       (let ((f (pool-submit (lambda () (raise 'boom)))))
         (list (raised (lambda () (future-get f))) (raised (lambda () (future-get f)))))
       '((raised boom) (raised boom)))
(check "an error object keeps its message"
       (raised (lambda () (future-get (pool-submit (lambda () (error "went wrong" 1 2))))))
       '(raised "went wrong"))
(check "a primitive's error"
       (car (raised (lambda () (future-get (pool-submit (lambda () (car 5))))))) 'raised)
(check "future-done? is #t for a future that raised"
       (let ((f (pool-submit (lambda () (raise 'x)))))
         (raised (lambda () (future-get f))) (future-done? f))
       #t)
(check "a failed future does not break the pool"
       (let loop ((i 0) (acc 0))
         (if (= i 50)
             acc
             (begin (raised (lambda () (future-get (pool-submit (lambda () (raise 'x))))))
                    (loop (+ i 1) (+ acc (future-get (pool-submit (lambda () 1))))))))
       50)

;;; ---- future-then ----
(check "future-then" (future-get (future-then (pool-submit (lambda () 20)) (lambda (x) (* x 2)))) 40)
(check "a chain"
       (future-get (future-then (future-then (pool-submit (lambda () 1)) (lambda (x) (+ x 1)))
                                (lambda (x) (* x 10))))
       20)
(check "future-then on a future that is already done"
       (let ((f (pool-submit (lambda () 5))))
         (future-get f)
         (future-get (future-then f (lambda (x) (+ x 1)))))
       6)
(check "future-then after a failure: the same exception, proc not called"
       (let ((called #f))
         (list (raised (lambda () (future-get (future-then (pool-submit (lambda () (raise 'first)))
                                                            (lambda (x) (set! called #t) 'never)))))
               called))
       '((raised first) #f))
(check "future-then whose proc raises"
       (raised (lambda () (future-get (future-then (pool-submit (lambda () 1)) (lambda (x) (raise 'second))))))
       '(raised second))
(check "several then's on one future"
       (let* ((f (pool-submit (lambda () 10)))
              (a (future-then f (lambda (x) (+ x 1))))
              (b (future-then f (lambda (x) (+ x 2))))
              (c (future-then f (lambda (x) (+ x 3)))))
         (list (future-get a) (future-get b) (future-get c)))
       '(11 12 13))
(check "future-then rejects a non-procedure" (car (raised (lambda () (future-then (pool-submit (lambda () 1)) 5)))) 'raised)
(check "a long chain, asked for at the end"
       (let loop ((i 0) (f (pool-submit (lambda () 0))))
         (if (= i 200) (future-get f) (loop (+ i 1) (future-then f (lambda (x) (+ x 1))))))
       200)
(check "a long chain, with a slow head"
       (let ((gate (make-semaphore 0)))
         (let loop ((i 0) (f (pool-submit (lambda () (sem-wait! gate) 0))) (fs '()))
           (if (< i 50)
               (loop (+ i 1) (future-then f (lambda (x) (+ x 1))) (cons f fs))
               (begin (sem-post! gate) (future-get f)))))
       50)

;;; ---- many futures ----
(check "200 futures"
       (sum (map future-get (map (lambda (i) (pool-submit (lambda () (* i i)))) (range 200))))
       (sum (map (lambda (i) (* i i)) (range 200))))
(define counter 0)
(define counter-mx (make-mutex))
(check "every future runs exactly once"
       (begin (for-each future-get
                        (map (lambda (i) (pool-submit (lambda () (with-mutex counter-mx (lambda () (set! counter (+ counter 1)))))))
                             (range 500)))
              counter)
       500)
(check "futures that allocate"
       (sum (map (lambda (f) (length (future-get f)))
                 (map (lambda (i) (pool-submit (lambda () (range 5000)))) (range 60))))
       300000)

;;; ---- waiting inside futures must not starve the pool ----
(define workers (hardware-concurrency))
(check "more outer futures than workers, each waiting for an inner one"
       (sum (map future-get
                 (map (lambda (i)
                        (pool-submit (lambda () (+ i (future-get (pool-submit (lambda () 1000)))))))
                      (range (* 3 workers)))))
       (+ (sum (range (* 3 workers))) (* 1000 (* 3 workers))))
(check "three levels"
       (future-get
         (pool-submit (lambda ()
           (future-get (pool-submit (lambda ()
             (future-get (pool-submit (lambda () 'deep)))))))))
       'deep)
(check "a tree of futures"
       (let tree ((n 6))
         (if (= n 0)
             1
             (let ((a (pool-submit (lambda () (tree (- n 1)))))
                   (b (pool-submit (lambda () (tree (- n 1))))))
               (+ (future-get a) (future-get b)))))
       64)
(check "then inside a future"
       (future-get (pool-submit (lambda ()
         (future-get (future-then (pool-submit (lambda () 2)) (lambda (x) (* x x)))))))
       4)

;;; ---- timed waits ----
(define gate (make-semaphore 0))
(define slow (pool-submit (lambda () (sem-wait! gate) 'finally)))
(check "future-wait times out" (future-wait slow 0.05) #f)
(check "future-done? is #f meanwhile" (future-done? slow) #f)
(sem-post! gate)
(check "future-wait returns #t once it is done" (future-wait slow 5) #t)
(check "and the value is there" (future-get slow) 'finally)
(check "future-wait with a fixnum" (future-wait (pool-submit (lambda () 1)) 2) #t)

;;; ---- from other threads ----
(define actor-result #f)
(define actor-done (make-semaphore 0))
(spawn (lambda ()
         (set! actor-result (future-get (pool-submit (lambda () (+ 20 22)))))
         (sem-post! actor-done)))
(sem-wait! actor-done)
(check "an actor can submit and get" actor-result 42)
(define shared (pool-submit (lambda () (sem-wait! gate) 'shared)))
(define got-a #f)
(define got-b #f)
(spawn (lambda () (set! got-a (future-get shared)) (sem-post! actor-done)))
(spawn (lambda () (set! got-b (future-get shared)) (sem-post! actor-done)))
(sem-post! gate)
(sem-wait! actor-done) (sem-wait! actor-done)
(check "two threads can wait for one future" (list got-a got-b) '(shared shared))

;;; ---- continuations ----
(check "call/cc inside the thunk"
       (future-get (pool-submit (lambda () (+ 1 (call/cc (lambda (k) (+ 100 (k 41))))))))
       42)
;; A thunk that invokes a continuation of the thread that waits for it. If a
;; worker runs it, that is a continuation from another thread and raises (#443);
;; if future-get runs it here, the escape is legitimate and leaves the thunk.
;; Either way the future must end up finished, not stuck as running.
(define escaping #f)
(define escape-outcomes '())
(let loop ((i 0))
  (when (< i 40)
    (let ((r (call/cc (lambda (k)
               (set! escaping (pool-submit (lambda () (k 'escaped))))
               (guard (e (#t 'raised)) (future-get escaping))))))
      (set! escape-outcomes (cons r escape-outcomes))
      (check "a continuation invoked from the thunk: the future is finished"
             (future-wait escaping 5) #t)
      (check "and has failed"
             (car (raised (lambda () (future-get escaping)))) 'raised))
    (loop (+ i 1))))
(check "each outcome was an escape or a raise"
       (let ((bad (filter (lambda (r) (not (memq r '(escaped raised)))) escape-outcomes)))
         (length bad))
       0)
(check "dynamic-wind inside the thunk"
       (let ((log '()))
         (list (future-get (pool-submit (lambda ()
                  (dynamic-wind (lambda () (set! log (cons 'in log)))
                                (lambda () 'v)
                                (lambda () (set! log (cons 'out log)))))))
               (reverse log)))
       '(v (in out)))

;;; ---- parallel map inside a future still works ----
(check "map inside a future"
       (future-get (pool-submit (lambda () (map (lambda (x) (* x 2)) (range 100)))))
       (map (lambda (x) (* x 2)) (range 100)))

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
