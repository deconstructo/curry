;;; neo4j_tests.scm — (curry neo4j) issue #165 regression (unchecked
;;; handle-argument bytevector cast).
;;;
;;; No dedicated test file existed for this module before. A live
;;; connection needs a real Neo4j server, so this only covers the #165
;;; regression -- a forged handle is rejected before any network access
;;; is ever attempted.

(import (scheme base) (curry neo4j))

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
;;; conn_unbox checked the tag but never checked the cdr was actually a
;;; pointer-holding bytevector before curry_bytevector_ref's own
;;; unchecked as_bytes() cast dereferenced whatever was there --
;;; confirmed reproducible SIGSEGV via
;;; (neo4j-disconnect (cons 'neo4j-conn 42)) pre-fix.
(check "neo4j-disconnect rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (neo4j-disconnect (cons 'neo4j-conn 42)))) #t)
(check "neo4j-run rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (neo4j-run (cons 'neo4j-conn 42) "RETURN 1"))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
