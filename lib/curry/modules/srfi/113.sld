(define-library (srfi 113)
  (import (srfi s113 sets-and-bags))
  (import (curry private lang-aliases))
  (export
    set set? set-contains? set-empty? set-disjoint? set-member set-adjoin
    set-adjoin! set-delete set-delete! set-delete-all set-delete-all!
    set-size set-find set-count set-any? set-every? set-map set-for-each
    set-fold set-filter set-filter! set-remove set-remove! set-partition
    set-copy set->list list->set set=? set<=? set<? set>=? set>? set-union
    set-union! set-intersection set-intersection! set-difference
    set-difference! set-xor set-xor! set-comparator bag bag? bag-contains?
    bag-empty? bag-size bag-unique-size bag-element-count bag-adjoin
    bag-adjoin! bag-delete bag-delete! bag-for-each bag-fold bag-map
    bag-filter bag-any? bag-every? bag-count bag-copy bag->list list->bag
    bag->alist alist->bag bag-union bag-union! bag-intersection bag-sum
    bag-sum! bag-product bag-product! bag=? bag-comparator
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; kiṣrum: "bundle, knot" for a set; kīsum: "purse, pouch" for a bag
    ;; (holds many of a kind, with counts). aḫītum: "something foreign/
    ;; strange" for XOR (either but not both).
    banû-kiṣrim 𒂍𒊕𒊻 dayyān-kiṣrim 𒄀𒈧𒇽 kiṣrum-īšû? 𒌑𒄀𒋻 maḫār-kiṣrim 𒀭𒅆𒌋
    kiṣrū-parsū? 𒇲𒇲𒈷 nasāḫ-gabbi-kiṣrim 𒌑𒂍 nasāḫ-gabbi-kiṣrim-ina 𒁹𒃲
    zâzu-kiṣrim 𒆠𒅆𒃲 nasāḫ-kiṣrim 𒅆𒁹𒌑 nasāḫ-kiṣrim-ina 𒈧𒅁 puḫur-kiṣrim-ina 𒍪𒀀
    ištēniš-kiṣrim-ina 𒌑𒋻 ḫarāṣ-kiṣrim-ina 𒅁𒂗𒅆 aḫītu-kiṣrim 𒈠𒈠
    aḫītu-kiṣrim-ina 𒍪𒀭𒆜 ṣeḫēr-kiṣrim? 𒆠𒂍 ṣeḫēr-mitḫār-kiṣrim? 𒅁𒍪𒉡
    rabû-kiṣrim? 𒈷𒈷𒌝 rabû-mitḫār-kiṣrim? 𒇲𒀭 banû-kīsim 𒆜𒇽𒀸 kīsum? 𒃲𒊻
    mitḫār-kīsim? 𒊕𒂍𒇽 kīsum-īšû? 𒀀𒄀 kīsum-rīqum? 𒅁𒆠 mīnu-kīsim 𒊕𒈠
    mīnu-ēdûti-kīsim 𒈷𒅁 mīnu-zittim-kīsim 𒈷𒅁𒈧 waṣāb-kīsim 𒍪𒌝 waṣāb-kīsim-ina
    𒀸𒈠𒅁 nasāḫ-kīsim 𒈧𒃲𒈧 nasāḫ-kīsim-ina 𒊕𒄿 ana-kālāma-kīsim 𒊻𒂗𒀭 lapāt-kīsim
    𒊻𒊻𒃲 epšet-kīsim 𒃲𒅆𒉡 ṣêr-kīsim 𒂍𒂍 mimma-kīsim? 𒅁𒌝 gabbu-kīsim? 𒌑𒁀𒁹
    mīnu-kašād-kīsim 𒆜𒀀𒆜 šutur-kīsim 𒀀𒂗 kīsum-ana-nindabîm 𒈧𒆠𒈷
    nindabûm-ana-kīsim 𒇽𒂍𒋻 kīsum-ana-libbim 𒊕𒈧𒌝 libbum-ana-kīsim 𒋻𒈷𒌝
    puḫur-kīsim 𒆜𒋻𒈷 puḫur-kīsim-ina 𒃲𒀸𒅆 ištēniš-kīsim 𒃲𒌝 kamār-kīsim 𒊕𒀀
    kamār-kīsim-ina 𒈷𒊻𒈠 šutakūl-kīsim 𒅁𒊕 šutakūl-kīsim-ina 𒂍𒁹𒃲 dayyān-kīsim
    𒈧𒌑𒂗)
  (begin
    (define-name-aliases
      (set                  banû-kiṣrim              𒂍𒊕𒊻)
      (set-comparator       dayyān-kiṣrim            𒄀𒈧𒇽)
      (set-contains?        kiṣrum-īšû?              𒌑𒄀𒋻)
      (set-member           maḫār-kiṣrim             𒀭𒅆𒌋)
      (set-disjoint?        kiṣrū-parsū?             𒇲𒇲𒈷)
      (set-delete-all       nasāḫ-gabbi-kiṣrim       𒌑𒂍)
      (set-delete-all!      nasāḫ-gabbi-kiṣrim-ina   𒁹𒃲)
      (set-partition        zâzu-kiṣrim              𒆠𒅆𒃲)
      (set-remove           nasāḫ-kiṣrim             𒅆𒁹𒌑)
      (set-remove!          nasāḫ-kiṣrim-ina         𒈧𒅁)
      (set-union!           puḫur-kiṣrim-ina         𒍪𒀀)
      (set-intersection!    ištēniš-kiṣrim-ina       𒌑𒋻)
      (set-difference!      ḫarāṣ-kiṣrim-ina         𒅁𒂗𒅆)
      (set-xor              aḫītu-kiṣrim             𒈠𒈠)
      (set-xor!             aḫītu-kiṣrim-ina         𒍪𒀭𒆜)
      (set<?                ṣeḫēr-kiṣrim?            𒆠𒂍)
      (set<=?               ṣeḫēr-mitḫār-kiṣrim?     𒅁𒍪𒉡)
      (set>?                rabû-kiṣrim?             𒈷𒈷𒌝)
      (set>=?               rabû-mitḫār-kiṣrim?      𒇲𒀭)
      (bag                  banû-kīsim               𒆜𒇽𒀸)
      (bag?                 kīsum?                   𒃲𒊻)
      (bag=?                mitḫār-kīsim?            𒊕𒂍𒇽)
      (bag-contains?        kīsum-īšû?               𒀀𒄀)
      (bag-empty?           kīsum-rīqum?             𒅁𒆠)
      (bag-size             mīnu-kīsim               𒊕𒈠)
      (bag-unique-size      mīnu-ēdûti-kīsim         𒈷𒅁)
      (bag-element-count    mīnu-zittim-kīsim        𒈷𒅁𒈧)
      (bag-adjoin           waṣāb-kīsim              𒍪𒌝)
      (bag-adjoin!          waṣāb-kīsim-ina          𒀸𒈠𒅁)
      (bag-delete           nasāḫ-kīsim              𒈧𒃲𒈧)
      (bag-delete!          nasāḫ-kīsim-ina          𒊕𒄿)
      (bag-for-each         ana-kālāma-kīsim         𒊻𒂗𒀭)
      (bag-fold             lapāt-kīsim              𒊻𒊻𒃲)
      (bag-map              epšet-kīsim              𒃲𒅆𒉡)
      (bag-filter           ṣêr-kīsim                𒂍𒂍)
      (bag-any?             mimma-kīsim?             𒅁𒌝)
      (bag-every?           gabbu-kīsim?             𒌑𒁀𒁹)
      (bag-count            mīnu-kašād-kīsim         𒆜𒀀𒆜)
      (bag-copy             šutur-kīsim              𒀀𒂗)
      (bag->list            kīsum-ana-nindabîm       𒈧𒆠𒈷)
      (list->bag            nindabûm-ana-kīsim       𒇽𒂍𒋻)
      (bag->alist           kīsum-ana-libbim         𒊕𒈧𒌝)
      (alist->bag           libbum-ana-kīsim         𒋻𒈷𒌝)
      (bag-union            puḫur-kīsim              𒆜𒋻𒈷)
      (bag-union!           puḫur-kīsim-ina          𒃲𒀸𒅆)
      (bag-intersection     ištēniš-kīsim            𒃲𒌝)
      (bag-sum              kamār-kīsim              𒊕𒀀)
      (bag-sum!             kamār-kīsim-ina          𒈷𒊻𒈠)
      (bag-product          šutakūl-kīsim            𒅁𒊕)
      (bag-product!         šutakūl-kīsim-ina        𒂍𒁹𒃲)
      (bag-comparator       dayyān-kīsim             𒈧𒌑𒂗))))
