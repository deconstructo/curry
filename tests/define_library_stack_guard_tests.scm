;;; Regression: non-tail recursion inside a define-library body used to
;;; SIGSEGV the whole process instead of raising a catchable condition.
;;;
;;; Originally, define-library bodies were tree-walked (modules.c's
;;; define_library_clause called eval() directly, not the compiler+VM
;;; path top-level script/REPL code used), and the tree-walker's own
;;; eval() had no depth guard at all -- unlike the bytecode VM, which
;;; has an explicit, catchable "call stack overflow (max N frames)"
;;; check. Deep non-tail recursion through a define-library-defined
;;; function just recursed in raw C until the real OS stack was
;;; exhausted and the process crashed.
;;;
;;; First fixed by giving eval() its own stack-depth guard (a per-thread
;;; cached stack base and REAL queried stack size -- not a fixed assumed
;;; byte budget, see the workpool-thread checks below for why that
;;; mattered -- compared against the current stack pointer on every real
;;; C-level entry into eval(); goto-tail iterations don't grow the stack
;;; and never reach the check), raising the same EC_STACK_OVERFLOW
;;; condition the VM's own guard uses.
;;;
;;; The eval-elimination migration later moved define-library bodies off
;;; the tree-walker entirely -- they now compile and run through the VM
;;; (modules.c calls vm_eval, not eval()) -- so it's the VM's own
;;; stricter 256-frame guard (vm.c's VM_FRAMES_MAX) that actually applies
;;; here now, not eval()'s ~1400-1600-frame budget. The catchable-
;;; condition behavior this test exists to verify is unaffected either
;;; way; only the specific depth that "moderate, still completes
;;; normally" can mean changed (see below). This is its own ctest entry
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

;; A depth comfortably under the VM's own compiled-path guard (256
;; frames, vm.c's VM_FRAMES_MAX) must still complete normally -- the
;; guard must not be so aggressive it breaks ordinary, previously-
;; working recursion depths. define-library bodies are now compiled and
;; run through the VM (the eval-elimination migration), not tree-walked,
;; so it's the VM's own stricter 256-frame guard that applies here, not
;; the tree-walker's much higher ~1400-1600 threshold this test used to
;; target -- 1000 legitimately overflows now (it did not before this
;; migration), so 100 is the depth actually being exercised, matching
;; the parallel-worker-thread variant of this same check below.
(check "moderate non-tail recursion depth still completes normally"
       (deep-sum 100)
       100)

;; Regression: independent security review found the guard's original
;; fixed 7MB threshold assumed every thread has an ~8MB stack like the
;; main thread and actors.c's own actor threads do -- but curry's
;; parallel map/reduce/for-each/par worker pool (workpool.c) spawned its
;; threads with a NULL pthread_attr_t (platform default stack size,
;; 512KB on macOS), so the fixed threshold never fired there and the
;; real, much smaller stack still exhausted and crashed the process
;; exactly as before this guard existed at all. Fixed two ways: the
;; guard now queries each thread's REAL stack size instead of assuming
;; one, and workpool.c now explicitly requests an 8MB stack (matching
;; actors.c's existing convention) so worker threads get the same
;; headroom as every other thread in the process. map auto-parallelizes
;; above map_par_threshold (default 8 elements, src/builtins_curry.c),
;; so a 12-element list here genuinely exercises worker threads, not
;; just the calling thread.
(check "non-tail recursion inside a define-library body, run on parallel map worker threads, doesn't crash"
       (map (lambda (x) (go-catch 1000000)) '(1 2 3 4 5 6 7 8 9 10 11 12))
       '(caught caught caught caught caught caught caught caught caught caught caught caught))
(check "moderate non-tail recursion depth on parallel map worker threads still completes normally"
       (map (lambda (x) (deep-sum 100)) '(1 2 3 4 5 6 7 8 9 10 11 12))
       '(100 100 100 100 100 100 100 100 100 100 100 100))

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
