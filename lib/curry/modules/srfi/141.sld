(define-library (srfi 141)
  (import (srfi s141 division-operators))
  (export
    ceiling/ ceiling-quotient ceiling-remainder
    round/ round-quotient round-remainder
    euclidean/ euclidean-quotient euclidean-remainder
    balanced/ balanced-quotient balanced-remainder))
