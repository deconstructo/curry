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

; R6RS: an eq?/eqv? hashtable has no user-level hash function
(check "hashtable-hash-function is #f for an eq? hashtable"
       (hashtable-hash-function (make-eq-hashtable))
       #f)
(check "hashtable-hash-function is a procedure for an equal?-mode hashtable"
       (procedure? (hashtable-hash-function (make-hashtable equal-hash equal?)))
       #t)

;;; hash-table-mutable? -- curry has no immutable hash tables, so this is
;;; always #t (same as SRFI-126's own hashtable-mutable? above it).
(check "hash-table-mutable? is #t"
       (hash-table-mutable? (make-hash-table equal-comparator))
       #t)

;;; hash-table=? / hash-table-find / hash-table-pop! / hash-table-xor! --
;;; Tier 2 gap-closing additions.

(let ((a (make-hash-table equal-comparator))
      (b (make-hash-table equal-comparator)))
  (hash-table-set! a "x" 1) (hash-table-set! a "y" 2)
  (hash-table-set! b "x" 1) (hash-table-set! b "y" 2)
  (check "hash-table=? true for equal tables" (hash-table=? equal-comparator a b) #t)
  (hash-table-set! b "y" 99)
  (check "hash-table=? false when a value differs" (hash-table=? equal-comparator a b) #f)
  (hash-table-set! b "y" 2) (hash-table-set! b "z" 3)
  (check "hash-table=? false when sizes differ" (hash-table=? equal-comparator a b) #f))

(let ((t (make-hash-table equal-comparator)))
  (hash-table-set! t "x" 1) (hash-table-set! t "y" 2)
  (check "hash-table-find locates a matching entry"
         (hash-table-find (lambda (k v) (and (= v 1) k)) t (lambda () 'none))
         "x")
  (check "hash-table-find calls failure thunk when nothing matches"
         (hash-table-find (lambda (k v) (= v 999)) t (lambda () 'none))
         'none))

(let ((t (make-hash-table equal-comparator)))
  (hash-table-set! t "only" 42)
  (call-with-values (lambda () (hash-table-pop! t))
    (lambda (k v) (check "hash-table-pop! returns the entry" (list k v) (list "only" 42))))
  (check "hash-table-pop! removes the entry" (hash-table-size t) 0)
  (check "hash-table-pop! raises on an empty table"
         (guard (e (#t 'raised)) (hash-table-pop! t))
         'raised))

(let ((a (make-hash-table equal-comparator))
      (b (make-hash-table equal-comparator)))
  (hash-table-set! a "shared" 1) (hash-table-set! a "only-a" 2)
  (hash-table-set! b "shared" 99) (hash-table-set! b "only-b" 3)
  (hash-table-xor! a b)
  (check "hash-table-xor! removes shared keys" (hash-table-contains? a "shared") #f)
  (check "hash-table-xor! keeps a-only keys" (hash-table-ref a "only-a") 2)
  (check "hash-table-xor! adds b-only keys" (hash-table-ref a "only-b") 3)
  (check "hash-table-xor! result size" (hash-table-size a) 2))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
