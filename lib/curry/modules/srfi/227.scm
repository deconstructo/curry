(define-library (srfi 227)
  (import (srfi s227 optional-arguments))
  (import (curry private lang-aliases))
  (export
    opt-lambda let-optionals let-optionals* default-object default-object?
    %opt-bind %opt-bind-optional
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; watrum: "surplus, extra" (already used for extended-gcd/
    ;; abundant?) fits an optional/extra argument. puzrum marks the
    ;; %-prefixed internal helpers, as elsewhere.
    epēšu-watrum 𒈷𒉡𒇲 leqû-mala 𒂍𒀭 leqû-mala-šanîš 𒊻𒌝 watrum-puzrum 𒀭𒅆𒈠
    watrum-puzrum-šanîš 𒁀𒅁 watru-kayyamānum 𒆜𒄿 watru-kayyamānum? 𒄿𒆠𒇽)
  (begin
    (define-syntax-aliases
      (opt-lambda               epēšu-watrum             𒈷𒉡𒇲)
      (let-optionals            leqû-mala                𒂍𒀭)
      (let-optionals*           leqû-mala-šanîš          𒊻𒌝)
      (%opt-bind                watrum-puzrum            𒀭𒅆𒈠)
      (%opt-bind-optional       watrum-puzrum-šanîš      𒁀𒅁))
    (define-name-aliases
      (default-object                   watru-kayyamānum             𒆜𒄿)
      (default-object?                  watru-kayyamānum?            𒄿𒆠𒇽))
))
