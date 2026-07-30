(define-library (srfi 27)
  (import (srfi s27 random-bits))
  (export
    default-random-source make-random-source random-source?
    random-source-randomize! random-source-pseudo-randomize!
    random-source->random-real random-source->random-integer random-real
    random-integer))
