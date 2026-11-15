;;; Tests for the bytecode-to-LLVM JIT backend (src/llvm/bcjit.cpp).
;;;
;;; Every check here must give the same answer on the bytecode VM and on
;;; either JIT backend, so the file is registered in ctest three times:
;;; plain, CURRY_JIT=force, and CURRY_JIT=force CURRY_JIT_BACKEND=bytecode.
;;; The shapes are the ones the source-based JIT refuses or that stress the
;;; frame handling of translated code: shared mutable upvalues, deep and
;;; mutual recursion, tail calls, variadic procedures, errors and escapes
;;; that unwind through native frames, and actors.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; Run a thunk many times so that it and everything it calls is compiled
;;; under the default threshold too; return the last result.
(define (hot thunk)
  (let loop ((i 0) (r #f))
    (if (< i 80) (loop (+ i 1) (thunk)) r)))

;;; 1. Upvalues: two closures share one variable (issue #291's shape) ───────
(define (make-counter)
  (let ((n 0))
    (cons (lambda () (set! n (+ n 1)) n)
          (lambda () n))))
(check "shared upvalue: writer and reader see the same variable"
       (hot (lambda ()
              (let* ((c (make-counter)) (inc (car c)) (get (cdr c)))
                (inc) (inc) (inc)
                (list (get) (inc) (get)))))
       '(3 4 4))

(define (make-acc)
  (let ((total 0))
    (lambda (x) (set! total (+ total x)) total)))
(define acc (make-acc))
(check "upvalue survives across calls of one closure"
       (let loop ((i 1) (r 0)) (if (> i 100) r (loop (+ i 1) (acc i))))
       5050)

;;; A loop variable captured per iteration: each closure keeps its own.
(define (collect-thunks n)
  (let loop ((i 0) (acc '()))
    (if (= i n) (reverse acc) (loop (+ i 1) (cons (lambda () (* i i)) acc)))))
(check "closures created in a loop each keep their own variable"
       (hot (lambda () (map (lambda (t) (t)) (collect-thunks 6))))
       '(0 1 4 9 16 25))

;;; An upvalue of an enclosing frame that has already returned.
(define (adder k) (lambda (x) (lambda (y) (+ x y k))))
(check "nested closures over a returned frame"
       (hot (lambda () (((adder 100) 20) 3))) 123)

;;; 2. Recursion ─────────────────────────────────────────────────────────────
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(check "fib 20" (hot (lambda () (fib 20))) 6765)

(define (count-up n) (if (= n 0) 0 (+ 1 (count-up (- n 1)))))
(check "non-tail recursion 20000 deep" (hot (lambda () (count-up 20000))) 20000)

(define (len l) (let loop ((l l)) (if (null? l) 0 (+ 1 (loop (cdr l))))))
(check "non-tail named let" (hot (lambda () (len '(a b c d e)))) 5)

(define (sum-to n acc) (if (= n 0) acc (sum-to (- n 1) (+ acc n))))
(check "self tail call through a global, 300000 iterations"
       (sum-to 300000 0) 45000150000)

(define (loop-sum n)
  (let loop ((i 0) (acc 0)) (if (= i n) acc (loop (+ i 1) (+ acc i)))))
(check "named-let loop" (hot (lambda () (loop-sum 1000))) 499500)

(define (my-even? n) (if (= n 0) #t (my-odd? (- n 1))))
(define (my-odd? n) (if (= n 0) #f (my-even? (- n 1))))
(check "mutual tail recursion, 200000 calls" (my-even? 200000) #t)
(check "mutual tail recursion, odd" (hot (lambda () (my-odd? 7))) #t)

;;; 3. Arguments ─────────────────────────────────────────────────────────────
(define (rest-args a . more) (list a more))
(check "rest list" (hot (lambda () (rest-args 1 2 3))) '(1 (2 3)))
(check "empty rest list" (hot (lambda () (rest-args 1))) '(1 ()))
(define (all-args . xs) xs)
(check "only a rest list" (hot (lambda () (all-args))) '())
(define (two a b) (- a b))
(check "too few arguments raises"
       (hot (lambda () (guard (e (#t 'raised)) (two 1)))) 'raised)
(check "too many arguments raises"
       (hot (lambda () (guard (e (#t 'raised)) (two 1 2 3)))) 'raised)
(check "called through map" (hot (lambda () (map two '(5 6 7) '(1 2 3)))) '(4 4 4))
(check "called through apply" (hot (lambda () (apply two '(9 4)))) 5)
;;; A procedure that rebinds or assigns its own parameters must not change
;;; the argument array of the primitive that called it.
(check "variadic procedure called through map"
       (hot (lambda () (map (lambda args args) '(1 2 3)))) '((1) (2) (3)))
(define (assigns-param x) (set! x (* x 10)) x)
(check "parameter assignment, called through map"
       (hot (lambda () (map assigns-param '(1 2 3)))) '(10 20 30))
(check "parameter assignment, called through apply twice"
       (hot (lambda ()
              (let ((args (list 7)))
                (list (apply assigns-param args) (apply assigns-param args) args))))
       '(70 70 (7)))

;;; Unbounded recursion ends in a catchable error, not a crash.
(define (forever n) (+ 1 (forever n)))
(check "unbounded recursion raises stack-overflow"
       (guard (e (#t 'overflow)) (forever 1)) 'overflow)
(check "and the VM works afterwards" (fib 10) 55)

;;; 4. Errors and escapes that unwind through translated frames ────────────
(define (fail-at n) (if (= n 0) (car '()) (+ 1 (fail-at (- n 1)))))
(define (catches n) (guard (e (#t 'caught)) (fail-at n)))
(check "error raised 50 frames down is caught" (hot (lambda () (catches 50))) 'caught)
(check "the VM stack is usable after the unwind"
       (hot (lambda () (list (catches 30) (fib 10) (count-up 100))))
       '(caught 55 100))

(define (find-first pred l)
  (call/cc (lambda (k) (for-each (lambda (x) (if (pred x) (k x))) l) #f)))
(check "call/cc escape out of a for-each"
       (hot (lambda () (find-first even? '(1 3 6 7)))) 6)
(check "call/cc without an escape"
       (hot (lambda () (find-first even? '(1 3 5)))) #f)

(define (unwinds)
  (let ((log '()))
    (guard (e (#t #f))
      (dynamic-wind
        (lambda () (set! log (cons 'in log)))
        (lambda () (fail-at 5))
        (lambda () (set! log (cons 'out log)))))
    (reverse log)))
(check "dynamic-wind after-thunk runs on a raise" (hot unwinds) '(in out))

(check "a captured closure still works after its creator raised"
       (hot (lambda ()
              (let ((saved #f))
                (guard (e (#t #f))
                  (let ((x 41))
                    (set! saved (lambda () (+ x 1)))
                    (car '())))
                (saved))))
       42)

;;; 5. Globals ───────────────────────────────────────────────────────────────
(define g-counter 0)
(define (bump!) (set! g-counter (+ g-counter 1)) g-counter)
(check "set! of a global" (begin (set! g-counter 0) (hot bump!)) 80)
(define (define-inside) (define local 5) (* local 2))
(check "internal define" (hot define-inside) 10)
(define (calls-later) (defined-later 4))
(define (defined-later x) (* x 3))
(check "global defined after its caller" (hot calls-later) 12)
(define (unbound-ref) (guard (e (#t 'unbound)) (this-is-not-defined-anywhere)))
(check "unbound global raises" (hot unbound-ref) 'unbound)

;;; 6. Redefining a primitive that has its own opcode ───────────────────────
;;; The opcode checks the binding on every execution, compiled or not.
(define (uses-car p) (car p))
(check "car before redefinition" (hot (lambda () (uses-car '(1 2)))) 1)
(define saved-car car)
(set! car (lambda (p) 'replaced))
(check "car after redefinition" (uses-car '(1 2)) 'replaced)
(set! car saved-car)
(check "car after restoring" (uses-car '(1 2)) 1)

;;; 7. Library code (a chunk with its own environment) ──────────────────────
(define-library (jit-bc-test lib)
  (import (scheme base))
  (export lib-triangle lib-make-stack)
  (begin
    (define (helper a b) (+ a b))
    (define (lib-triangle n)
      (let loop ((i 0) (acc 0)) (if (> i n) acc (loop (+ i 1) (helper acc i)))))
    (define (lib-make-stack)
      (let ((items '()))
        (lambda (op . args)
          (cond ((eq? op 'push) (set! items (cons (car args) items)) items)
                ((eq? op 'pop) (let ((x (car items))) (set! items (cdr items)) x))
                (else items)))))))
(import (jit-bc-test lib))
(check "library procedure calling a library-private helper"
       (hot (lambda () (lib-triangle 100))) 5050)
(check "library closure with state"
       (hot (lambda ()
              (let ((s (lib-make-stack)))
                (s 'push 1) (s 'push 2) (s 'push 3)
                (list (s 'pop) (s 'pop) (s 'peek)))))
       '(3 2 (1)))

;;; 8. Actors: the same compiled chunk on several threads ───────────────────
(import (curry sync))
;;; Each actor writes its own vector slot, then posts the semaphore.
(define (worker results done k)
  (vector-set! results k (list (fib 15) (loop-sum 500) (count-up 300)))
  (sem-post! done))
(define (run-actors n)
  (let ((results (make-vector n #f))
        (done (make-semaphore 0)))
    (let spawn-loop ((k 0))
      (when (< k n)
        (spawn (lambda () (worker results done k)))
        (spawn-loop (+ k 1))))
    (let wait ((k 0)) (when (< k n) (sem-wait! done) (wait (+ k 1))))
    (let count ((k 0) (ok 0))
      (if (= k n)
          ok
          (count (+ k 1)
                 (if (equal? (vector-ref results k) '(610 124750 300)) (+ ok 1) ok))))))
(check "12 actors run the same compiled procedures" (run-actors 12) 12)

;;; 9. What the bytecode backend compiles ────────────────────────────────────
;;; Only meaningful in a build with LLVM and CURRY_JIT_BACKEND=bytecode.
(define bytecode-backend?
  (guard (e (#t #f)) (and (curry-llvm-available?) (eq? (jit-backend) 'bytecode))))
(when bytecode-backend?
  (jit-compile! fib)
  (when (jit-compiled? fib)             ; not with CURRY_JIT=off
    (check "a closure with upvalues is compiled"
           (begin (jit-compile! acc) (jit-compiled? acc)) #t)
    (check "a non-tail named let is compiled"
           (begin (jit-compile! len) (jit-compiled? len)) #t)
    (check "a library procedure is compiled"
           (begin (jit-compile! lib-triangle) (jit-compiled? lib-triangle)) #t)
    ;; An instruction the translator does not handle yet is refused with
    ;; its name. When OP_VALUES gets support, pick another one here.
    (let ()
      (define (two-values) (values 1 2))
      (jit-compile! two-values)
      (check "an unsupported instruction is refused, and named"
             (jit-refusal-reason two-values)
             "JIT: opcode VALUES is not supported yet")
      (check "and the procedure still works"
             (call-with-values two-values list) '(1 2)))))

;;; Summary ──────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
