;;; sync_tests.scm — (curry sync) basic correctness + issue #165
;;; regression (unchecked handle-argument bytevector cast).
;;;
;;; No dedicated test file existed for this module before.

(import (scheme base) (curry sync))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

;;; ── Basic correctness ────────────────────────────────────────────────

(define m (make-mutex))
(mutex-lock! m)
(mutex-unlock! m)
(check "mutex? on a real mutex" (mutex? m) #t)

(define cv (make-condvar))
(check "condvar? on a real condvar" (condvar? cv) #t)

(define sem (make-semaphore 1))
(sem-wait! sem)
(sem-post! sem)
(check "semaphore-value after wait/post" (sem-value sem) 1)

;;; ── Issue #165: unchecked handle-argument bytevector cast ───────────
;;;
;;; get_mutex/get_cond/get_sem checked the tag but never checked the cdr
;;; was actually a pointer-holding bytevector before unpack_ptr's
;;; curry_bytevector_ref dereferenced whatever was there -- confirmed
;;; reproducible SIGSEGV via (mutex-lock! (cons 'mutex 42)) pre-fix.
(check "mutex-lock! rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (mutex-lock! (cons 'mutex 42)))) #t)
(check "condvar wait rejects a forged mutex handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (cond-wait! cv (cons 'mutex 42)))) #t)
(check "sem-wait! rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (sem-wait! (cons 'semaphore 42)))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
