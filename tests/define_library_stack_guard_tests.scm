;;; Regression: non-tail recursion inside a define-library body used to
;;; SIGSEGV the whole process instead of raising a catchable condition.
;;;
;;; define-library bodies are tree-walked (modules.c's define_library_
;;; clause calls eval() directly, not the compiler+VM path top-level
;;; script/REPL code uses), and the tree-walker's own eval() had no
;;; depth guard at all -- unlike the bytecode VM, which has an explicit,
;;; catchable "call stack overflow (max N frames)" check. Deep non-tail
;;; recursion through a define-library-defined function just recursed in
;;; raw C until the real OS stack was exhausted and the process crashed.
;;;
;;; Fixed by giving eval() its own stack-depth guard (a per-thread cached
;;; stack base compared against the current stack pointer on every real
;;; C-level entry into eval() -- goto-tail iterations don't grow the
;;; stack and never reach the check), raising the same EC_STACK_OVERFLOW
;;; condition the VM's own guard uses. This is its own ctest entry
;;; (rather than folded into r7rs_tests.scm) matching this suite's
;;; existing convention for a scenario that would previously crash the
;;; whole test binary rather than just fail one assertion, so a future
;;; regression surfaces as a clear, isolated CTest failure/crash instead
;;; of taking the rest of a shared test file down with it.

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

(define-library (test deep-recursion)
  (import (scheme base))
  (export deep-sum go-catch)
  (begin
    (define (deep-sum n)
      (if (= n 0) 0 (+ 1 (deep-sum (- n 1)))))
    (define (go-catch n)
      (guard (e (#t 'caught)) (deep-sum n)))))

(import (test deep-recursion))

;; Previously: process SIGSEGV, ctest reports this as a crash, not a
;; normal PASS/FAIL. Now: raises, guard catches it, execution continues.
(check "non-tail recursion inside a define-library body raises a catchable condition instead of crashing"
       (go-catch 1000000)
       'caught)

;; A depth well past the VM's own compiled-path guard (256 frames,
;; vm.c's VM_FRAMES_MAX) but comfortably under the tree-walker's own
;; guard threshold (empirically ~1400-1600 with the current 7MB budget --
;; each eval() non-tail frame costs noticeably more C stack than a
;; compiled VM frame does) must still complete normally -- the fix must
;; not be so aggressive it breaks ordinary, previously-working recursion
;; depths.
(check "moderate non-tail recursion depth still completes normally"
       (deep-sum 1000)
       1000)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
