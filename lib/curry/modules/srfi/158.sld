(define-library (srfi 158)
  (import (srfi s158 generators-and-accumulators))
  (import (curry private lang-aliases))
  (export
    generator make-coroutine-generator make-for-each-generator
    list->generator vector->generator string->generator make-range-generator
    make-iota-generator circular-generator generator->list generator->vector
    generator->string gtake gdrop gappend gcons* gmap gfilter gremove gzip
    gflatten generator-map->list generator-fold generator-for-each
    generator-each generator-count generator-any generator-every
    generator-find generator-length make-accumulator count-accumulator
    list-accumulator reverse-list-accumulator vector-accumulator
    sum-accumulator product-accumulator
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; nāgirum: "herald, crier" -- calls forth values one at a time.
    ;; kamārum: "to gather, heap up" (already used for divisor-sum) is
    ;; the accumulator root.
    banû-nāgirim 𒌋𒂍𒌋 banû-nāgir-tārim 𒅆𒇽 banû-nāgir-kālāma 𒌑𒂗
    banû-nāgir-minâtim 𒉡𒂗𒉡 banû-nāgir-kibsim 𒆜𒂗 nāgir-saḫrum 𒈠𒀸𒊕
    nindabûm-ana-nāgirim 𒆠𒆜 ṣindānum-ana-nāgirim 𒉡𒄿𒂗 ṭuppum-ana-nāgirim 𒊕𒈠𒂗
    nāgirum-ana-nindabîm 𒂍𒀀 nāgirum-ana-ṣindim 𒀀𒇽 nāgirum-ana-ṭuppim 𒈠𒆜𒌑
    nāgir-mala 𒌋𒉡𒉡 nāgir-zibbatim 𒈧𒀸𒍪 nāgir-redîm 𒁀𒆜 nāgir-rakāsim 𒆜𒄿𒁀
    nāgir-šutakūlim 𒀭𒅆𒁹 nāgir-ṣêrim 𒁹𒌋𒀸 nāgir-nasāḫim 𒆜𒁀 nāgir-kaṣārim 𒀀𒌋𒌝
    nāgir-paṭārim 𒈷𒀀𒁹 nāgir-šutakūl-nindabîm 𒁀𒆜𒋻 nāgir-lapātim 𒍪𒄿
    nāgir-ana-kālāma 𒄿𒀸 nāgir-kālāma-ēdiš 𒊕𒁀 nāgir-mīnim 𒌋𒃲 nāgir-mimma 𒈧𒍪
    nāgir-gabbi 𒀸𒆜𒀸 nāgir-bêrim 𒆜𒊻𒆠 mīnu-nāgirim 𒅁𒊻 banû-kamārim 𒈧𒄀
    kamār-mīnim 𒋻𒊻𒃲 kamār-nindabi-mala 𒈧𒌑 kamār-nindabi-turrum 𒂍𒇲𒁀
    kamār-ṣindim 𒋻𒈧 kamār-matāḫim 𒀀𒅆𒌑 kamār-šutakūlim 𒍪𒀸)
  (begin
    (define-name-aliases
      (generator                        banû-nāgirim                 𒌋𒂍𒌋)
      (make-coroutine-generator         banû-nāgir-tārim             𒅆𒇽)
      (make-for-each-generator          banû-nāgir-kālāma            𒌑𒂗)
      (make-iota-generator              banû-nāgir-minâtim           𒉡𒂗𒉡)
      (make-range-generator             banû-nāgir-kibsim            𒆜𒂗)
      (circular-generator               nāgir-saḫrum                 𒈠𒀸𒊕)
      (list->generator                  nindabûm-ana-nāgirim         𒆠𒆜)
      (vector->generator                ṣindānum-ana-nāgirim         𒉡𒄿𒂗)
      (string->generator                ṭuppum-ana-nāgirim           𒊕𒈠𒂗)
      (generator->list                  nāgirum-ana-nindabîm         𒂍𒀀)
      (generator->vector                nāgirum-ana-ṣindim           𒀀𒇽)
      (generator->string                nāgirum-ana-ṭuppim           𒈠𒆜𒌑)
      (gtake                            nāgir-mala                   𒌋𒉡𒉡)
      (gdrop                            nāgir-zibbatim               𒈧𒀸𒍪)
      (gappend                          nāgir-redîm                  𒁀𒆜)
      (gcons*                           nāgir-rakāsim                𒆜𒄿𒁀)
      (gmap                             nāgir-šutakūlim              𒀭𒅆𒁹)
      (gfilter                          nāgir-ṣêrim                  𒁹𒌋𒀸)
      (gremove                          nāgir-nasāḫim                𒆜𒁀)
      (gzip                             nāgir-kaṣārim                𒀀𒌋𒌝)
      (gflatten                         nāgir-paṭārim                𒈷𒀀𒁹)
      (generator-map->list              nāgir-šutakūl-nindabîm       𒁀𒆜𒋻)
      (generator-fold                   nāgir-lapātim                𒍪𒄿)
      (generator-for-each               nāgir-ana-kālāma             𒄿𒀸)
      (generator-each                   nāgir-kālāma-ēdiš            𒊕𒁀)
      (generator-count                  nāgir-mīnim                  𒌋𒃲)
      (generator-any                    nāgir-mimma                  𒈧𒍪)
      (generator-every                  nāgir-gabbi                  𒀸𒆜𒀸)
      (generator-find                   nāgir-bêrim                  𒆜𒊻𒆠)
      (generator-length                 mīnu-nāgirim                 𒅁𒊻)
      (make-accumulator                 banû-kamārim                 𒈧𒄀)
      (count-accumulator                kamār-mīnim                  𒋻𒊻𒃲)
      (list-accumulator                 kamār-nindabi-mala           𒈧𒌑)
      (reverse-list-accumulator         kamār-nindabi-turrum         𒂍𒇲𒁀)
      (vector-accumulator               kamār-ṣindim                 𒋻𒈧)
      (sum-accumulator                  kamār-matāḫim                𒀀𒅆𒌑)
      (product-accumulator              kamār-šutakūlim              𒍪𒀸))
))
