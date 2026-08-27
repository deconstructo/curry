(define-library (srfi srfi-133)
  (import (srfi s133 vectors))
  (import (curry private lang-aliases))
  (export
    make-vector vector vector? vector-length vector-ref vector-set!
    vector->list list->vector vector-fill! vector-copy vector-copy!
    vector-append vector-map vector-for-each vector-empty? vector=
    vector-swap! reverse! vector-reverse! vector-reverse!* vector-index
    vector-index-right vector-count vector-any vector-every vector-fold
    vector-fold-right vector-binary-search vector-concatenate vector-unfold
    vector-unfold-right vector-unfold! vector-unfold-right!
    vector-reverse-copy vector-append-subvectors vector-map! vector-cumulate
    vector-skip vector-skip-right vector-partition
    reverse-vector->list reverse-list->vector
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; Extends the vector root (ṣindum) already established for the
    ;; R7RS core vector procedures.
    turru-ina 𒇲𒈧𒇽 mimma-ṣindim 𒈠𒈧𒈧 bêr-ṣindim 𒈠𒈷 puḫur-ṣindī 𒊻𒄿 mīnu-ṣindim
    𒊻𒀀 ṣindum-rīqum? 𒂗𒆜𒀸 gabbu-ṣindim 𒅁𒅁𒉡 lapāt-ṣindim 𒈧𒀭𒅁
    lapāt-ṣindim-imittam 𒇽𒁹 ašar-ṣindim 𒌑𒇲𒌋 ašar-ṣindim-imittam 𒂍𒊻
    turru-ṣindim-ina 𒇲𒀀 turru-ṣindim-ina-šanîtum 𒄿𒆠𒈷 šutbû-ṣindim-ina 𒅆𒍪𒂍
    paṭār-ṣindim 𒄀𒅆𒂗 paṭār-ṣindim-imittam 𒄿𒈷𒊻 mitḫār-ṣindim 𒇲𒀭𒂍)
  (begin
    (define-name-aliases
      (reverse!                         turru-ina                    𒇲𒈧𒇽)
      (vector-any                       mimma-ṣindim                 𒈠𒈧𒈧)
      (vector-binary-search             bêr-ṣindim                   𒈠𒈷)
      (vector-concatenate               puḫur-ṣindī                  𒊻𒄿)
      (vector-count                     mīnu-ṣindim                  𒊻𒀀)
      (vector-empty?                    ṣindum-rīqum?                𒂗𒆜𒀸)
      (vector-every                     gabbu-ṣindim                 𒅁𒅁𒉡)
      (vector-fold                      lapāt-ṣindim                 𒈧𒀭𒅁)
      (vector-fold-right                lapāt-ṣindim-imittam         𒇽𒁹)
      (vector-index                     ašar-ṣindim                  𒌑𒇲𒌋)
      (vector-index-right               ašar-ṣindim-imittam          𒂍𒊻)
      (vector-reverse!                  turru-ṣindim-ina             𒇲𒀀)
      (vector-reverse!*                 turru-ṣindim-ina-šanîtum     𒄿𒆠𒈷)
      (vector-swap!                     šutbû-ṣindim-ina             𒅆𒍪𒂍)
      (vector-unfold                    paṭār-ṣindim                 𒄀𒅆𒂗)
      (vector-unfold-right              paṭār-ṣindim-imittam         𒄿𒈷𒊻)
      (vector=                          mitḫār-ṣindim                𒇲𒀭𒂍))))
