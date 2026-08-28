(define-library (srfi srfi-27)
  (import (srfi s27 random-bits))
  (import (curry private lang-aliases))
  (export
    default-random-source make-random-source random-source?
    random-source-randomize! random-source-pseudo-randomize!
    random-source-state-ref random-source-state-set!
    random-source->random-real random-source->random-integer random-real
    random-integer
    ;; Akkadian synonym -- pūrum: "lot" (as in casting lots), the genuine
    ;; OB word for chance/randomness. kayyamānum: "regular, standing" --
    ;; the fixed/default lot.
    pūru-kayyamānum 𒈠𒈠𒁹)
  (begin
    (define-name-aliases
      (default-random-source  pūru-kayyamānum  𒈠𒈠𒁹))))
