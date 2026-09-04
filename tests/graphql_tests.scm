;;; graphql_tests.scm — (curry graphql) issue #165 regression (unchecked
;;; handle-argument bytevector cast).
;;;
;;; No dedicated test file existed for this module before. A live query
;;; needs a real GraphQL endpoint, so this only covers the #165
;;; regression -- a forged handle is rejected before any network access
;;; is ever attempted.

(import (scheme base) (curry graphql))

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
;;; val_to_gql checked NOTHING at all -- not even a tag, let alone that
;;; the cdr was really a pointer-holding bytevector. Confirmed
;;; reproducible SIGSEGV via
;;; (graphql-query (cons 'graphql-client 42) "{x}") pre-fix.
(check "graphql-query rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (graphql-query (cons 'graphql-client 42) "{x}"))) #t)
(check "graphql-query rejects a non-pair argument (was a reproducible SIGSEGV)"
  (raises? (lambda () (graphql-query 42 "{x}"))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
