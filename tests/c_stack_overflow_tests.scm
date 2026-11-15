;;; Recursion that goes through a primitive which calls back into Scheme raises
;;; a catchable stack-overflow error instead of crashing the process (#376).
;;;
;;; Each level of such a recursion starts a new vm_run, a large C frame, and
;;; nothing bounded the depth: (rc 2000) below was a segmentation fault.

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
(define (overflow-code thunk)
  (guard (e ((error-object? e) (error-object-code e)) (#t 'other)) (thunk) 'no-error))

;;; apply
(define (rc n) (if (= n 0) 0 (+ 1 (rd (- n 1)))))
(define (rd n) (if (= n 0) 0 (+ 1 (apply rc (list (- n 1))))))
(check "apply recursion, shallow" (rc 100) 100)
(check "apply recursion, far too deep: a stack-overflow error"
       (overflow-code (lambda () (rc 1000000))) 'stack-overflow)
(check "the interpreter is usable afterwards" (rc 100) 100)

;;; map and for-each
(define (nest-map d) (if (= d 0) 1 (car (map (lambda (x) (+ 1 (nest-map (- d 1)))) '(1)))))
(check "map recursion, shallow" (nest-map 100) 101)
(check "map recursion, far too deep" (overflow-code (lambda () (nest-map 1000000))) 'stack-overflow)

(define (nest-for-each d)
  (if (= d 0) 0 (let ((r 0)) (for-each (lambda (x) (set! r (+ 1 (nest-for-each (- d 1))))) '(1)) r)))
(check "for-each recursion, shallow" (nest-for-each 100) 100)
(check "for-each recursion, far too deep"
       (overflow-code (lambda () (nest-for-each 1000000))) 'stack-overflow)

;;; a comparison procedure that recurses through the primitive it was given to
(define (nest-vector-map d)
  (if (= d 0) 1 (vector-ref (vector-map (lambda (x) (+ 1 (nest-vector-map (- d 1)))) (vector 1)) 0)))
(check "vector-map recursion, shallow" (nest-vector-map 100) 101)
(check "vector-map recursion, far too deep"
       (overflow-code (lambda () (nest-vector-map 1000000))) 'stack-overflow)

;;; with-exception-handler / dynamic-wind / call/cc as the recursing primitive
(define (nest-dw d) (if (= d 0) 0 (dynamic-wind (lambda () #f) (lambda () (+ 1 (nest-dw (- d 1)))) (lambda () #f))))
(check "dynamic-wind recursion, shallow" (nest-dw 100) 100)
(check "dynamic-wind recursion, far too deep" (overflow-code (lambda () (nest-dw 1000000))) 'stack-overflow)
(define (nest-cc d) (if (= d 0) 0 (+ 1 (call/cc (lambda (k) (nest-cc (- d 1)))))))
(check "call/cc recursion, shallow" (nest-cc 100) 100)
(check "call/cc recursion, far too deep" (overflow-code (lambda () (nest-cc 1000000))) 'stack-overflow)

;;; on an actor thread, whose stack is smaller than the main thread's
(define sem (make-semaphore 0))
(define actor-result #f)
(spawn (lambda ()
         (set! actor-result (overflow-code (lambda () (rc 1000000))))
         (sem-post! sem)))
(sem-wait! sem)
(check "the same in an actor" actor-result 'stack-overflow)

;;; in a compiled loop, repeated: the error leaves nothing behind
(define (repeat n) (if (= n 0) 'done (begin (overflow-code (lambda () (rc 1000000))) (repeat (- n 1)))))
(check "ten overflows in a row" (repeat 10) 'done)
(check "and the interpreter still works" (rc 50) 50)

;;; after the functions are hot enough for the JIT to compile them
(let loop ((i 0)) (if (< i 3000) (begin (rc 20) (loop (+ i 1)))))
(check "warmed apply recursion: a stack-overflow error"
       (overflow-code (lambda () (rc 1000000))) 'stack-overflow)
(check "warmed apply recursion still returns" (rc 100) 100)

;;; force
(define (nest-force d) (if (= d 0) 0 (force (delay (+ 1 (nest-force (- d 1)))))))
(check "force recursion, shallow" (nest-force 100) 100)
(check "force recursion, far too deep"
       (overflow-code (lambda () (nest-force 1000000))) 'stack-overflow)

;;; with-exception-handler
(define (nest-weh d) (if (= d 0) 0 (+ 1 (with-exception-handler (lambda (e) (raise e)) (lambda () (nest-weh (- d 1)))))))
(check "with-exception-handler recursion, far too deep"
       (overflow-code (lambda () (nest-weh 1000000))) 'stack-overflow)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
