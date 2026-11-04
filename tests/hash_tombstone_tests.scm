;;; Hash tables and sets: tombstones from deletions must count toward the
;;; load factor (a table full of tombstones and no empty slot made every
;;; lookup of an absent key loop forever), and hash-table-set! must not
;;; store a key twice when it sits past a tombstone in its probe chain.
;;; Regression tests for issue #353.

(import (scheme base) (scheme write) (srfi 69))

(define pass-count 0)
(define fail-count 0)
(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display name)
             (display " expected ") (write expected)
             (display " got ") (write actual) (newline))))

;; Many insert/delete cycles of distinct keys, then absent-key lookups.
;; Before the fix this hung once the tombstones filled the table.
(let ((h (make-hash-table eq?)))
  (let loop ((i 0))
    (when (< i 5000)
      (let ((k (list i)))
        (hash-table-set! h k #t)
        (hash-table-delete! h k))
      (loop (+ i 1))))
  (check "table empty after churn" 0 (hash-table-size h))
  (check "absent key terminates" #f (hash-table-exists? h (list 'absent))))

;; A key stored past a tombstone is updated in place, not duplicated.
(let ((h (make-hash-table equal?)))
  (let loop ((i 0))
    (when (< i 200)
      (hash-table-set! h i i)
      (loop (+ i 1))))
  (let loop ((i 0))
    (when (< i 200)
      (when (even? i) (hash-table-delete! h i))
      (loop (+ i 1))))
  (let loop ((i 0))
    (when (< i 200)
      (hash-table-set! h i (* 2 i))
      (loop (+ i 1))))
  (check "no duplicate keys" 200 (hash-table-size h))
  (let loop ((i 0))
    (when (< i 200)
      (hash-table-delete! h i)
      (loop (+ i 1))))
  (check "one delete removes the key" 0 (hash-table-size h))
  (check "deleted key is gone" #f (hash-table-exists? h 7)))

(display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(exit (if (= fail-count 0) 0 1))
