(define-library (srfi 128)
  (import (srfi s128 comparators))
  (export
    comparator? comparator-ordered? comparator-hashable? make-comparator
    comparator-type-test-predicate comparator-equality-predicate
    comparator-ordering-predicate comparator-hash-function
    comparator-test-type comparator-check-type comparator-hash =? <? >? <=?
    >=? comparator-register-default! default-comparator
    make-default-comparator boolean-comparator real-comparator
    number-comparator char-comparator char-ci-comparator string-comparator
    string-ci-comparator symbol-comparator pair-comparator list-comparator
    vector-comparator eq-comparator eqv-comparator equal-comparator))
