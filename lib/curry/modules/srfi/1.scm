(define-library (srfi 1)
  (import (srfi s1 lists))
  (export
    cons car cdr caaar cadar caar cdar list list* make-list length append
    reverse list-tail list-ref last-pair map for-each filter fold-left
    fold-right fold fold-right iota any every remove delete append-map
    filter-map flat-map take drop take-while drop-while count partition first
    second third fourth fifth))
