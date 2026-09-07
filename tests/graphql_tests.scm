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

;;; ── Issue #189: unchecked direct scalar argument casts ──────────────
;;; graphql-client passed av[0] straight to curry_string with no
;;; curry_is_string check.
(check "graphql-client rejects a non-string URL"
  (raises? (lambda () (graphql-client 42))) #t)

;;; ── Issue #192: unchecked list-element casts, one layer deeper ──────
;;; alist_to_json only validated the FIRST vars-alist element's shape
;;; before dispatching into the alist path; every later element's
;;; curry_car/curry_string was unguarded. Runs before any network I/O.
(check "graphql-query rejects a vars alist whose 2nd element isn't a pair"
  (raises? (lambda ()
    (graphql-query (graphql-client "http://example.invalid") "{x}"
                    (list (cons "a" 1) 42)))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
