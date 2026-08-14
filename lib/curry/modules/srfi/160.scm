(define-library (srfi 160)
  (import (srfi s160 uniform-vectors))
  (export
    make-u8vector u8vector u8vector? u8vector-length u8vector-ref
    u8vector-set! u8vector->list list->u8vector u8vector-copy
    u8vector-append u8vector-copy! u8vector-fill! u8vector-empty? u8vector=
    u8vector-swap! u8vector-reverse! u8vector-reverse-copy u8vector-map
    u8vector-map! u8vector-for-each u8vector-count u8vector-index
    u8vector-index-right u8vector-skip u8vector-skip-right u8vector-any
    u8vector-every u8vector-filter u8vector-remove u8vector-partition
    u8vector-fold u8vector-fold-right u8vector-concatenate u8vector-unfold
    u8vector-unfold-right u8vector-comparator u8vector->generator
    make-u8vector-generator make-s8vector s8vector s8vector?
    s8vector-length s8vector-ref s8vector-set! s8vector->list
    list->s8vector s8vector-copy s8vector-append s8vector-copy!
    s8vector-fill! s8vector-empty? s8vector= s8vector-swap!
    s8vector-reverse! s8vector-reverse-copy s8vector-map s8vector-map!
    s8vector-for-each s8vector-count s8vector-index s8vector-index-right
    s8vector-skip s8vector-skip-right s8vector-any s8vector-every
    s8vector-filter s8vector-remove s8vector-partition s8vector-fold
    s8vector-fold-right s8vector-concatenate s8vector-unfold
    s8vector-unfold-right s8vector-comparator s8vector->generator
    make-s8vector-generator make-u16vector u16vector u16vector?
    u16vector-length u16vector-ref u16vector-set! u16vector->list
    list->u16vector u16vector-copy u16vector-append u16vector-copy!
    u16vector-fill! u16vector-empty? u16vector= u16vector-swap!
    u16vector-reverse! u16vector-reverse-copy u16vector-map u16vector-map!
    u16vector-for-each u16vector-count u16vector-index
    u16vector-index-right u16vector-skip u16vector-skip-right u16vector-any
    u16vector-every u16vector-filter u16vector-remove u16vector-partition
    u16vector-fold u16vector-fold-right u16vector-concatenate
    u16vector-unfold u16vector-unfold-right u16vector-comparator
    u16vector->generator make-u16vector-generator make-s16vector s16vector
    s16vector? s16vector-length s16vector-ref s16vector-set!
    s16vector->list list->s16vector s16vector-copy s16vector-append
    s16vector-copy! s16vector-fill! s16vector-empty? s16vector=
    s16vector-swap! s16vector-reverse! s16vector-reverse-copy s16vector-map
    s16vector-map! s16vector-for-each s16vector-count s16vector-index
    s16vector-index-right s16vector-skip s16vector-skip-right s16vector-any
    s16vector-every s16vector-filter s16vector-remove s16vector-partition
    s16vector-fold s16vector-fold-right s16vector-concatenate
    s16vector-unfold s16vector-unfold-right s16vector-comparator
    s16vector->generator make-s16vector-generator make-u32vector u32vector
    u32vector? u32vector-length u32vector-ref u32vector-set!
    u32vector->list list->u32vector u32vector-copy u32vector-append
    u32vector-copy! u32vector-fill! u32vector-empty? u32vector=
    u32vector-swap! u32vector-reverse! u32vector-reverse-copy u32vector-map
    u32vector-map! u32vector-for-each u32vector-count u32vector-index
    u32vector-index-right u32vector-skip u32vector-skip-right u32vector-any
    u32vector-every u32vector-filter u32vector-remove u32vector-partition
    u32vector-fold u32vector-fold-right u32vector-concatenate
    u32vector-unfold u32vector-unfold-right u32vector-comparator
    u32vector->generator make-u32vector-generator make-s32vector s32vector
    s32vector? s32vector-length s32vector-ref s32vector-set!
    s32vector->list list->s32vector s32vector-copy s32vector-append
    s32vector-copy! s32vector-fill! s32vector-empty? s32vector=
    s32vector-swap! s32vector-reverse! s32vector-reverse-copy s32vector-map
    s32vector-map! s32vector-for-each s32vector-count s32vector-index
    s32vector-index-right s32vector-skip s32vector-skip-right s32vector-any
    s32vector-every s32vector-filter s32vector-remove s32vector-partition
    s32vector-fold s32vector-fold-right s32vector-concatenate
    s32vector-unfold s32vector-unfold-right s32vector-comparator
    s32vector->generator make-s32vector-generator make-u64vector u64vector
    u64vector? u64vector-length u64vector-ref u64vector-set!
    u64vector->list list->u64vector u64vector-copy u64vector-append
    u64vector-copy! u64vector-fill! u64vector-empty? u64vector=
    u64vector-swap! u64vector-reverse! u64vector-reverse-copy u64vector-map
    u64vector-map! u64vector-for-each u64vector-count u64vector-index
    u64vector-index-right u64vector-skip u64vector-skip-right u64vector-any
    u64vector-every u64vector-filter u64vector-remove u64vector-partition
    u64vector-fold u64vector-fold-right u64vector-concatenate
    u64vector-unfold u64vector-unfold-right u64vector-comparator
    u64vector->generator make-u64vector-generator make-s64vector s64vector
    s64vector? s64vector-length s64vector-ref s64vector-set!
    s64vector->list list->s64vector s64vector-copy s64vector-append
    s64vector-copy! s64vector-fill! s64vector-empty? s64vector=
    s64vector-swap! s64vector-reverse! s64vector-reverse-copy s64vector-map
    s64vector-map! s64vector-for-each s64vector-count s64vector-index
    s64vector-index-right s64vector-skip s64vector-skip-right s64vector-any
    s64vector-every s64vector-filter s64vector-remove s64vector-partition
    s64vector-fold s64vector-fold-right s64vector-concatenate
    s64vector-unfold s64vector-unfold-right s64vector-comparator
    s64vector->generator make-s64vector-generator make-f64vector f64vector
    f64vector? f64vector-length f64vector-ref f64vector-set!
    f64vector->list list->f64vector f64vector-copy f64vector-append
    f64vector-fill! f64vector-empty? f64vector= f64vector-swap!
    f64vector-reverse! f64vector-reverse-copy f64vector-map f64vector-map!
    f64vector-for-each f64vector-count f64vector-index
    f64vector-index-right f64vector-skip f64vector-skip-right f64vector-any
    f64vector-every f64vector-filter f64vector-remove f64vector-partition
    f64vector-fold f64vector-fold-right f64vector-concatenate
    f64vector-unfold f64vector-unfold-right f64vector-comparator
    f64vector->generator make-f64vector-generator comparator? =? <? >? <=?
    >=?))
