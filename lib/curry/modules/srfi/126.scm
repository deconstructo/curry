(define-library (srfi 126)
  (import (srfi s126 hashtables))
  (export
    make-eq-hashtable make-eqv-hashtable make-hashtable hashtable?
    hashtable-set! hashtable-ref hashtable-delete! hashtable-contains?
    hashtable-update! hashtable-copy hashtable-clear! hashtable-size
    hashtable-keys hashtable-values hashtable-entries hashtable->alist
    hashtable-walk hashtable-mutable? hashtable-hash-function
    hashtable-equivalence-function equal-hash string-hash symbol-hash))
