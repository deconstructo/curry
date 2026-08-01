(define-library (srfi srfi-128)
  (import (srfi s128 comparators))
  (import (curry private lang-aliases))
  (export
    comparator? comparator-ordered? comparator-hashable? make-comparator
    comparator-type-test-predicate comparator-equality-predicate
    comparator-ordering-predicate comparator-hash-function
    comparator-test-type comparator-check-type comparator-hash =? <? >? <=?
    >=? comparator-register-default! default-comparator
    make-default-comparator boolean-comparator real-comparator
    number-comparator char-comparator char-ci-comparator string-comparator
    string-ci-comparator symbol-comparator pair-comparator list-comparator
    vector-comparator eq-comparator eqv-comparator equal-comparator
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; dayyānum: "judge" -- a comparator decides order/equality, exactly
    ;; what a judge does.
    ṣeḫēr-dayyānim? 𒊕𒌋𒁹 ṣeḫēr-mitḫār-dayyānim? 𒄿𒈷𒅆 mitḫār-dayyānim? 𒀀𒋻𒁹
    rabû-dayyānim? 𒄀𒄿 rabû-mitḫār-dayyānim? 𒅆𒂍 dayyānum? 𒁀𒈠𒀸
    dayyānum-šutēšurum? 𒂍𒅁 dayyānum-kunukkum? 𒁀𒈧𒉡 banû-dayyānim 𒄿𒊕
    dayyān-gattim 𒈧𒌝 dayyān-mitḫārim 𒆜𒆠 dayyān-šutēšurim 𒁀𒊻𒃲 epšet-dayyānim
    𒀸𒁹𒀸 parās-gattim 𒍪𒈷𒈧 naṣār-gattim 𒋻𒆜 kunuk-dayyānim 𒈠𒉡𒇲
    šakān-dayyān-kayyamānim 𒈷𒄿𒈷 dayyān-kayyamānum 𒊕𒅁𒈧 banû-dayyān-kayyamānim
    𒆠𒉡 dayyān-kēnim 𒁀𒀸𒀀 dayyān-ṣīrim 𒂍𒄿𒌑 dayyān-nikkassim 𒌋𒄿𒂗 dayyān-ṣibtim
    𒌋𒌝𒅁 dayyān-ṣibti-mithāriš 𒄀𒀭𒁀 dayyān-ṭuppim 𒊕𒅆𒌝 dayyān-ṭuppi-mithāriš
    𒆜𒇽𒄿 dayyān-šumim 𒆜𒇽 dayyān-qitnim 𒈠𒌋𒁀 dayyān-nindabîm 𒁹𒍪𒇲 dayyān-ṣindim
    𒌝𒄀𒂍 dayyān-eq 𒇲𒈧𒂗 dayyān-eqv 𒈧𒊕𒀭 dayyān-šalmim 𒁀𒄀𒂍)
  (begin
    (define-name-aliases
      (<?                               ṣeḫēr-dayyānim?              𒊕𒌋𒁹)
      (<=?                              ṣeḫēr-mitḫār-dayyānim?       𒄿𒈷𒅆)
      (=?                               mitḫār-dayyānim?             𒀀𒋻𒁹)
      (>?                               rabû-dayyānim?               𒄀𒄿)
      (>=?                              rabû-mitḫār-dayyānim?        𒅆𒂍)
      (comparator?                      dayyānum?                    𒁀𒈠𒀸)
      (comparator-ordered?              dayyānum-šutēšurum?          𒂍𒅁)
      (comparator-hashable?             dayyānum-kunukkum?           𒁀𒈧𒉡)
      (make-comparator                  banû-dayyānim                𒄿𒊕)
      (comparator-type-test-predicate   dayyān-gattim                𒈧𒌝)
      (comparator-equality-predicate    dayyān-mitḫārim              𒆜𒆠)
      (comparator-ordering-predicate    dayyān-šutēšurim             𒁀𒊻𒃲)
      (comparator-hash-function         epšet-dayyānim               𒀸𒁹𒀸)
      (comparator-test-type             parās-gattim                 𒍪𒈷𒈧)
      (comparator-check-type            naṣār-gattim                 𒋻𒆜)
      (comparator-hash                  kunuk-dayyānim               𒈠𒉡𒇲)
      (comparator-register-default!     šakān-dayyān-kayyamānim      𒈷𒄿𒈷)
      (default-comparator               dayyān-kayyamānum            𒊕𒅁𒈧)
      (make-default-comparator          banû-dayyān-kayyamānim       𒆠𒉡)
      (boolean-comparator               dayyān-kēnim                 𒁀𒀸𒀀)
      (real-comparator                  dayyān-ṣīrim                 𒂍𒄿𒌑)
      (number-comparator                dayyān-nikkassim             𒌋𒄿𒂗)
      (char-comparator                  dayyān-ṣibtim                𒌋𒌝𒅁)
      (char-ci-comparator               dayyān-ṣibti-mithāriš        𒄀𒀭𒁀)
      (string-comparator                dayyān-ṭuppim                𒊕𒅆𒌝)
      (string-ci-comparator             dayyān-ṭuppi-mithāriš        𒆜𒇽𒄿)
      (symbol-comparator                dayyān-šumim                 𒆜𒇽)
      (pair-comparator                  dayyān-qitnim                𒈠𒌋𒁀)
      (list-comparator                  dayyān-nindabîm              𒁹𒍪𒇲)
      (vector-comparator                dayyān-ṣindim                𒌝𒄀𒂍)
      (eq-comparator                    dayyān-eq                    𒇲𒈧𒂗)
      (eqv-comparator                   dayyān-eqv                   𒈧𒊕𒀭)
      (equal-comparator                 dayyān-šalmim                𒁀𒄀𒂍))))
