(define-library (srfi 4)
  (import (srfi s4 uniform-vectors))
  (export
    make-u8vector u8vector u8vector? u8vector-length u8vector-ref
    u8vector-set! u8vector->list list->u8vector u8vector-copy
    u8vector-copy! u8vector-append u8vector-fill!

    make-s8vector s8vector s8vector? s8vector-length s8vector-ref
    s8vector-set! s8vector->list list->s8vector s8vector-copy
    s8vector-copy! s8vector-append s8vector-fill!

    make-u16vector u16vector u16vector? u16vector-length u16vector-ref
    u16vector-set! u16vector->list list->u16vector u16vector-copy
    u16vector-copy! u16vector-append u16vector-fill!

    make-s16vector s16vector s16vector? s16vector-length s16vector-ref
    s16vector-set! s16vector->list list->s16vector s16vector-copy
    s16vector-copy! s16vector-append s16vector-fill!

    make-u32vector u32vector u32vector? u32vector-length u32vector-ref
    u32vector-set! u32vector->list list->u32vector u32vector-copy
    u32vector-copy! u32vector-append u32vector-fill!

    make-s32vector s32vector s32vector? s32vector-length s32vector-ref
    s32vector-set! s32vector->list list->s32vector s32vector-copy
    s32vector-copy! s32vector-append s32vector-fill!

    make-u64vector u64vector u64vector? u64vector-length u64vector-ref
    u64vector-set! u64vector->list list->u64vector u64vector-copy
    u64vector-copy! u64vector-append u64vector-fill!

    make-s64vector s64vector s64vector? s64vector-length s64vector-ref
    s64vector-set! s64vector->list list->s64vector s64vector-copy
    s64vector-copy! s64vector-append s64vector-fill!

    make-f64vector f64vector f64vector? f64vector-length f64vector-ref
    f64vector-set! f64vector->list list->f64vector f64vector-copy
    f64vector-fill!))
