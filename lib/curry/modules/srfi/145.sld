(define-library (srfi 145)
  (import (srfi s145 assume))
  (import (curry private lang-aliases))
  (export
    assume assume-type
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; qâpum: "to trust" -- assume declares a trusted invariant.
    qâpum 𒍪𒁀𒄀 qâp-gattim 𒉡𒆜𒊻)
  (begin
    (define-syntax-aliases
      (assume                   qâpum                    𒍪𒁀𒄀))
    (define-name-aliases
      (assume-type                      qâp-gattim                   𒉡𒆜𒊻))
))
