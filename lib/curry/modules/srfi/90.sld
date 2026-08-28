(define-library (srfi 90)
  (import (srfi s90 hash-tables))
  (import (curry private lang-aliases))
  (export
    make-table
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; kunukkum: "seal" -- same hash-table root as SRFI 69/125.
    banû-kunukkim 𒀀𒀀)
  (begin
    (define-name-aliases
      (make-table                   banû-kunukkim                𒀀𒀀))
))
