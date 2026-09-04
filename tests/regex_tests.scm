;;; regex_tests.scm — (curry regex) basic correctness + issue #165
;;; regression (unchecked handle-argument bytevector cast).
;;;
;;; No dedicated test file existed for this module before -- added
;;; alongside the #165 fix since that fix is specifically about
;;; get_regex rejecting a forged handle cleanly instead of crashing.

(import (scheme base) (curry regex))

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

(define rx (regex-compile "^[0-9]+$"))
(check "regex-match on matching input" (regex-match rx "12345") '((0 . 5)))
(check "regex-match on non-matching input" (regex-match rx "abc") #f)
(regex-free rx)

;;; ── Issue #165: unchecked handle-argument bytevector cast ───────────
;;;
;;; get_regex checked the tag (car) but never checked the cdr was
;;; actually a pointer-holding bytevector before unpack_ptr's
;;; curry_bytevector_ref dereferenced whatever was there -- confirmed
;;; reproducible SIGSEGV via (regex-free (cons 'regex 42)) pre-fix.
(check "regex-free rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (regex-free (cons 'regex 42)))) #t)
(check "regex-match rejects a forged handle (was a reproducible SIGSEGV)"
  (raises? (lambda () (regex-match (cons 'regex 42) "x"))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
