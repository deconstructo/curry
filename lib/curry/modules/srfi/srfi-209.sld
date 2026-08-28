(define-library (srfi srfi-209)
  (import (srfi s209 enums))
  (export
    enum-type? enum? enum-type-contains? enum=? enum<? enum>? enum<=? enum>=?
    make-enum-type
    enum-type enum-name enum-ordinal enum-value
    enum-name->enum enum-ordinal->enum enum-name->ordinal enum-name->value
    enum-ordinal->name enum-ordinal->value
    enum-type-size enum-min enum-max enum-type-enums enum-type-names enum-type-values
    enum-next enum-prev
    make-enum-comparator
    enum-empty-set enum-type->enum-set enum-set list->enum-set enum-set-copy
    enum-set-projection
    make-enumeration enum-set-universe enum-set-constructor enum-set-member?
    enum-set->list enum-set-indexer
    enum-set? enum-set-contains? enum-set-empty? enum-set-disjoint?
    enum-set=? enum-set<? enum-set>? enum-set<=? enum-set>=? enum-set-subset?
    enum-set-any? enum-set-every?
    enum-set-type
    enum-set-adjoin enum-set-adjoin! enum-set-delete enum-set-delete!
    enum-set-delete-all enum-set-delete-all!
    enum-set-size enum-set->enum-list enum-set-count enum-set-filter
    enum-set-remove enum-set-map->list enum-set-for-each enum-set-fold
    enum-set-complement enum-set-complement!
    enum-set-union enum-set-union!
    enum-set-intersection enum-set-intersection!
    enum-set-difference enum-set-difference!
    enum-set-xor enum-set-xor!
    define-enum define-enumeration
    %register-enum-type! %define-enum-lookup %define-enumeration-lookup
    %define-enum-set-ctor %normalize-enumeration-name-values))
