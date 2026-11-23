;;; A JIT-compiled named-let loop does not use C stack on every iteration (#421).
;;;
;;; The source JIT (src/llvm/codegen.cpp) put the argument array of every
;;; call, and the capture array of every closure it built, in an alloca at
;;; the point of the call. Inside a loop body that is a dynamic alloca: it
;;; ran once per iteration and the space was not released until the function
;;; returned. A loop that made a call per iteration ran out of C stack after
;;; a few hundred thousand iterations (curl_global_init_race crashed this way
;;; with CURRY_JIT=force). The function is compiled by hand with
;;; (jit-compile! f), so the test does not depend on CURRY_JIT or on timing.

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

(define (note-if-not-compiled label f)
  (if (not (jit-compiled? f))
      (begin (display "SKIP: ") (display label)
             (display " was not JIT-compiled, so the loop ran in the VM") (newline))))

(define N 3000000)   ; 16 bytes per iteration was 48 MB, against an 8 MB stack

(define (id x) x)
(define (add a b) (+ a b))

;;; a call to a global procedure per iteration
(define (loop-call k)
  (let loop ((n 0))
    (if (< n k) (begin (id n) (loop (+ n 1))) n)))
(jit-compile! loop-call)
(note-if-not-compiled "loop-call" loop-call)
(check "loop with a call per iteration" (loop-call N) N)

;;; a call whose result is the loop variable
(define (loop-sum k)
  (let loop ((n 0) (acc 0))
    (if (< n k) (loop (+ n 1) (add acc 1)) acc)))
(jit-compile! loop-sum)
(note-if-not-compiled "loop-sum" loop-sum)
(check "loop with a call in an argument" (loop-sum N) N)

;;; a closure created per iteration, capturing the loop variable
(define (loop-closure k)
  (let loop ((n 0) (last #f))
    (if (< n k) (loop (+ n 1) (lambda () n)) (last))))
(jit-compile! loop-closure)
(note-if-not-compiled "loop-closure" loop-closure)
(check "loop that makes a capturing closure per iteration"
       (loop-closure N) (- N 1))

;;; the same loops on an actor's own, smaller, stack
(define (run-in-actor thunk)
  (let ((a (spawn thunk)))
    (let wait () (if (actor-alive? a) (wait) 'done))))
(check "loops in an actor" (begin (run-in-actor (lambda () (loop-call N))) 'ok) 'ok)

;;; arguments are still right when the call sits in a loop: each call reads
;;; its own argument array, reused from one iteration to the next
(define (loop-args k)
  (let loop ((n 0) (acc '()))
    (if (< n k) (loop (+ n 1) (cons (add n (add n 1)) acc)) (reverse acc))))
(jit-compile! loop-args)
(check "arguments in a loop" (loop-args 5) '(1 3 5 7 9))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(if (> fail 0) (exit 1) (exit 0))
