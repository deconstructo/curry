(define-library (srfi 69)
  (import (srfi s69 hash-tables))
  (import (curry private lang-aliases))
  (export
    make-hash-table hash-table? alist->hash-table
    hash-table-equivalence-function hash-table-hash-function hash-table-ref
    hash-table-ref/default hash-table-set! hash-table-delete!
    hash-table-exists? hash-table-update! hash-table-update!/default
    hash-table-size hash-table-keys hash-table-values hash-table-walk
    hash-table-fold hash-table->alist hash-table-copy hash-table-merge! hash
    string-hash string-ci-hash hash-by-identity
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; kunukkum: "seal" -- reused from the crypto hash-digest sense
    ;; (md5/sha1/sha256 already use kunukku) for a hash table.
    ṭuppu-ana-kunukkim 𒅆𒇲 kunukku 𒉡𒉡𒊻 kunuk-gattim 𒁀𒋻𒋻 šutur-kunukkim 𒍪𒊻
    parās-mitḫāri-kunukkim 𒌋𒌋𒄿 lapāt-kunukkim 𒀭𒍪𒆠 epšet-kunukkim 𒁹𒀀
    kaṣār-kunukkim 𒃲𒆜𒄿 maḫār-kunukkim-kayyamānim 𒇲𒂗𒍪 šanî-kunukkim 𒀀𒈷
    šanî-kunukkim-kayyamānim 𒁹𒅆 alāk-kunukkim 𒆠𒌋 kunuk-ṭuppi-mithāriš 𒅆𒇽𒁹
    kunuk-šaṭārim 𒍪𒀭)
  (begin
    (define-name-aliases
      (alist->hash-table            ṭuppu-ana-kunukkim           𒅆𒇲)
      (hash                         kunukku                      𒉡𒉡𒊻)
      (hash-by-identity             kunuk-gattim                 𒁀𒋻𒋻)
      (hash-table-copy              šutur-kunukkim               𒍪𒊻)
      (hash-table-equivalence-function parās-mitḫāri-kunukkim       𒌋𒌋𒄿)
      (hash-table-fold              lapāt-kunukkim               𒀭𒍪𒆠)
      (hash-table-hash-function     epšet-kunukkim               𒁹𒀀)
      (hash-table-merge!            kaṣār-kunukkim               𒃲𒆜𒄿)
      (hash-table-ref/default       maḫār-kunukkim-kayyamānim    𒇲𒂗𒍪)
      (hash-table-update!           šanî-kunukkim                𒀀𒈷)
      (hash-table-update!/default   šanî-kunukkim-kayyamānim     𒁹𒅆)
      (hash-table-walk              alāk-kunukkim                𒆠𒌋)
      (string-ci-hash               kunuk-ṭuppi-mithāriš         𒅆𒇽𒁹)
      (string-hash                  kunuk-šaṭārim                𒍪𒀭))
))
