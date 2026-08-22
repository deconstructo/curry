;;; Tests for the Tier 2.3 local inliner (docs/thoughts/performance-chez-
;;; kaappi.md sec5, item 2.3) -- an internal `(define name (lambda ...))`
;;; (or `(define (f params...) body...)` sugar) whose value compiles fully
;;; closed can be spliced directly into a call site's own instruction
;;; stream instead of allocating a closure and doing a real call. This file
;;; is purely a functional/end-to-end complement to src/compiler.c's own
;;; differential checks (compiler_ir_optimize_check, compiler_ir_inline_
;;; fired_check, exercised via tests/test_core.c's `core` ctest target) --
;;; those verify bytecode-level correctness and that inlining actually
;;; fires; this file verifies observable Scheme-level behavior end to end,
;;; including interactions (call/cc, dynamic-wind, tail calls, named-let)
;;; that only make sense to exercise via the real running VM.

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

;;; Basic closed helper, called multiple times as arguments to a single
;;; fused-global call -- the exact shape that exposed a real miscompilation
;;; during development (add_local aliasing a still-pending sibling
;;; argument's stack slot; see compiler.c's own commit history).
(check "multi-call fused-global"
  (let ()
    (define (sq x) (* x x))
    (+ (sq 3) (sq 4)))
  25)

(check "3+ pending arguments"
  (let ()
    (define (sq x) (* x x))
    (+ (sq 1) (sq 2) (sq 3) (sq 4)))
  30)

;;; Nested inlining: an inlined call's own argument is itself another
;;; inlined call.
(check "nested inlining"
  (let ()
    (define (sq x) (* x x))
    (define (dbl x) (+ x x))
    (sq (dbl 3)))
  36)

;;; A nested closure inside the inlined body captures a spliced-in param --
;;; exercises end_scope's OP_CLOSE_UP firing correctly for a same-frame
;;; splice, not just a real closure's own frame teardown.
(check "closure over spliced param"
  (let ()
    (define (adder x) (lambda (y) (+ x y)))
    (define add5 (adder 5))
    (add5 10))
  15)

;;; Self-recursive candidate: must not be inlined (named-let/self-tail-call
;;; already owns recursive loops), and must still be correct.
(check "self-recursive candidate"
  (let ()
    (define (fact k) (if (= k 0) 1 (* k (fact (- k 1)))))
    (fact 6))
  720)

;;; set! before the only call site poisons the registration.
(check "set! poisons before call"
  (let ()
    (define (sq x) (* x x))
    (set! sq (lambda (x) 999))
    (sq 3))
  999)

;;; Cross-Compiler poisoning: a NESTED closure mutates the outer
;;; known-local via an upvalue-resolved set!, not a same-Compiler one.
(check "set! poisons across closure boundary"
  (let ()
    (define (sq x) (* x x))
    (define mutate! (lambda () (set! sq (lambda (x) 777))))
    (mutate!)
    (sq 3))
  777)

;;; Rest-param candidate: rejected (proper-param-list gate), still correct.
(check "rest-param candidate"
  (let ()
    (define (sumall . xs) (apply + xs))
    (sumall 1 2 3 4 5))
  15)

;;; Non-symbol callee position (never reaches the inliner at all, since it
;;; only fires for a bare-symbol head) -- confirms the inliner and the
;;; ordinary generic-call path coexist correctly.
(check "generic call to a known local via non-symbol callee"
  (let ()
    (define (sq x) (* x x))
    (define ops (list sq sq))
    ((car ops) 7))
  49)

;;; Inlined call inside a named-let loop body, composing with self-tail-
;;; call classification each iteration.
(check "inlined call inside named-let loop"
  (let ()
    (define (sq x) (* x x))
    (let loop ((i 0) (acc 0))
      (if (= i 6) acc (loop (+ i 1) (+ acc (sq i))))))
  55)

;;; Inlined call in tail position of the enclosing function.
(define (tail-sq n)
  (define (sq x) (* x x))
  (sq n))
(check "inlined call in tail position" (tail-sq 9) 81)

;;; Inlined call in NON-tail position (as one operand of an enclosing
;;; call), inside a function whose own call site is itself in tail
;;; position -- exercises that the inlined body's own tail-ness is
;;; correctly derived from the call site's tail flag, not the enclosing
;;; function's.
(define (nontail-sq n)
  (define (sq x) (* x x))
  (+ 1 (sq n)))
(check "inlined call in non-tail position" (nontail-sq 9) 82)

;;; call/cc escaping out of a function whose body contains an inlined call
;;; that never gets a chance to run its own second half -- exercises that
;;; the inliner's same-frame splice doesn't corrupt continuation capture
;;; (the inlined body's locals are ordinary same-frame locals, closed over
;;; exactly like any other local a captured continuation would need to
;;; unwind past).
(check "call/cc escapes past an inlined call"
  (call/cc
    (lambda (k)
      (define (sq x) (* x x))
      (+ (sq 3) (k 'escaped) (sq 4))))
  'escaped)

;;; dynamic-wind around a call that inlines -- the closedness argument
;;; that makes inlining sound doesn't depend on dynamic-wind at all (a
;;; closed lambda's body is spliced by VALUE, not by any notion of
;;; "current dynamic extent"), but this is direct regression coverage for
;;; that claim rather than just an inspection argument.
(define dw-log '())
(let ()
  (define (sq x) (* x x))
  (dynamic-wind
    (lambda () (set! dw-log (cons 'in dw-log)))
    (lambda () (set! dw-log (cons (sq 5) dw-log)))
    (lambda () (set! dw-log (cons 'out dw-log)))))
(check "dynamic-wind around inlined call" (reverse dw-log) '(in 25 out))

;;; Oversized body (over INLINE_MAX_BODY_NODES): must fall back to a real
;;; call, not attempt to inline, and still be correct.
(check "oversized body falls back correctly"
  (let ()
    (define (sum-many x)
      (+ x x x x x x x x x x x x x x x x x x x x
         x x x x x x x x x x x x x x x x x x x x
         x x x x x x x x x x x x x x x x x x x x
         x x x x x x x x x x x x x x x x x x x x))
    (sum-many 2))
  160)

;;; Multiple distinct known-lambda locals in the same scope, each called
;;; several times -- no cross-contamination of known[] registrations by
;;; physical slot index.
(check "multiple distinct known locals"
  (let ()
    (define (sq x) (* x x))
    (define (cube x) (* x x x))
    (+ (sq 2) (cube 2) (sq 3) (cube 3)))
  (+ 4 8 9 27))

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
