(define-library (srfi srfi-224)
  (import (srfi s224 fxmappings))
  (export
    fxmapping?
    fxmapping fxmapping-unfold fxmapping-accumulate
    alist->fxmapping alist->fxmapping/combinator
    fxmapping-contains? fxmapping-empty? fxmapping-disjoint?
    fxmapping-ref fxmapping-ref/default fxmapping-min fxmapping-max
    fxmapping-adjoin fxmapping-adjoin/combinator fxmapping-set fxmapping-adjust
    fxmapping-delete fxmapping-delete-all fxmapping-update fxmapping-alter
    fxmapping-delete-min fxmapping-delete-max
    fxmapping-update-min fxmapping-update-max
    fxmapping-pop-min fxmapping-pop-max
    fxmapping-size fxmapping-find fxmapping-count fxmapping-any? fxmapping-every?
    fxmapping-map fxmapping-for-each fxmapping-fold fxmapping-fold-right
    fxmapping-map->list fxmapping-relation-map
    fxmapping-filter fxmapping-remove fxmapping-partition
    fxmapping->alist fxmapping->decreasing-alist fxmapping-keys fxmapping-values
    fxmapping->generator fxmapping->decreasing-generator
    fxmapping=? fxmapping<? fxmapping<=? fxmapping>? fxmapping>=?
    fxmapping-union fxmapping-intersection fxmapping-difference fxmapping-xor
    fxmapping-union/combinator fxmapping-intersection/combinator
    fxmapping-open-interval fxmapping-closed-interval
    fxmapping-open-closed-interval fxmapping-closed-open-interval
    fxsubmapping= fxsubmapping< fxsubmapping<= fxsubmapping> fxsubmapping>=
    fxmapping-split))
