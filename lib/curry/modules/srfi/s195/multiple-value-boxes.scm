(define-library (srfi s195 multiple-value-boxes)
  (import (scheme base) (srfi s111 boxes))
  (export box box? unbox set-box! box-arity unbox-value set-box-value!)
  (begin

    ; SRFI-195 extends SRFI-111 to hold zero-or-more values instead of
    ; exactly one, and requires "the bindings that are exported by both
    ; SRFIs have to be the same" when a program imports both. (srfi s111
    ; boxes) is already built variadic-capable for exactly this reason, so
    ; box/box?/unbox/set-box! are re-exported here completely unchanged --
    ; not reimplemented -- guaranteeing that requirement trivially. Only
    ; the three genuinely new introspection procedures are added, built on
    ; s111's own public unbox/set-box! (not any private internal state),
    ; so this library doesn't need to know how a box is represented.

    (define (%replace lst i v)
      (if (= i 0) (cons v (cdr lst)) (cons (car lst) (%replace (cdr lst) (- i 1) v))))

    ; curry's core list-ref has no bounds check of its own (an out-of-range
    ; index segfaults rather than raising), so every index into a box's
    ; values must be validated here before it ever reaches list-ref/%replace.
    (define (%check-index proc b i)
      (if (or (not (integer? i)) (< i 0) (>= i (box-arity b)))
          (error (string-append (symbol->string proc) ": index out of range") b i)))

    (define (box-arity b)
      (call-with-values (lambda () (unbox b)) (lambda vals (length vals))))

    (define (unbox-value b i)
      (%check-index 'unbox-value b i)
      (call-with-values (lambda () (unbox b)) (lambda vals (list-ref vals i))))

    (define (set-box-value! b i obj)
      (%check-index 'set-box-value! b i)
      (call-with-values (lambda () (unbox b))
        (lambda vals (apply set-box! b (%replace vals i obj)))))))
