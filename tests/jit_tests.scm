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

;;; 12. jit-call-depth does not leak across a caught exception raised from
;;; inside a JIT-to-JIT call (curry_jit_apply_arr → apply_arr → longjmp) ──────
;;; Regression coverage: before ExnHandler gained saved_jit_depth (restored
;;; by SCM_PROTECT, the VM's OP_PUSH_HANDLER, and eval.c's guard), a caught
;;; exception raised from a JIT'd closure calling another JIT'd closure never
;;; ran the matching jit_depth_pop(), permanently inflating g_jit_call_depth
;;; until it pinned at JIT_CALL_DEPTH_LIMIT and silently disabled the JIT
;;; fast path for the rest of the thread's lifetime.
(define (leak-callee x y) (+ x y))
(jit-compile! leak-callee)
(define (leak-caller) (leak-callee 1)) ; always wrong arity
(jit-compile! leak-caller)
(let loop ((i 0))
  (when (< i 260) ; well past JIT_CALL_DEPTH_LIMIT (512) if each leaks by 2
    (guard (e (#t #f)) (leak-caller))
    (loop (+ i 1))))
(check "jit-call-depth does not leak across caught nested-JIT exceptions"
       (jit-call-depth) 0)
(define (leak-good-caller x y) (leak-callee x y))
(jit-compile! leak-good-caller)
(check "JIT fast path still functional after leak scenario"
       (leak-good-caller 3 4) 7)

;;; 13. Same leak check via with-exception-handler, not just guard ─────────────
;;; with-exception-handler's primitive (prim_with_exception_handler in
;;; builtins.c) hand-rolls its own ExnHandler install rather than going
;;; through the VM's OP_PUSH_HANDLER or the SCM_PROTECT macro — a distinct
;;; call site that needed its own saved_jit_depth fix.
(define (weh-callee x y) (+ x y))
(jit-compile! weh-callee)
(define (weh-caller) (weh-callee 1)) ; always wrong arity
(jit-compile! weh-caller)
(let loop ((i 0))
  (when (< i 260)
    (with-exception-handler
      (lambda (e) 'caught)
      (lambda () (guard (e2 (#t #f)) (weh-caller))))
    (loop (+ i 1))))
(check "jit-call-depth does not leak across with-exception-handler"
       (jit-call-depth) 0)

;;; 14. Same leak check via parameterize ────────────────────────────────────────
;;; S_PARAMETERIZE in eval.c hand-rolls its own ExnHandler install (to run
;;; the parameter-restoring after-thunk before re-raising) rather than going
;;; through SCM_PROTECT — another distinct call site found on a second,
;;; more exhaustive completeness pass.
(define pz-param (make-parameter 10))
(define (pz-callee x y) (+ x y))
(jit-compile! pz-callee)
(define (pz-caller) (pz-callee 1)) ; always wrong arity
(jit-compile! pz-caller)
(let loop ((i 0))
  (when (< i 260)
    (guard (e (#t #f))
      (parameterize ((pz-param 99)) (pz-caller)))
    (loop (+ i 1))))
(check "jit-call-depth does not leak across parameterize"
       (jit-call-depth) 0)

;;; 15. Macro invocations inside a hot function must not be miscompiled ────────
;;; Regression coverage for issue #111: maybe_jit_bcc JIT-compiles a chunk's
;;; raw, un-macro-expanded src_lambda, and codegen.cpp's emit_expr only
;;; understands ~18 core forms -- anything else (a macro call; a special
;;; form the JIT tier doesn't implement) used to fall through to the
;;; generic "procedure call" path and get compiled as an ordinary call to
;;; whatever the head symbol happened to resolve to, silently producing
;;; wrong results ("apply: not a procedure", "unbound variable") once the
;;; function got hot enough to JIT-promote, instead of declining promotion
;;; and staying on the bytecode interpreter.
(define-syntax jit-macro-inc
  (syntax-rules () ((_ x) (+ x 1))))
(define (jit-macro-user n) (jit-macro-inc n))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: hot function using a user macro stays correct"
           (jit-macro-user i) (+ i 1))
    (loop (+ i 1))))

;;; The exact case that surfaced #111: (scheme case-lambda)'s own expansion
;;; leaves its internal %case-lambda-help helper call unexpanded in
;;; src_lambda, and case-lambda itself is a special form this JIT tier has
;;; never implemented.
(import (scheme case-lambda))
(define jit-cl-count
  (case-lambda
    ((n) (jit-cl-count n 0))
    ((n acc) (if (= n 0) acc (jit-cl-count (- n 1) (+ acc 1))))))
(check "JIT bailout: case-lambda self-recursive dispatch, 1000 iterations"
       (jit-cl-count 1000) 1000)

;;; A handful of the other unimplemented special forms named in issue #111,
;;; each driven past JIT_THRESHOLD (50 calls) to force auto-JIT promotion.
(define (jit-qq-user n) `(a ,n b))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: quasiquote stays correct when hot"
           (jit-qq-user i) (list 'a i 'b))
    (loop (+ i 1))))

(define (jit-lv-user n)
  (let-values (((q r) (floor/ n 3))) (+ q r)))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: let-values stays correct when hot"
           (jit-lv-user i) (+ (quotient i 3) (remainder i 3)))
    (loop (+ i 1))))

;;; codegen.cpp didn't call lang_translate before matching an operator's
;;; name against its own special-form dispatch, so a supported form spelled
;;; in Akkadian never matched any branch and fell through to the same
;;; miscompile path as an unrecognized form -- found by independent review
;;; of the #111 fix. šumma is Akkadian for "if".
(define (jit-akk-user n) (šumma (> n 0) (+ n 1) 0))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: Akkadian special-form synonym stays correct when hot"
           (jit-akk-user i) (if (> i 0) (+ i 1) 0))
    (loop (+ i 1))))

;;; defined? (SF_DEFINED_P) was missing from the unsupported-forms list --
;;; also found by independent review -- and has no GLOBAL_ENV binding of
;;; its own, so it fell through the same way.
(define jit-dp-target 1)
(define (jit-dp-user n) (if (defined? jit-dp-target) (+ n 1) 0))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: defined? stays correct when hot"
           (jit-dp-user i) (+ i 1))
    (loop (+ i 1))))

;;; Summary ─────────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
