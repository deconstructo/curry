(define-library (srfi 132)
  (import (srfi s132 sorting))
  (import (curry private lang-aliases))
  (export
    list-sorted? vector-sorted? list-sort list-stable-sort list-sort!
    list-stable-sort! vector-sort vector-sort! vector-stable-sort
    vector-stable-sort! list-merge list-merge! vector-merge vector-merge!
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; šutēšurum: "well-ordered, regular" (already used for monotonic
    ;; time) is the sort root; kayyamānum: "standing, stable" (already
    ;; used for default-random-source) qualifies the stable variants.
    šutēšur-nindabîm 𒂍𒌋 šutēšur-nindabîm-ina 𒌑𒍪𒌝 šutēšur-nindabi-kayyamānum
    𒃲𒌋 šutēšur-nindabi-kayyamānum-ina 𒄀𒅆 nindabûm-šutēšurum? 𒇽𒈧𒅆
    kaṣār-nindabîm 𒃲𒊻𒁹 kaṣār-nindabîm-ina 𒁹𒉡 šutēšur-ṣindim 𒆜𒈠𒍪
    šutēšur-ṣindim-ina 𒆜𒇲𒀸 šutēšur-ṣindi-kayyamānum 𒋻𒇽
    šutēšur-ṣindi-kayyamānum-ina 𒈷𒂗𒌋 ṣindum-šutēšurum? 𒁀𒄿 kaṣār-ṣindim 𒅁𒌑𒋻
    kaṣār-ṣindim-ina 𒇽𒅁𒈷)
  (begin
    (define-name-aliases
      (list-sort                        šutēšur-nindabîm             𒂍𒌋)
      (list-sort!                       šutēšur-nindabîm-ina         𒌑𒍪𒌝)
      (list-stable-sort                 šutēšur-nindabi-kayyamānum   𒃲𒌋)
      (list-stable-sort!                šutēšur-nindabi-kayyamānum-ina 𒄀𒅆)
      (list-sorted?                     nindabûm-šutēšurum?          𒇽𒈧𒅆)
      (list-merge                       kaṣār-nindabîm               𒃲𒊻𒁹)
      (list-merge!                      kaṣār-nindabîm-ina           𒁹𒉡)
      (vector-sort                      šutēšur-ṣindim               𒆜𒈠𒍪)
      (vector-sort!                     šutēšur-ṣindim-ina           𒆜𒇲𒀸)
      (vector-stable-sort               šutēšur-ṣindi-kayyamānum     𒋻𒇽)
      (vector-stable-sort!              šutēšur-ṣindi-kayyamānum-ina 𒈷𒂗𒌋)
      (vector-sorted?                   ṣindum-šutēšurum?            𒁀𒄿)
      (vector-merge                     kaṣār-ṣindim                 𒅁𒌑𒋻)
      (vector-merge!                    kaṣār-ṣindim-ina             𒇽𒅁𒈷))))
