;;; A continuation invoked after its call/cc has returned raises
;;; continuation-expired (#443).
;;;
;;; curry's continuations are escape-only: the continuation holds a jmp_buf
;;; into the call/cc's own C stack frame. Once that call/cc returned, normally,
;;; by a raise or by another continuation, invoking it jumped into a frame that
;;; no longer existed, and the process died with a segmentation fault.

(import (curry sync))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;; The error code of whatever (thunk) raises, or (returned value).
(define (code-of thunk)
  (guard (e ((error-object? e) (error-object-code e))
            (#t (list 'raised e)))
    (list 'returned (thunk))))

;;; The three ways a call/cc can return.
(define k-normal #f)
(check "the call/cc returns normally"
       (+ 1 (call/cc (lambda (k) (set! k-normal k) 1))) 2)
(check "its continuation, invoked afterwards"
       (code-of (lambda () (k-normal 5))) 'continuation-expired)
(check "and again" (code-of (lambda () (k-normal 5))) 'continuation-expired)

(define k-raised #f)
(guard (e (#t #f))
  (call/cc (lambda (k) (set! k-raised k) (raise 'boom))))
(check "the call/cc was left by a raise"
       (code-of (lambda () (k-raised 1))) 'continuation-expired)

(define k-escaped #f)
(call/cc (lambda (outer)
           (call/cc (lambda (inner) (set! k-escaped inner) (outer 'out)))))
(check "the call/cc was left by another continuation"
       (code-of (lambda () (k-escaped 1))) 'continuation-expired)

;;; Returned from a procedure, then called deeper in the stack.
(define (grab) (call/cc (lambda (k) k)))
(define k-returned (grab))
(define (deep n) (if (= n 0) (k-returned 1) (+ 1 (deep (- n 1)))))
(check "returned from a procedure, invoked from deeper in the stack"
       (code-of (lambda () (deep 200))) 'continuation-expired)

;;; Called with several values, and through apply.
(check "with zero values" (code-of (lambda () (k-normal))) 'continuation-expired)
(check "with two values" (code-of (lambda () (k-normal 1 2))) 'continuation-expired)
(check "through apply" (code-of (lambda () (apply k-normal '(1)))) 'continuation-expired)
(check "through map" (code-of (lambda () (map k-normal '(1)))) 'continuation-expired)

;;; An expired continuation inside a dynamic-wind: the after thunk runs once,
;;; by the raise, not by an escape.
(check "inside dynamic-wind the after thunk runs once"
       (let ((afters 0))
         (list (code-of (lambda ()
                          (dynamic-wind (lambda () #f)
                                        (lambda () (k-normal 1))
                                        (lambda () (set! afters (+ afters 1))))))
               afters))
       '(continuation-expired 1))

;;; From the tree-walker.
(check "from tree-eval"
       (tree-eval '(guard (e ((error-object? e) (error-object-code e)))
                     (k-normal 1)))
       'continuation-expired)

;;; From another thread: the call/cc's frame is on this thread's stack.
(define k-live-here #f)
(define actor-result #f)
(define actor-done (make-semaphore 0))
(check "from an actor, while its call/cc is still running here"
       (call/cc
         (lambda (k)
           (set! k-live-here k)
           (spawn (lambda ()
                    (set! actor-result (code-of (lambda () (k-live-here 1))))
                    (sem-post! actor-done)))
           (sem-wait! actor-done)
           actor-result))
       'continuation-expired)

;;; A guard clause that runs in place (raise-continuable) leaves the call/cc
;;; frames between the raise and the guard.
(define k-in-guard #f)
(check "a guard clause run in place, with a call/cc frame in between"
       (tree-eval '(guard (e (#t 'caught))
                     (call/cc (lambda (k) (set! k-in-guard k) (raise-continuable 'x)))))
       'caught)
(check "that continuation is expired afterwards"
       (code-of (lambda () (k-in-guard 1))) 'continuation-expired)

;;; An STM retry leaves the call/cc frames inside the first alternative.
(define k-in-stm #f)
(check "or-else takes the second alternative after a retry"
       (atomically (lambda ()
                     (%or-else (lambda () (call/cc (lambda (k) (set! k-in-stm k) (retry))))
                               (lambda () 'second))))
       'second)
(check "the continuation captured before the retry is expired"
       (code-of (lambda () (k-in-stm 1))) 'continuation-expired)

;;; Escapes still work, at every depth.
(check "escape" (call/cc (lambda (k) (+ 1 (k 42)))) 42)
(check "escape with two values"
       (call-with-values (lambda () (call/cc (lambda (k) (k 1 2)))) list) '(1 2))
(check "escape through dynamic-wind"
       (let ((log '()))
         (list (call/cc (lambda (k)
                          (dynamic-wind (lambda () (set! log (cons 'in log)))
                                        (lambda () (k 'escaped))
                                        (lambda () (set! log (cons 'out log))))))
               (reverse log)))
       '(escaped (in out)))
(check "outer escape from an inner call/cc"
       (call/cc (lambda (outer) (call/cc (lambda (inner) (outer 'outer))))) 'outer)
(check "inner escape after an inner call/cc returned"
       (call/cc (lambda (outer)
                  (call/cc (lambda (inner) 1))
                  (outer 'still-live)))
       'still-live)
(check "escape out of a guard body"
       (call/cc (lambda (k) (guard (e (#t 'caught)) (k 'escaped)))) 'escaped)
(check "escape from a guard clause"
       (call/cc (lambda (k) (guard (e (#t (k 'from-clause))) (raise 'x)))) 'from-clause)
(check "escape from a with-exception-handler handler"
       (call/cc (lambda (k)
                  (with-exception-handler (lambda (e) (k 'handled))
                    (lambda () (raise 'boom)))))
       'handled)
(check "escape after a caught raise inside the call/cc"
       (call/cc (lambda (k)
                  (guard (e (#t #f)) (raise 'inner))
                  (k 'after-guard)))
       'after-guard)

;;; In a loop, so the code is compiled (and native with the JIT).
(define (find-first pred lst)
  (call/cc (lambda (return)
             (for-each (lambda (x) (if (pred x) (return x))) lst)
             #f)))
(define (run-finds n)
  (let loop ((i 0) (acc 0))
    (if (= i n) acc (loop (+ i 1) (+ acc (find-first (lambda (x) (> x 5)) '(1 3 7 9)))))))
(check "a loop of 500 escapes" (run-finds 500) 3500)
(check "and again after it has been compiled" (run-finds 500) 3500)
(check "expired continuations are still caught after the loop"
       (code-of (lambda () (k-normal 1))) 'continuation-expired)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
