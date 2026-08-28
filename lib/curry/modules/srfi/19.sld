(define-library (srfi 19)
  (import (srfi s19 time))
  (import (curry private lang-aliases))
  (export
    time-duration time-monotonic time-process time-tai time-thread time-utc
    make-time time? time-type time-nanosecond time-second set-time-type!
    set-time-nanosecond! set-time-second! copy-time current-time
    time-resolution time<=? time<? time=? time>=? time>? time-difference
    time-difference! add-duration add-duration! subtract-duration
    subtract-duration! make-date date? date-nanosecond date-second
    date-minute date-hour date-day date-month date-year date-zone-offset
    date-year-day date-week-day date-week-number current-date
    current-julian-day current-modified-julian-day date->julian-day
    date->modified-julian-day date->time-monotonic date->time-tai
    date->time-utc julian-day->date julian-day->time-monotonic
    julian-day->time-tai julian-day->time-utc modified-julian-day->date
    modified-julian-day->time-monotonic modified-julian-day->time-tai
    modified-julian-day->time-utc time-monotonic->date
    time-monotonic->julian-day time-monotonic->modified-julian-day
    time-monotonic->time-tai time-monotonic->time-tai!
    time-monotonic->time-utc time-monotonic->time-utc! time-tai->date
    time-tai->julian-day time-tai->modified-julian-day
    time-tai->time-monotonic time-tai->time-monotonic! time-tai->time-utc
    time-tai->time-utc! time-utc->date time-utc->julian-day
    time-utc->modified-julian-day time-utc->time-monotonic
    time-utc->time-monotonic! time-utc->time-tai time-utc->time-tai!
    date->string string->date
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; adannum: "appointed time, moment" (already used for MCP's TTL
    ;; field) is the time-object root; ūmum: "day" for date objects;
    ;; kīnum: "fixed, true, standard" for UTC; dannum: "strong, mighty"
    ;; for TAI (evoking "atomic"); šutēšurum: "well-ordered, regular"
    ;; for monotonic time; ridûtum: "a stretch, an extent" (from redûm,
    ;; "to follow/extend") for a duration.
    ridûtum 𒄿𒁹𒁀 šutēšurum 𒇲𒋻 šipru 𒈷𒊻𒄿 dannum 𒀭𒌝𒉡 ilku 𒌑𒊻𒌑 kīnum 𒉡𒁀𒅁
    epēš-adannim 𒌝𒃲𒄀 adannum? 𒊻𒂗𒆜 zikru-adannim 𒈧𒆜 adan-ṣiḫrim 𒂍𒀀𒀀
    adan-kabtim 𒈠𒀭 šakān-zikri-adannim 𒆜𒀸 šakān-adan-ṣiḫrim 𒄿𒂍
    šakān-adan-kabtim 𒌑𒈧𒀭 šutur-adannim 𒂗𒀀𒀭 adannu-inanna 𒆜𒅆 parās-adannim
    𒈧𒇲 ṣeḫēr-adannim? 𒍪𒉡𒇲 ṣeḫēr-mitḫār-adannim? 𒄿𒁀𒁹 mitḫār-adannim? 𒄿𒂗𒀸
    rabû-mitḫār-adannim? 𒀭𒈠𒅆 rabû-adannim? 𒀭𒁀 nasāḫ-adannim 𒅁𒇲𒁀
    nasāḫ-adannim-ina 𒀭𒊻 waṣāb-ridûtim 𒊻𒇲 waṣāb-ridûtim-ina 𒌑𒉡 ḫarāṣ-ridûtim
    𒅁𒂗 ḫarāṣ-ridûtim-ina 𒀀𒅆𒈠 epēš-ūmim 𒈠𒀭𒊕 ūmum? 𒈠𒍪 ūm-ṣiḫrim 𒁹𒆠𒈠 ūm-kabtim
    𒁀𒅆𒊻 ūm-mislim 𒌋𒀭 ūm-berîm 𒄿𒁹𒉡 ūm-warḫim 𒂗𒋻𒂍 warḫum 𒆜𒆠𒈷 šattum 𒁹𒂍𒀭
    nasāḫ-ašrim 𒃲𒇲𒈠 ūm-šattim 𒌑𒇲𒅁 ūm-ḫamuštim 𒃲𒉡 mīnu-ḫamuštim 𒅆𒄀𒈠
    ūmu-inanna 𒌝𒅆𒇽 minât-ūmī-inanna 𒃲𒅁𒈧 minât-ūmī-eššim-inanna 𒄿𒌑𒍪
    ūmum-ana-minât-ūmī 𒈠𒍪𒌝 ūmum-ana-minât-ūmī-eššim 𒊕𒉡 ūmum-ana-šutēšurim 𒀸𒃲
    ūmum-ana-dannim 𒀀𒁹𒀭 ūmum-ana-kīnim 𒆠𒃲 minât-ūmī-ana-ūmim 𒄀𒉡𒋻
    minât-ūmī-ana-šutēšurim 𒅆𒁹𒌋 minât-ūmī-ana-dannim 𒊕𒃲𒌋 minât-ūmī-ana-kīnim
    𒂗𒊕𒍪 minât-ūmī-eššim-ana-ūmim 𒀀𒂍 minât-ūmī-eššim-ana-šutēšurim 𒂗𒀀𒂍
    minât-ūmī-eššim-ana-dannim 𒄿𒈧 minât-ūmī-eššim-ana-kīnim 𒃲𒈠𒆠
    šutēšurum-ana-ūmim 𒊻𒀭 šutēšurum-ana-minât-ūmī 𒀭𒇲𒌋
    šutēšurum-ana-minât-ūmī-eššim 𒈠𒆜𒆠 šutēšurum-ana-dannim 𒆜𒊻𒆜
    šutēšurum-ana-dannim-ina 𒈠𒄿 šutēšurum-ana-kīnim 𒊻𒌋
    šutēšurum-ana-kīnim-ina 𒊻𒋻 dannum-ana-ūmim 𒊻𒍪𒃲 dannum-ana-minât-ūmī 𒈷𒇽𒈧
    dannum-ana-minât-ūmī-eššim 𒀸𒃲𒊻 dannum-ana-šutēšurim 𒆜𒌋𒈧
    dannum-ana-šutēšurim-ina 𒈷𒌋 dannum-ana-kīnim 𒌑𒈠𒌑 dannum-ana-kīnim-ina
    𒈷𒅆𒈠 kīnum-ana-ūmim 𒄿𒁹𒅆 kīnum-ana-minât-ūmī 𒌋𒂍 kīnum-ana-minât-ūmī-eššim
    𒀸𒅆𒆠 kīnum-ana-šutēšurim 𒀀𒋻 kīnum-ana-šutēšurim-ina 𒅆𒌑 kīnum-ana-dannim
    𒀀𒈧 kīnum-ana-dannim-ina 𒀸𒌋𒍪 ūmum-ana-ṭuppim 𒋻𒄀𒋻 ṭuppum-ana-ūmim 𒈧𒋻𒌋)
  (begin
    (define-name-aliases
      (time-duration                ridûtum                  𒄿𒁹𒁀)
      (time-monotonic               šutēšurum                𒇲𒋻)
      (time-process                 šipru                    𒈷𒊻𒄿)
      (time-tai                     dannum                   𒀭𒌝𒉡)
      (time-thread                  ilku                     𒌑𒊻𒌑)
      (time-utc                     kīnum                    𒉡𒁀𒅁)
      (make-time                    epēš-adannim             𒌝𒃲𒄀)
      (time?                        adannum?                 𒊻𒂗𒆜)
      (time-type                    zikru-adannim            𒈧𒆜)
      (time-nanosecond              adan-ṣiḫrim              𒂍𒀀𒀀)
      (time-second                  adan-kabtim              𒈠𒀭)
      (set-time-type!               šakān-zikri-adannim      𒆜𒀸)
      (set-time-nanosecond!         šakān-adan-ṣiḫrim        𒄿𒂍)
      (set-time-second!             šakān-adan-kabtim        𒌑𒈧𒀭)
      (copy-time                    šutur-adannim            𒂗𒀀𒀭)
      (current-time                 adannu-inanna            𒆜𒅆)
      (time-resolution              parās-adannim            𒈧𒇲)
      (time<?                       ṣeḫēr-adannim?           𒍪𒉡𒇲)
      (time<=?                      ṣeḫēr-mitḫār-adannim?    𒄿𒁀𒁹)
      (time=?                       mitḫār-adannim?          𒄿𒂗𒀸)
      (time>=?                      rabû-mitḫār-adannim?     𒀭𒈠𒅆)
      (time>?                       rabû-adannim?            𒀭𒁀)
      (time-difference              nasāḫ-adannim            𒅁𒇲𒁀)
      (time-difference!             nasāḫ-adannim-ina        𒀭𒊻)
      (add-duration                 waṣāb-ridûtim            𒊻𒇲)
      (add-duration!                waṣāb-ridûtim-ina        𒌑𒉡)
      (subtract-duration            ḫarāṣ-ridûtim            𒅁𒂗)
      (subtract-duration!           ḫarāṣ-ridûtim-ina        𒀀𒅆𒈠)
      (make-date                    epēš-ūmim                𒈠𒀭𒊕)
      (date?                        ūmum?                    𒈠𒍪)
      (date-nanosecond              ūm-ṣiḫrim                𒁹𒆠𒈠)
      (date-second                  ūm-kabtim                𒁀𒅆𒊻)
      (date-minute                  ūm-mislim                𒌋𒀭)
      (date-hour                    ūm-berîm                 𒄿𒁹𒉡)
      (date-day                     ūm-warḫim                𒂗𒋻𒂍)
      (date-month                   warḫum                   𒆜𒆠𒈷)
      (date-year                    šattum                   𒁹𒂍𒀭)
      (date-zone-offset             nasāḫ-ašrim              𒃲𒇲𒈠)
      (date-year-day                ūm-šattim                𒌑𒇲𒅁)
      (date-week-day                ūm-ḫamuštim              𒃲𒉡)
      (date-week-number             mīnu-ḫamuštim            𒅆𒄀𒈠)
      (current-date                 ūmu-inanna               𒌝𒅆𒇽)
      (current-julian-day           minât-ūmī-inanna         𒃲𒅁𒈧)
      (current-modified-julian-day  minât-ūmī-eššim-inanna   𒄿𒌑𒍪)
      (date->julian-day             ūmum-ana-minât-ūmī       𒈠𒍪𒌝)
      (date->modified-julian-day    ūmum-ana-minât-ūmī-eššim 𒊕𒉡)
      (date->time-monotonic         ūmum-ana-šutēšurim       𒀸𒃲)
      (date->time-tai               ūmum-ana-dannim          𒀀𒁹𒀭)
      (date->time-utc               ūmum-ana-kīnim           𒆠𒃲)
      (julian-day->date             minât-ūmī-ana-ūmim       𒄀𒉡𒋻)
      (julian-day->time-monotonic   minât-ūmī-ana-šutēšurim  𒅆𒁹𒌋)
      (julian-day->time-tai         minât-ūmī-ana-dannim     𒊕𒃲𒌋)
      (julian-day->time-utc         minât-ūmī-ana-kīnim      𒂗𒊕𒍪)
      (modified-julian-day->date    minât-ūmī-eššim-ana-ūmim 𒀀𒂍)
      (modified-julian-day->time-monotonic minât-ūmī-eššim-ana-šutēšurim 𒂗𒀀𒂍)
      (modified-julian-day->time-tai minât-ūmī-eššim-ana-dannim 𒄿𒈧)
      (modified-julian-day->time-utc minât-ūmī-eššim-ana-kīnim 𒃲𒈠𒆠)
      (time-monotonic->date         šutēšurum-ana-ūmim       𒊻𒀭)
      (time-monotonic->julian-day   šutēšurum-ana-minât-ūmī  𒀭𒇲𒌋)
      (time-monotonic->modified-julian-day šutēšurum-ana-minât-ūmī-eššim 𒈠𒆜𒆠)
      (time-monotonic->time-tai     šutēšurum-ana-dannim     𒆜𒊻𒆜)
      (time-monotonic->time-tai!    šutēšurum-ana-dannim-ina 𒈠𒄿)
      (time-monotonic->time-utc     šutēšurum-ana-kīnim      𒊻𒌋)
      (time-monotonic->time-utc!    šutēšurum-ana-kīnim-ina  𒊻𒋻)
      (time-tai->date               dannum-ana-ūmim          𒊻𒍪𒃲)
      (time-tai->julian-day         dannum-ana-minât-ūmī     𒈷𒇽𒈧)
      (time-tai->modified-julian-day dannum-ana-minât-ūmī-eššim 𒀸𒃲𒊻)
      (time-tai->time-monotonic     dannum-ana-šutēšurim     𒆜𒌋𒈧)
      (time-tai->time-monotonic!    dannum-ana-šutēšurim-ina 𒈷𒌋)
      (time-tai->time-utc           dannum-ana-kīnim         𒌑𒈠𒌑)
      (time-tai->time-utc!          dannum-ana-kīnim-ina     𒈷𒅆𒈠)
      (time-utc->date               kīnum-ana-ūmim           𒄿𒁹𒅆)
      (time-utc->julian-day         kīnum-ana-minât-ūmī      𒌋𒂍)
      (time-utc->modified-julian-day kīnum-ana-minât-ūmī-eššim 𒀸𒅆𒆠)
      (time-utc->time-monotonic     kīnum-ana-šutēšurim      𒀀𒋻)
      (time-utc->time-monotonic!    kīnum-ana-šutēšurim-ina  𒅆𒌑)
      (time-utc->time-tai           kīnum-ana-dannim         𒀀𒈧)
      (time-utc->time-tai!          kīnum-ana-dannim-ina     𒀸𒌋𒍪)
      (date->string                 ūmum-ana-ṭuppim          𒋻𒄀𒋻)
      (string->date                 ṭuppum-ana-ūmim          𒈧𒋻𒌋))))
