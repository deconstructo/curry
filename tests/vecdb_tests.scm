;;; vecdb_tests.scm — (curry vecdb) basic correctness + issue #179
;;; regression (unchecked vector/element casts on data-payload args).
;;;
;;; No dedicated test file existed for this module before.

(import (scheme base) (curry vecdb))

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

(define db (vecdb-make 3))
(vecdb-add db 1 (vector 1.0 0.0 0.0))
(vecdb-add db 2 (vector 0.0 1.0 0.0))
(vecdb-add db 3 (vector 0.0 0.0 1.0))
(check "vecdb-size after three adds" (vecdb-size db) 3)
(check "vecdb-search finds the exact match first"
  (caar (vecdb-search db (vector 1.0 0.0 0.0) 1)) 1)
(vecdb-remove db 1)
(check "vecdb-size after remove" (vecdb-size db) 2)

;;; ── Issue #179: unchecked vector/element casts on data-payload args ────
;;;
;;; vecdb-add/vecdb-search never checked their vector argument was
;;; actually a vector before curry_vector_length/_ref's unchecked
;;; as_vec() cast -- same class as #167's forged-image-vector bug. Even
;;; after checking the outer vector, each ELEMENT also needs to be
;;; numeric before curry_float, which is itself unchecked for anything
;;; but a fixnum/flonum. Confirmed reproducible SIGSEGV via
;;; (vecdb-add db 1 42) and (vecdb-add db 1 (vector "x" 1 2)) pre-fix.
(check "vecdb-add rejects a non-vector argument (was a reproducible SIGSEGV)"
  (raises? (lambda () (vecdb-add db 4 42))) #t)
(check "vecdb-add rejects a vector with a non-numeric element (was a reproducible SIGSEGV)"
  (raises? (lambda () (vecdb-add db 4 (vector "x" 1 2)))) #t)
(check "vecdb-search rejects a non-vector argument (was a reproducible SIGSEGV)"
  (raises? (lambda () (vecdb-search db 42 1))) #t)
(check "vecdb-search rejects a vector with a non-numeric element (was a reproducible SIGSEGV)"
  (raises? (lambda () (vecdb-search db (vector 'a 'b 'c) 1))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
