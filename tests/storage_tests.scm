;;; storage_tests.scm — (curry storage) issue #165 regression (unchecked
;;; handle-argument bytevector cast).
;;;
;;; No dedicated test file existed for this module before. Live S3/GCS/
;;; Swift/Azure round-trips need real credentials and network access, so
;;; this only covers the #165 regression -- a forged handle is rejected
;;; before any client/network state is ever touched.

(import (scheme base) (curry storage))

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

;;; ── Issue #165: unchecked handle-argument bytevector cast ───────────
;;;
;;; val_to_client checked NOTHING at all -- not even that its argument
;;; was a pair, let alone the tag or that the cdr was really a
;;; pointer-holding bytevector. Confirmed reproducible SIGSEGV via
;;; (swift-put! (cons 'storage-client 42) "a" "b" (make-bytevector 1 0))
;;; pre-fix.
(check "swift-put! rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda ()
             (swift-put! (cons 'storage-client 42) "a" "b" (make-bytevector 1 0))))
  #t)
(check "swift-put! rejects a non-pair argument (was a reproducible SIGSEGV)"
  (raises? (lambda () (swift-put! 42 "a" "b" (make-bytevector 1 0)))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
