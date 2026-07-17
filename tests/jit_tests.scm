;;; JIT (LLVM ORC v2) correctness tests — only meaningful with BUILD_LLVM=ON.
;;; When JIT is unavailable, procedures fall back to bytecode; tests still pass.

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

;;; 1. API surface ─────────────────────────────────────────────────────────────
(check "curry-llvm-available? returns bool"
       (boolean? (curry-llvm-available?)) #t)

;;; 2. curry-jit-call ──────────────────────────────────────────────────────────
(check "jit-call arithmetic"
       (curry-jit-call (lambda () (+ 1 2))) 3)

(check "jit-call string"
       (curry-jit-call (lambda () (string-append "foo" "bar"))) "foobar")

(check "jit-call conditional"
       (curry-jit-call (lambda () (if #t 'yes 'no))) 'yes)

;;; 3. curry-jit-eval ──────────────────────────────────────────────────────────
(define x 42)
(check "jit-eval global lookup"
       (curry-jit-eval '(+ x 8)) 50)

;;; 4. jit-compile! ────────────────────────────────────────────────────────────
(define (square n) (* n n))
(jit-compile! square)
(check "jit-compile! correctness" (square 9) 81)
(check "jit-compile! idempotent"  (square 3) 9)

;;; 5. Auto-JIT: hot function compiles and gives correct results ───────────────
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
; Drive fib past threshold (50 calls) then verify correctness
(let loop ((i 0))
  (when (< i 60)
    (check (string-append "fib(10) call " (number->string i))
           (fib 10) 55)
    (loop (+ i 1))))

;;; 6. Tail recursion survives JIT depth guard ─────────────────────────────────
(define (count-down n) (if (= n 0) 'done (count-down (- n 1))))
(check "tail-recursion 1M iterations" (count-down 1000000) 'done)

;;; 7. Closure capture correctness through JIT ─────────────────────────────────
(define (make-adder k) (lambda (x) (+ x k)))
(define add10 (make-adder 10))
; Drive add10 past threshold
(let loop ((i 0))
  (when (< i 60)
    (check (string-append "closure-capture call " (number->string i))
           (add10 i) (+ i 10))
    (loop (+ i 1))))

;;; 8. Named-let (self-capturing — JIT disabled, bytecode handles it) ──────────
(check "named-let loop"
       (let sum ((i 0) (acc 0))
         (if (= i 100) acc (sum (+ i 1) (+ acc i))))
       4950)

;;; 9. jit-call-depth is 0 between calls ───────────────────────────────────────
(check "jit-call-depth at top level" (jit-call-depth) 0)

;;; 10. curry-llvm-dump-last doesn't crash ─────────────────────────────────────
(curry-llvm-dump-last) ; side-effect: prints to stderr — just mustn't crash

;;; 11. Arity checking and variadic rest-arg collection survive JIT ────────────
;;; Regression coverage: apply_arr/OP_CALL/OP_TAIL_CALL's vis_jitclosure fast
;;; path called the compiled native function directly with no argc check at
;;; all. A fixed-arity closure called with the wrong argument count silently
;;; read stale/garbage stack data instead of raising; codegen.cpp's rest-arg
;;; gathering (curry_jit_list_tail) also assumed argc was already valid.
(define (jit-rest-of a . rest) rest)
(jit-compile! jit-rest-of)
(check "JIT variadic: rest param collects trailing args"
       (jit-rest-of 1 2 3 4) '(2 3 4))
(check "JIT variadic: rest param empty when no extra args"
       (jit-rest-of 1) '())

(define (jit-exact-two x y) (+ x y))
(jit-compile! jit-exact-two)
(check "JIT arity: exact match still works" (jit-exact-two 1 2) 3)
(check "JIT arity: too few arguments raises wrong-number-of-arguments"
       (guard (e (#t (error-object-code e))) (jit-exact-two 1))
       'wrong-number-of-arguments)
(check "JIT arity: too many arguments raises wrong-number-of-arguments"
       (guard (e (#t (error-object-code e))) (jit-exact-two 1 2 3))
       'wrong-number-of-arguments)

(define (jit-need-two-rest a b . rest) (list a b rest))
(jit-compile! jit-need-two-rest)
(check "JIT arity: variadic too few required args raises"
       (guard (e (#t (error-object-code e))) (jit-need-two-rest 1))
       'wrong-number-of-arguments)
(check "JIT arity: variadic exact required args, empty rest"
       (jit-need-two-rest 1 2) '(1 2 ()))

;;; Same checks again via the auto-JIT (call-count threshold) path rather
;;; than the explicit jit-compile! path, since they hit a different call site
;;; (OP_CALL/OP_TAIL_CALL's inline fast path vs. curry-jit-call/-eval).
(define (jit-auto-rest a . rest) rest)
(define (jit-auto-exact x y) (+ x y))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT auto: variadic rest correctness (warmup)"
           (jit-auto-rest 1 2 3) '(2 3))
    (check "JIT auto: exact-arity correctness (warmup)"
           (jit-auto-exact 1 2) 3)
    (loop (+ i 1))))
(check "JIT auto: too few arguments raises after warmup"
       (guard (e (#t (error-object-code e))) (jit-auto-exact 1))
       'wrong-number-of-arguments)
(check "JIT auto: too many arguments raises after warmup"
       (guard (e (#t (error-object-code e))) (jit-auto-exact 1 2 3))
       'wrong-number-of-arguments)

;;; Summary ─────────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
