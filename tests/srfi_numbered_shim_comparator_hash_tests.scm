;;; (srfi N) shims for the comparator-keyed hash-table family — see
;;; srfi_numbered_shim_hash_family_tests.scm for why this is a separate
;;; file from the (srfi 69)/(srfi 90) one. (srfi 125) and (srfi 126) use
;;; disjoint names (hash-table-* vs hashtable-*) so they coexist fine.

(import (srfi 128) (srfi 125) (srfi 126))

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

(define t (make-hash-table equal-comparator))
(hash-table-set! t "x" 1)
(check "(srfi 125): hash-table-ref" (hash-table-ref t "x") 1)

(define ht (make-eq-hashtable))
(hashtable-set! ht 'a 1)
(check "(srfi 126): hashtable-ref" (hashtable-ref ht 'a 0) 1)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
