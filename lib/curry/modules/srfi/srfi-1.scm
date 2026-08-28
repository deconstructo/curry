(define-library (srfi srfi-1)
  (import (curry private lang-aliases))
  (import (srfi s1 lists))
  (export
    ; SRFI-1 procedures already in curry's global env
    cons car cdr caaar cadar caar cdar
    list list* make-list length append reverse
    list-tail list-ref last-pair list-copy
    map for-each filter fold-left

    ; Constructors
    xcons cons* list-tabulate circular-list

    ; Predicates
    proper-list? circular-list? dotted-list? null-list? not-pair? list=

    ; Selectors
    first second third fourth fifth sixth seventh eighth ninth tenth
    take drop take-right drop-right take! drop-right!
    split-at split-at! last

    ; Fold, unfold, map
    fold fold-right reduce reduce-right
    pair-fold pair-fold-right
    unfold unfold-right
    map! map-in-order pair-for-each append-map append-map! filter-map flat-map

    ; Filtering / partitioning
    any every remove remove! filter! delete delete! partition partition! count

    ; car+cdr, length+, except-last-pair
    car+cdr length+ except-last-pair except-last-pair!

    ; Searching
    find find-tail take-while drop-while span break list-index
    member assoc

    ; Deleting duplicates
    delete-duplicates delete-duplicates!

    ; Append / concatenate / reverse
    concatenate concatenate! append! append-reverse append-reverse! reverse!

    ; iota (existing)
    iota

    ; Zip / unzip
    zip unzip1 unzip2 unzip3 unzip4 unzip5

    ; Association lists
    alist-cons alist-copy del-assq del-assv del-assoc del-assq! del-assv! del-assoc!

    ; Lists as sets
    lset<= lset= lset-adjoin
    lset-union lset-intersection lset-difference lset-xor
    lset-union! lset-intersection! lset-difference! lset-xor!
    lset-diff+intersection lset-diff+intersection!

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
