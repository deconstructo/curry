(define-library (srfi 113)
  (import (srfi s113 sets-and-bags))
  (export
    set set? set-contains? set-empty? set-disjoint? set-member set-adjoin
    set-adjoin! set-delete set-delete! set-delete-all set-delete-all!
    set-size set-find set-count set-any? set-every? set-map set-for-each
    set-fold set-filter set-filter! set-remove set-remove! set-partition
    set-copy set->list list->set set=? set<=? set<? set>=? set>? set-union
    set-union! set-intersection set-intersection! set-difference
    set-difference! set-xor set-xor! set-comparator bag bag? bag-contains?
    bag-empty? bag-size bag-unique-size bag-element-count bag-adjoin
    bag-adjoin! bag-delete bag-delete! bag-for-each bag-fold bag-map
    bag-filter bag-any? bag-every? bag-count bag-copy bag->list list->bag
    bag->alist alist->bag bag-union bag-union! bag-intersection bag-sum
    bag-sum! bag-product bag-product! bag=? bag-comparator))
