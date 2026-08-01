(define-library (srfi s111 boxes)
  (import (scheme base))
  (export box box? unbox set-box!)
  (begin

    ; SRFI-111 itself only specifies the single-value case (box holds
    ; exactly one value; box/set-box! each take exactly one value argument;
    ; unbox returns exactly one value). This implementation is built
    ; variadic-capable from the start -- storing a vector of 0+ values --
    ; so it can double as (srfi s195 multiple-value-boxes)'s base: SRFI-195
    ; requires "the bindings that are exported by both SRFIs have to be the
    ; same" when a program imports both, and the cleanest way to guarantee
    ; that is for there to only be one implementation of box/box?/unbox/
    ; set-box! at all, which s195 re-exports rather than reimplementing.
    ; Calling this library's own box/unbox/set-box! with exactly one value
    ; behaves exactly per the SRFI-111 spec -- `(unbox (box 5))` => 5, not
    ; some wrapped or multi-value form, since R7RS defines `(values x)` to
    ; behave identically to plain `x` in a single-value context.

    (define-record-type <box>
      (%make-box vals)
      box?
      (vals %box-vals %set-box-vals!))

    (define (box . vals) (%make-box (list->vector vals)))

    (define (unbox b) (apply values (vector->list (%box-vals b))))

    (define (set-box! b . vals)
      (if (not (= (length vals) (vector-length (%box-vals b))))
          (error "set-box!: wrong number of values" b (vector-length (%box-vals b)) (length vals)))
      (%set-box-vals! b (list->vector vals)))))
