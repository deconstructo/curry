;;; (srfi N) shims for the hash-table/set family — kept in a separate file
;;; from srfi_numbered_shims_tests.scm because (srfi 69)/(srfi 90) and
;;; (srfi 125) both export a generic `make-hash-table` with different
;;; semantics; importing both into one script has the same "which one wins"
;;; ambiguity any Scheme has for colliding imports, so each compatible group
;;; gets its own import here instead.

(import (srfi 69) (srfi 90))

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

(define t69 (make-hash-table))
(hash-table-set! t69 'a 1)
(check "(srfi 69): hash-table-ref" (hash-table-ref t69 'a) 1)
(check "(srfi 90): make-table is bound" (procedure? make-table) #t)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
