(define-library (srfi 125)
  (import (srfi s125 hash-tables))
  (export
    make-hash-table hash-table hash-table-unfold hash-table?
    hash-table-comparator hash-table-contains? hash-table-exists?
    hash-table-empty? hash-table-size hash-table-count hash-table-ref
    hash-table-ref/default hash-table-set! hash-table-delete!
    hash-table-intern! hash-table-update! hash-table-update!/default
    hash-table-clear! hash-table-copy hash-table-empty-copy hash-table-keys
    hash-table-values hash-table-entries hash-table->alist alist->hash-table
    hash-table-walk hash-table-for-each hash-table-map->list hash-table-fold
    hash-table-count-matching hash-table-map! hash-table-prune!
    hash-table-union! hash-table-intersection! hash-table-difference!))
