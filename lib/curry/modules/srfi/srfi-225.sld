(define-library (srfi srfi-225)
  (import (srfi s225 dictionaries))
  (export
    dto? make-dto dto-ref
    dictionary? dict-empty? dict-contains? dict=? dict-pure?
    dict-ref dict-ref/default dict-comparator
    dict-set! dict-adjoin! dict-delete! dict-delete-all! dict-replace!
    dict-intern! dict-update! dict-update/default! dict-pop!
    dict-find-update!
    dict-map dict-filter dict-remove
    dict-size dict-count dict-any dict-every
    dict-keys dict-values dict-entries dict-fold dict-map->list dict->alist
    dict-for-each dict->generator dict-set!-accumulator dict-adjoin!-accumulator
    dictionary-error dictionary-error? dictionary-message dictionary-irritants
    make-alist-dto eqv-alist-dto equal-alist-dto
    srfi-69-dto hash-table-dto srfi-126-dto
    dictionary?-id dict-find-update!-id dict-comparator-id dict-map-id
    dict-pure?-id dict-remove-id dict-size-id
    dict->alist-id dict-adjoin!-id dict-adjoin!-accumulator-id
    dict-any-id dict-every-id dict-contains?-id dict-count-id
    dict-delete!-id dict-delete-all!-id dict-empty?-id
    dict-entries-id dict-filter-id dict-fold-id dict-for-each-id
    dict-intern!-id dict-keys-id dict-map->list-id dict-pop!-id
    dict-ref-id dict-ref/default-id dict-replace!-id
    dict-set!-id dict-set!-accumulator-id
    dict-update!-id dict-update/default!-id dict-values-id dict=?-id
    dict->generator-id))
