(define-library (surfage s27 random-bits)
  (import (scheme base))
  (export
    default-random-source
    make-random-source
    random-source?
    random-source-randomize!
    random-source-pseudo-randomize!
    random-source->random-real
    random-source->random-integer
    random-real
    random-integer)
  (begin
    ; All implementations delegate to the C-level primitives registered
    ; in builtins.c (random-real, random-integer, random-source-randomize!, etc.)
    ; Nothing extra to define — they're already in the global environment.
    ))
