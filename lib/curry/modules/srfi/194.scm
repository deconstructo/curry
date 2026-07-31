(define-library (srfi 194)
  (import (srfi s194 random-data-samples))
  (import (curry private lang-aliases))
  (export
    make-random-integer-generator make-random-real-generator
    make-random-boolean-generator make-random-char-generator
    make-uniform-generator make-normal-generator make-exponential-generator
    make-bernoulli-generator make-binomial-generator make-geometric-generator
    make-poisson-generator make-categorical-generator
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; pūrum: "lot" (as in casting lots, already used for
    ;; default-random-source) is the chance/randomness root.
    banû-pūr-mithāriš 𒂍𒆜 banû-pūr-kīnim 𒄿𒋻𒂗 banû-pūr-napḫarim 𒌋𒅆𒈧
    banû-pūr-kilallān 𒈧𒋻𒇲 banû-pūr-ḫisbim 𒅁𒆜𒌋 banû-pūr-mitḫartim 𒅁𒃲𒋻
    banû-pūr-eššim 𒈷𒇽 banû-pūr-zikrim 𒀭𒉡 banû-pūr-kēnim 𒌋𒇽𒇽 banû-pūr-ṣibtim
    𒋻𒆠𒈧 banû-pūr-nikkassim 𒀸𒀭 banû-pūr-ṣīrim 𒊕𒇲𒄿)
  (begin
    (define-name-aliases
      (make-uniform-generator           banû-pūr-mithāriš            𒂍𒆜)
      (make-normal-generator            banû-pūr-kīnim               𒄿𒋻𒂗)
      (make-exponential-generator       banû-pūr-napḫarim            𒌋𒅆𒈧)
      (make-bernoulli-generator         banû-pūr-kilallān            𒈧𒋻𒇲)
      (make-binomial-generator          banû-pūr-ḫisbim              𒅁𒆜𒌋)
      (make-geometric-generator         banû-pūr-mitḫartim           𒅁𒃲𒋻)
      (make-poisson-generator           banû-pūr-eššim               𒈷𒇽)
      (make-categorical-generator       banû-pūr-zikrim              𒀭𒉡)
      (make-random-boolean-generator    banû-pūr-kēnim               𒌋𒇽𒇽)
      (make-random-char-generator       banû-pūr-ṣibtim              𒋻𒆠𒈧)
      (make-random-integer-generator    banû-pūr-nikkassim           𒀸𒀭)
      (make-random-real-generator       banû-pūr-ṣīrim               𒊕𒇲𒄿))
))
