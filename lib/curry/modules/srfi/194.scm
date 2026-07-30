(define-library (srfi 194)
  (import (srfi s194 random-data-samples))
  (export
    make-random-integer-generator make-random-real-generator
    make-random-boolean-generator make-random-char-generator
    make-uniform-generator make-normal-generator make-exponential-generator
    make-bernoulli-generator make-binomial-generator make-geometric-generator
    make-poisson-generator make-categorical-generator))
