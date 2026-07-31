(define-library (srfi 1)
  (import (srfi s1 lists))
  (import (curry private lang-aliases))
  (export
    cons car cdr caaar cadar caar cdar list list* make-list length append
    reverse list-tail list-ref last-pair map for-each filter fold-left
    fold-right fold fold-right iota any every remove delete append-map
    filter-map flat-map take drop take-while drop-while count partition first
    second third fourth fifth
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    rakāsum-mala 𒈠𒀀
    qitin-zibbatim 𒋻𒈧𒂗
    lapātum 𒄀𒃲
    minâtu-ištēn 𒄿𒍪
    ibašši-ištēn 𒋻𒅁𒂗
    gabbu 𒈷𒀸
    nasāḫum 𒌑𒈧
    batāqum 𒇲𒄿𒇽
    redû-šutakūlim 𒊻𒄿𒁀
    ṣêr-šutakūlim 𒃲𒅆
    šutakūl-paṭārim 𒈠𒄿𒀀
    leqûm-mala 𒀭𒊻𒄀
    zibbatum-mala 𒄿𒄀
    leqûm-adi 𒄿𒌋
    zibbatum-adi 𒈷𒌑
    mīnu-ṣêrim 𒇲𒄿𒈷
    zâzu-ṣêrim 𒇽𒍪𒇲
    ištēn-nindabîm 𒅁𒂗𒇲
    šina-nindabîm 𒆠𒌑
    šalāš-nindabîm 𒀀𒆜
    erbe-nindabîm 𒀭𒍪
    ḫamiš-nindabîm 𒇲𒅁)
  (begin
    (define-name-aliases
      (list*       rakāsum-mala     𒈠𒀀)
      (last-pair   qitin-zibbatim   𒋻𒈧𒂗)
      (fold        lapātum          𒄀𒃲)
      (iota        minâtu-ištēn     𒄿𒍪)
      (any         ibašši-ištēn     𒋻𒅁𒂗)
      (every       gabbu            𒈷𒀸)
      (remove      nasāḫum          𒌑𒈧)
      (delete      batāqum          𒇲𒄿𒇽)
      (append-map  redû-šutakūlim   𒊻𒄿𒁀)
      (filter-map  ṣêr-šutakūlim    𒃲𒅆)
      (flat-map    šutakūl-paṭārim  𒈠𒄿𒀀)
      (take        leqûm-mala       𒀭𒊻𒄀)
      (drop        zibbatum-mala    𒄿𒄀)
      (take-while  leqûm-adi        𒄿𒌋)
      (drop-while  zibbatum-adi     𒈷𒌑)
      (count       mīnu-ṣêrim       𒇲𒄿𒈷)
      (partition   zâzu-ṣêrim       𒇽𒍪𒇲)
      (first       ištēn-nindabîm   𒅁𒂗𒇲)
      (second      šina-nindabîm    𒆠𒌑)
      (third       šalāš-nindabîm   𒀀𒆜)
      (fourth      erbe-nindabîm    𒀭𒍪)
      (fifth       ḫamiš-nindabîm   𒇲𒅁))))
