(define-library (srfi 125)
  (import (srfi s125 hash-tables))
  (import (curry private lang-aliases))
  (export
    make-hash-table hash-table hash-table-unfold hash-table?
    hash-table-comparator hash-table-contains? hash-table-exists?
    hash-table-empty? hash-table-size hash-table-count hash-table-ref
    hash-table-ref/default hash-table-set! hash-table-delete!
    hash-table-intern! hash-table-update! hash-table-update!/default
    hash-table-clear! hash-table-copy hash-table-empty-copy hash-table-keys
    hash-table-values hash-table-entries hash-table->alist alist->hash-table
    hash-table-walk hash-table-for-each hash-table-map->list hash-table-fold
    hash-table-count-matching hash-table-map! hash-table-prune!
    hash-table-union! hash-table-intersection! hash-table-difference!
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; kunukkum: "seal" (same root as SRFI 69, above) -- the words are
    ;; shared for shared english names, distinct where the APIs diverge.
    ;; dayyānum: "judge" -- reused as the comparator root (see SRFI 128).
    ṭuppu-ana-kunukkim 𒅆𒇲 banû-kunukkim-mala 𒊻𒀭𒇲 ullul-kunukkim 𒈠𒊕𒀸
    dayyān-kunukkim 𒈠𒊕 kunukkum-īšû? 𒇽𒇲𒅆 šutur-kunukkim 𒍪𒊻 mīnu-kunukkim 𒄀𒈧𒅆
    mīnu-kašād-kunukkim 𒌑𒄿𒇲 ḫarāṣ-kunukkim 𒇽𒌋 šutur-rīqi-kunukkim 𒅁𒀭𒍪
    kunukkum-rīqum? 𒀀𒅁𒆜 libbū-kunukkim 𒌋𒈠 lapāt-kunukkim 𒀭𒍪𒆠
    ana-kālāma-kunukkim 𒄿𒂗 kanāk-kunukkim 𒊕𒇽 ištēniš-kunukkim 𒈧𒅆𒆠
    šutakūl-kunukkim-ana-nindabîm 𒌑𒈠𒌝 šutakūl-kunukkim 𒂗𒈠𒂍 nakās-kunukkim
    𒌑𒂍𒌑 maḫār-kunukkim-kayyamānim 𒇲𒂗𒍪 paṭār-kunukkim 𒌋𒌝 puḫur-kunukkim 𒁀𒅆
    šanî-kunukkim 𒀀𒈷 šanî-kunukkim-kayyamānim 𒁹𒅆 alāk-kunukkim 𒆠𒌋)
  (begin
    (define-name-aliases
      (alist->hash-table            ṭuppu-ana-kunukkim           𒅆𒇲)
      (hash-table                   banû-kunukkim-mala           𒊻𒀭𒇲)
      (hash-table-clear!            ullul-kunukkim               𒈠𒊕𒀸)
      (hash-table-comparator        dayyān-kunukkim              𒈠𒊕)
      (hash-table-contains?         kunukkum-īšû?                𒇽𒇲𒅆)
      (hash-table-copy              šutur-kunukkim               𒍪𒊻)
      (hash-table-count             mīnu-kunukkim                𒄀𒈧𒅆)
      (hash-table-count-matching    mīnu-kašād-kunukkim          𒌑𒄿𒇲)
      (hash-table-difference!       ḫarāṣ-kunukkim               𒇽𒌋)
      (hash-table-empty-copy        šutur-rīqi-kunukkim          𒅁𒀭𒍪)
      (hash-table-empty?            kunukkum-rīqum?              𒀀𒅁𒆜)
      (hash-table-entries           libbū-kunukkim               𒌋𒈠)
      (hash-table-fold              lapāt-kunukkim               𒀭𒍪𒆠)
      (hash-table-for-each          ana-kālāma-kunukkim          𒄿𒂗)
      (hash-table-intern!           kanāk-kunukkim               𒊕𒇽)
      (hash-table-intersection!     ištēniš-kunukkim             𒈧𒅆𒆠)
      (hash-table-map->list         šutakūl-kunukkim-ana-nindabîm 𒌑𒈠𒌝)
      (hash-table-map!              šutakūl-kunukkim             𒂗𒈠𒂍)
      (hash-table-prune!            nakās-kunukkim               𒌑𒂍𒌑)
      (hash-table-ref/default       maḫār-kunukkim-kayyamānim    𒇲𒂗𒍪)
      (hash-table-unfold            paṭār-kunukkim               𒌋𒌝)
      (hash-table-union!            puḫur-kunukkim               𒁀𒅆)
      (hash-table-update!           šanî-kunukkim                𒀀𒈷)
      (hash-table-update!/default   šanî-kunukkim-kayyamānim     𒁹𒅆)
      (hash-table-walk              alāk-kunukkim                𒆠𒌋))
))
