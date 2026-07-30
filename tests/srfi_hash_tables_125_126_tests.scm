;;; (srfi s125 hash-tables) and (srfi s126 hashtables)

(import (srfi s128 comparators) (srfi s125 hash-tables) (srfi s126 hashtables))

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

;;; s125

(define t (make-hash-table string-comparator))
(hash-table-set! t "a" 1)
(hash-table-set! t "b" 2)
(check "hash-table-size after two inserts" (hash-table-size t) 2)
(check "hash-table-contains? for a present key" (hash-table-contains? t "a") #t)
(check "hash-table-ref on a present key" (hash-table-ref t "a") 1)
(check "hash-table-ref calls the failure thunk on a miss"
       (hash-table-ref t "z" (lambda () 'missing))
       'missing)
(check "hash-table-ref/default returns the default on a miss"
       (hash-table-ref/default t "z" 'dflt)
       'dflt)
(hash-table-update!/default t "a" (lambda (v) (+ v 10)) 0)
(check "hash-table-update!/default applies the updater" (hash-table-ref t "a") 11)

(define t2 (hash-table string-comparator "x" 1 "y" 2))
(check "hash-table constructor with inline kv pairs" (hash-table-size t2) 2)

(hash-table-union! t t2)
(check "hash-table-union! merges keys" (hash-table-size t) 4)

(check "hash-table-empty-copy starts empty" (hash-table-size (hash-table-empty-copy t)) 0)

(hash-table-clear! t)
(check "hash-table-clear! empties the table" (hash-table-empty? t) #t)

;;; s126

(define ht (make-eqv-hashtable))
(hashtable-set! ht 'a 1)
(hashtable-set! ht 'b 2)
(check "hashtable-size" (hashtable-size ht) 2)
(check "hashtable-contains? for a present key" (hashtable-contains? ht 'a) #t)
(check "hashtable-ref default on a miss" (hashtable-ref ht 'z 'dflt) 'dflt)
(hashtable-update! ht 'a (lambda (v) (+ v 100)) 0)
(check "hashtable-update! applies the updater" (hashtable-ref ht 'a 0) 101)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
