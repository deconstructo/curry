;; SRFI-31: A special form `rec` for recursive evaluation, allowing
;; self-referential expressions (most commonly a lambda) to be built
;; without a surrounding `define` or `letrec`.
(define-library (srfi s31 rec)
  (import (scheme base))
  (export rec)
  (begin
    (define-syntax rec
      (syntax-rules ()
        ((_ (name . args) body ...)
         (letrec ((name (lambda args body ...))) name))
        ((_ name expr)
         (letrec ((name expr)) name))))))
