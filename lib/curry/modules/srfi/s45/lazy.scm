;; SRFI-45: Primitives for Expressing Iterative Lazy Algorithms.
;;
;; curry's core `delay`/`force`/`delay-force`/`make-promise`/`promise?`
;; already implement R7RS's promise semantics, which subsume SRFI-45's:
;; R7RS's `delay-force` is exactly SRFI-45's `lazy` (a promise whose forcing
;; may itself return another promise, iterated without growing the call
;; stack -- the whole point of SRFI-45, needed for genuinely iterative lazy
;; streams/algorithms rather than ones that blow the stack after enough
;; steps), and R7RS's `make-promise` on a non-promise value is exactly
;; SRFI-45's `eager`. This shim only needs to supply the two names.
(define-library (srfi s45 lazy)
  (import (scheme base))
  (export lazy force delay eager)
  (begin
    (define-syntax lazy
      (syntax-rules ()
        ((_ expr) (delay-force expr))))
    (define (eager v) (make-promise v))))
