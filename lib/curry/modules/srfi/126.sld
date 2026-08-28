(define-library (srfi 126)
  (import (srfi s126 hashtables))
  (import (curry private lang-aliases))
  (export
    make-eq-hashtable make-eqv-hashtable make-hashtable hashtable?
    hashtable-set! hashtable-ref hashtable-delete! hashtable-contains?
    hashtable-update! hashtable-copy hashtable-clear! hashtable-size
    hashtable-keys hashtable-values hashtable-entries hashtable->alist
    hashtable-walk hashtable-mutable? hashtable-hash-function
    hashtable-equivalence-function equal-hash string-hash symbol-hash
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; kunuk-ṭuppim: "seal-tablet" -- SRFI 126's R6RS-style "hashtable"
    ;; (no dash) gets a variant of the SRFI 69/125 kunukkum root so the
    ;; two never collide if both are imported into the same environment.
    kunuk-mitḫārim 𒈧𒇽 kunuk-ṭuppi-ana-libbim 𒁀𒆠 ullul-kunuk-ṭuppim 𒇲𒇲𒌑
    kunuk-ṭuppum-īšû? 𒊕𒌝𒈧 šutur-kunuk-ṭuppim 𒆜𒉡 nasāḫ-kunuk-ṭuppim 𒃲𒊻𒄿
    libbū-kunuk-ṭuppim 𒀀𒂍𒈠 parās-mitḫāri-kunuk-ṭuppim 𒇲𒍪 epšet-kunuk-ṭuppim
    𒁹𒀸 rēšū-kunuk-ṭuppim 𒅁𒆜𒉡 kunuk-ṭuppum-šunnâm? 𒍪𒁀𒂗 maḫār-kunuk-ṭuppim 𒆜𒈷𒈧
    šakān-kunuk-ṭuppim 𒂍𒂍𒌑 mīnu-kunuk-ṭuppim 𒉡𒀀𒅁 šanî-kunuk-ṭuppim 𒁹𒈧𒊻
    šīmū-kunuk-ṭuppim 𒈷𒇲 alāk-kunuk-ṭuppim 𒄀𒅁 kunuk-ṭuppum? 𒃲𒌑𒇽
    banû-kunuk-ṭuppi-eq 𒀀𒆠𒁹 banû-kunuk-ṭuppi-eqv 𒈠𒁀 banû-kunuk-ṭuppim 𒊕𒄀𒀀
    kunuk-ṭuppi-šaṭārim 𒂗𒋻 kunuk-šumim 𒆠𒆜𒇽)
  (begin
    (define-name-aliases
      (equal-hash                   kunuk-mitḫārim               𒈧𒇽)
      (hashtable->alist             kunuk-ṭuppi-ana-libbim       𒁀𒆠)
      (hashtable-clear!             ullul-kunuk-ṭuppim           𒇲𒇲𒌑)
      (hashtable-contains?          kunuk-ṭuppum-īšû?            𒊕𒌝𒈧)
      (hashtable-copy               šutur-kunuk-ṭuppim           𒆜𒉡)
      (hashtable-delete!            nasāḫ-kunuk-ṭuppim           𒃲𒊻𒄿)
      (hashtable-entries            libbū-kunuk-ṭuppim           𒀀𒂍𒈠)
      (hashtable-equivalence-function parās-mitḫāri-kunuk-ṭuppim   𒇲𒍪)
      (hashtable-hash-function      epšet-kunuk-ṭuppim           𒁹𒀸)
      (hashtable-keys               rēšū-kunuk-ṭuppim            𒅁𒆜𒉡)
      (hashtable-mutable?           kunuk-ṭuppum-šunnâm?         𒍪𒁀𒂗)
      (hashtable-ref                maḫār-kunuk-ṭuppim           𒆜𒈷𒈧)
      (hashtable-set!               šakān-kunuk-ṭuppim           𒂍𒂍𒌑)
      (hashtable-size               mīnu-kunuk-ṭuppim            𒉡𒀀𒅁)
      (hashtable-update!            šanî-kunuk-ṭuppim            𒁹𒈧𒊻)
      (hashtable-values             šīmū-kunuk-ṭuppim            𒈷𒇲)
      (hashtable-walk               alāk-kunuk-ṭuppim            𒄀𒅁)
      (hashtable?                   kunuk-ṭuppum?                𒃲𒌑𒇽)
      (make-eq-hashtable            banû-kunuk-ṭuppi-eq          𒀀𒆠𒁹)
      (make-eqv-hashtable           banû-kunuk-ṭuppi-eqv         𒈠𒁀)
      (make-hashtable               banû-kunuk-ṭuppim            𒊕𒄀𒀀)
      (string-hash                  kunuk-ṭuppi-šaṭārim          𒂗𒋻)
      (symbol-hash                  kunuk-šumim                  𒆠𒆜𒇽))
))
