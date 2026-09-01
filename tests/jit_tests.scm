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

;;; 16. Locally shadowing an arithmetic operator must not be ignored ───────────
;;; Regression coverage for issue #115: emit_call's ARITH2/ARITH1 fast paths
;;; matched an operator's name directly against a builtin table with no
;;; regard for local lexical scope, unlike every other name-dispatch in
;;; codegen.cpp. A hot function shadowing +/-/*// etc. silently got the
;;; builtin operator instead of its own local binding, with no error at
;;; all -- a worse failure mode than #111's (that one at least raised a
;;; catchable error).
(define (jit-shadow-plus n) (let ((+ (lambda (a b) 99))) (+ n 1)))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT: locally shadowed + is not silently ignored when hot"
           (jit-shadow-plus i) 99)
    (loop (+ i 1))))

(define (jit-shadow-minus n) (let ((- (lambda (a b) 77))) (- n 1)))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT: locally shadowed - is not silently ignored when hot"
           (jit-shadow-minus i) 77)
    (loop (+ i 1))))

;;; Same bug class, found during review of the #115 fix above (issue #117):
;;; the named-let self-call backedge matched the loop's name with no
;;; cc.lookup check either -- a local binding shadowing the loop name
;;; compiled to a backedge instead of an ordinary call, corrupting the loop
;;; variable and hanging forever once JIT-promoted (worse than #115's
;;; silently-wrong-answer outcome: an actual infinite loop). The named let
;;; compiles inline into its enclosing function's own chunk (it isn't a
;;; separate closure), so it's jit-shadow-loop itself -- not the inner
;;; loop -- that needs to cross JIT_THRESHOLD for this to matter; each call
;;; runs the loop to completion first regardless of tier, so this can't
;;; hang indefinitely even pre-fix on a single call, only across repeated
;;; calls once the enclosing closure gets promoted.
(define (jit-shadow-loop n)
  (let loop ((i 0))
    (if (< i 5)
        (loop (+ i 1))
        (let ((loop (lambda (x) 7))) (loop 5)))))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT: locally shadowed named-let loop name is not silently ignored"
           (jit-shadow-loop i) 7)
    (loop (+ i 1))))

;;; A first attempt at the #117 fix (a blanket "any local binding of this
;;; name suppresses the backedge" check) overcorrected: a named let's own
;;; name must still shadow an ENCLOSING binding of the same name and
;;; self-recurse normally -- only a binding introduced INSIDE the loop
;;; body (the case above) should suppress it. Found by independent review
;;; of that first attempt, before it was ever merged.
(define (jit-enclosing-shadow-loop n)
  (let ((loop (lambda (x) 'outer)))
    (let loop ((i 0))
      (if (< i n) (loop (+ i 1)) i))))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT: named-let name correctly shadows an enclosing binding"
           (jit-enclosing-shadow-loop 3) 3)
    (loop (+ i 1))))

;;; The sentinel fix itself (Binding::is_named_let) initially bound the
;;; loop's own name into scope BEFORE its init expressions were compiled,
;;; so an init expression legitimately referencing an ENCLOSING binding
;;; sharing the loop's name resolved to the not-yet-real sentinel instead
;;; -- a Binding with a null slot, corrupting the generated LLVM IR
;;; (caught only by verifyModule on an assertions-disabled LLVM build,
;;; would have asserted inside LoadInst's constructor on an assertions-
;;; enabled one, rather than the clean "JIT failed, fell back to
;;; bytecode" this now-fixed shape used to produce). Fixed by computing
;;; every init value before the loop's own scope (and its sentinel)
;;; exists, matching a named let's inits being evaluated in the ENCLOSING
;;; scope, same as an ordinary `let`.
;;;
;;; This exact reordering also independently closes issue #119 (a
;;; separate LLVM-JIT-only bug, filed then fixed within this same
;;; commit): named-let init expressions were getting let*-style scoping
;;; -- init i could see loop variables 0..i-1 bound by earlier iterations
;;; of the same binding loop -- instead of R7RS's let-style "every init
;;; sees only the enclosing scope". curry-jit-eval is used directly
;;; (rather than driving a wrapper past JIT_THRESHOLD) so the asserted
;;; value is unambiguously the JIT tier's own answer, not whichever tier
;;; happened to run for a given call count.
(check "JIT: named-let init expr resolves an enclosing shadow, not the sentinel"
       (curry-jit-eval
        '((lambda (n)
            (let ((loop (lambda (x) (* x 10))))
              (let loop ((i (loop n))) i)))
          5))
       50)

;;; #119's own repro, confirming the fix generalizes beyond the specific
;;; shadow-of-the-loop's-own-name case above: plain let* -> let scoping
;;; for two ordinary loop variables (b's init `a` must see the outer
;;; global, not the loop variable `a` bound moments earlier by the same
;;; named let), no local-macro or shadowing involved at all.
(define jit-119-a 100)
(check "JIT: named-let init exprs use let (not let*) scoping"
       (curry-jit-eval
        '((lambda (n) (let loop ((jit-119-a 1) (b jit-119-a)) b)) 1))
       100)

;;; Referencing a named let's own name as a plain VALUE (not calling it)
;;; has no real slot for the sentinel to provide -- this was already
;;; unsupported before the sentinel existed (curry compiles named-let
;;; purely as a backedge construct, never allocating a real closure for
;;; the loop name), but used to at least compile to a global-variable
;;; lookup that raised a clean "unbound variable" rather than reaching
;;; LLVM's IR builder with a null operand. Must decline JIT promotion
;;; cleanly (falls back to the bytecode interpreter, which is where this
;;; correctly-consistent #t answer actually comes from at every call
;;; count, not just below JIT_THRESHOLD) rather than crash.
(define (jit-value-ref-loop n)
  (let loop ((i 0)) (if (< i n) (loop (+ i 1)) (procedure? loop))))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT: named-let name used as a value declines promotion cleanly"
           (jit-value-ref-loop 3) #t)
    (loop (+ i 1))))

;;; 17. A let-syntax/letrec-syntax-local macro must not be miscompiled ─────────
;;; Regression coverage for issue #114, a residual gap in #111's fix: the
;;; JIT's macro guard only checks GLOBAL_ENV, and a let-syntax-local macro
;;; has no GLOBAL_ENV binding -- worse, the enclosing let-syntax form is
;;; evaluated once at define time to produce the closure and is never part
;;; of the closure's own src_lambda, so it can't be caught by string-
;;; matching the AST either. Fixed at the bytecode-compiler level instead:
;;; classify_head now marks Chunk::uses_local_macro whenever compiling a
;;; chunk resolves a name via resolve_syntax_local, and maybe_jit_bcc
;;; refuses JIT promotion for such a chunk, the same way it already does
;;; for target_env.
(define jit-ls-user
  (let-syntax ((m (syntax-rules () ((_ x) (+ x 1)))))
    (lambda (n) (m n))))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: let-syntax-local macro stays correct when hot"
           (jit-ls-user i) (+ i 1))
    (loop (+ i 1))))

;;; The first attempt at this fix only marked the innermost chunk actually
;;; referencing the macro, but codegen.cpp's emit_lambda compiles a nested
;;; lambda literal INLINE into its enclosing closure's own native code --
;;; so an unmarked OUTER chunk still got JIT-promoted and inline-compiled
;;; straight through, missing the correctly-set inner chunk's flag
;;; entirely. Found by two independent reviews before this ever merged.
;;; resolve_syntax_local now marks every chunk from the reference point up
;;; to the one owning the macro, not just the innermost one.
(define jit-ls-nested-user
  (let-syntax ((m (syntax-rules () ((_ x) (+ x 1)))))
    (lambda (n) ((lambda () (m n))))))
(let loop ((i 0))
  (when (< i 60)
    (check "JIT bailout: let-syntax-local macro stays correct one lambda deeper"
           (jit-ls-nested-user i) (+ i 1))
    (loop (+ i 1))))

;;; Summary ─────────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
