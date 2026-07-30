(define-library (srfi 133)
  (import (srfi s133 vectors))
  (export
    make-vector vector vector? vector-length vector-ref vector-set!
    vector->list list->vector vector-fill! vector-copy vector-copy!
    vector-append vector-map vector-for-each vector-empty? vector=
    vector-swap! reverse! vector-reverse! vector-reverse!* vector-index
    vector-index-right vector-count vector-any vector-every vector-fold
    vector-fold-right vector-binary-search vector-concatenate vector-unfold
    vector-unfold-right))
