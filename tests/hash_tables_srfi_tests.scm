;;; (srfi s69 hash-tables) and (srfi s90 hash-tables) tests

(import (srfi s69 hash-tables) (srfi s90 hash-tables))

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

;;; make-hash-table / hash-table?

(define t (make-hash-table))
(check "hash-table? on a fresh table" (hash-table? t) #t)
(check "hash-table? on a non-table" (hash-table? 5) #f)

(hash-table-set! t 'a 1)
(hash-table-set! t 'b 2)
(check "hash-table-size after two inserts" (hash-table-size t) 2)
(check "hash-table-exists? for a present key" (hash-table-exists? t 'a) #t)
(check "hash-table-exists? for an absent key" (hash-table-exists? t 'z) #f)

;;; hash-table-ref — thunk semantics, distinct from curry's own builtin

(check "hash-table-ref on a present key" (hash-table-ref t 'a) 1)
(check "hash-table-ref calls the thunk on a miss"
       (hash-table-ref t 'z (lambda () 'missing))
       'missing)
(check "hash-table-ref signals an error on a miss with no thunk"
       (guard (e (#t 'caught)) (hash-table-ref t 'z))
       'caught)
(check "hash-table-ref/default returns the default on a miss"
       (hash-table-ref/default t 'z 'dflt)
       'dflt)
(check "hash-table-ref/default returns the value when present"
       (hash-table-ref/default t 'a 'dflt)
       1)

;;; hash-table-delete!

(hash-table-set! t 'temp 99)
(hash-table-delete! t 'temp)
(check "hash-table-delete! removes the key" (hash-table-exists? t 'temp) #f)

;;; hash-table-update! / hash-table-update!/default

(hash-table-update! t 'a (lambda (v) (+ v 10)))
(check "hash-table-update! on a present key" (hash-table-ref t 'a) 11)
(check "hash-table-update! signals an error on a miss with no default/thunk"
       (guard (e (#t 'caught)) (hash-table-update! t 'nope (lambda (v) v)))
       'caught)
(hash-table-update!/default t 'counter (lambda (v) (+ v 1)) 0)
(check "hash-table-update!/default seeds from the default on first use"
       (hash-table-ref t 'counter)
       1)

;;; hash-table-keys / hash-table-values / hash-table->alist

(check "hash-table-keys count matches size" (length (hash-table-keys t)) (hash-table-size t))
(check "hash-table-values count matches size" (length (hash-table-values t)) (hash-table-size t))
(check "hash-table->alist round-trips through alist->hash-table"
       (let* ((al (hash-table->alist t))
              (t2 (alist->hash-table al)))
         (and (= (hash-table-size t2) (hash-table-size t))
              (equal? (hash-table-ref t2 'a) (hash-table-ref t 'a))))
       #t)

;;; hash-table-walk / hash-table-fold

(check "hash-table-fold sums all values"
       (hash-table-fold t (lambda (k v acc) (+ v acc)) 0)
       (apply + (hash-table-values t)))

(let ((seen 0))
  (hash-table-walk t (lambda (k v) (set! seen (+ seen 1))))
  (check "hash-table-walk visits every entry" seen (hash-table-size t)))

;;; hash-table-copy — independence

(define t-copy (hash-table-copy t))
(hash-table-set! t-copy 'a 'changed)
(check "hash-table-copy produces an independent table"
       (list (hash-table-ref t 'a) (hash-table-ref t-copy 'a))
       (list 11 'changed))

;;; hash-table-merge!

(define merge-target (make-hash-table))
(define merge-source (make-hash-table))
(hash-table-set! merge-source 'x 1)
(hash-table-set! merge-source 'y 2)
(hash-table-merge! merge-target merge-source)
(check "hash-table-merge! copies all entries from source into target"
       (list (hash-table-ref merge-target 'x) (hash-table-ref merge-target 'y))
       (list 1 2))

;;; Comparator handling

(define eq-t (make-hash-table eq?))
(hash-table-set! eq-t 'sym 'val)
(check "eq?-comparator table works for symbol keys" (hash-table-ref eq-t 'sym) 'val)
(check "hash-table-equivalence-function reports eq? for an eq?-table"
       (eq? (hash-table-equivalence-function eq-t) eq?)
       #t)
(check "hash-table-equivalence-function reports equal? for a default table"
       (eq? (hash-table-equivalence-function t) equal?)
       #t)
(check "an unsupported custom predicate raises a clear error"
       (guard (e (#t 'caught)) (make-hash-table (lambda (a b) #t)))
       'caught)

;;; hash-table-hash-function

(check "hash-table-hash-function is itself callable"
       (integer? ((hash-table-hash-function t) 'anything))
       #t)

;;; Hash functions

(check "hash is deterministic for equal? values" (= (hash "hello") (hash "hello")) #t)
(check "hash respects equal? across compound structures"
       (= (hash (list 1 2 "three")) (hash (list 1 2 "three")))
       #t)
(check "string-hash is deterministic" (= (string-hash "abc") (string-hash "abc")) #t)
(check "string-ci-hash is case-insensitive"
       (= (string-ci-hash "abc") (string-ci-hash "ABC"))
       #t)
(check "string-hash is case-sensitive (differs from string-ci-hash's collapsing)"
       (= (string-hash "abc") (string-hash "ABC"))
       #f)
(check "hash respects an explicit bound" (< (hash "hello" 100) 100) #t)
(check "hash-by-identity returns an integer" (integer? (hash-by-identity 42)) #t)

;;; SRFI-90 make-table

(define mt (make-table))
(hash-table-set! mt 'k 'v)
(check "make-table with no args behaves like make-hash-table" (hash-table-ref mt 'k) 'v)

(define mt2 (make-table 'test: eq?))
(check "make-table honors a quoted test: keyword"
       (eq? (hash-table-equivalence-function mt2) eq?)
       #t)

(define mt3 (make-table 'test: eq? 'size: 64 'min-load: 0.25 'max-load: 0.75
                         'weak-keys: #f 'weak-values: #f 'hash: (lambda (x) 0)))
(hash-table-set! mt3 'k2 'v2)
(check "make-table accepts and ignores all advisory keywords alongside test:"
       (hash-table-ref mt3 'k2)
       'v2)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
