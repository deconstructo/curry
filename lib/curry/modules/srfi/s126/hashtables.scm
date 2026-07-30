(define-library (srfi s126 hashtables)
  (import (scheme base) (srfi s128 comparators) (srfi s125 hash-tables))
  (export
    make-eq-hashtable make-eqv-hashtable make-hashtable
    hashtable? hashtable-set! hashtable-ref hashtable-delete!
    hashtable-contains? hashtable-update! hashtable-copy hashtable-clear!
    hashtable-size hashtable-keys hashtable-values hashtable-entries
    hashtable->alist hashtable-walk hashtable-mutable?
    hashtable-hash-function hashtable-equivalence-function
    equal-hash string-hash symbol-hash)
  (begin

    ; R6RS-flavored naming layered on (srfi s125 hash-tables). Like that
    ; library, `make-hashtable`'s hash/equiv arguments are honored only when
    ; they resolve to curry's three native equivalences (eq?/eqv?/equal?);
    ; see s125's header comment for the rationale.

    (define (make-eq-hashtable . args) (make-hash-table eq-comparator))
    (define (make-eqv-hashtable . args) (make-hash-table eqv-comparator))

    (define (make-hashtable hash-fn equiv-fn . args)
      (make-hash-table
       (cond ((eq? equiv-fn eq?) eq-comparator)
             ((eq? equiv-fn eqv?) eqv-comparator)
             (else equal-comparator))))

    (define hashtable? hash-table?)
    (define hashtable-set! hash-table-set!)
    (define (hashtable-ref t key default) (hash-table-ref/default t key default))
    (define hashtable-delete! hash-table-delete!)
    (define hashtable-contains? hash-table-contains?)
    (define (hashtable-update! t key proc default)
      (hash-table-update!/default t key proc default))
    (define hashtable-copy hash-table-copy)
    (define hashtable-clear! hash-table-clear!)
    (define hashtable-size hash-table-size)
    (define hashtable-keys hash-table-keys)
    (define hashtable-values hash-table-values)
    (define hashtable-entries hash-table-entries)
    (define hashtable->alist hash-table->alist)
    (define hashtable-walk hash-table-walk)
    (define (hashtable-mutable? t) #t)

    (define (hashtable-equivalence-function t)
      (comparator-equality-predicate (hash-table-comparator t)))

    (define (hashtable-hash-function t) equal-hash)

    (define (equal-hash obj) (comparator-hash equal-comparator obj))
    (define (string-hash s) (comparator-hash string-comparator s))
    (define (symbol-hash s) (comparator-hash symbol-comparator s))))
