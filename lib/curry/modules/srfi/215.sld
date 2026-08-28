(define-library (srfi 215)
  (import (srfi s215 log))
  (import (curry private lang-aliases))
  (export
    send-log current-log-fields current-log-callback EMERGENCY ALERT CRITICAL
    ERROR WARNING NOTICE INFO DEBUG
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    ;; egertum: "letter, dispatch" is the log-message root. The severity
    ;; constants get distinct genuine words rather than one pattern,
    ;; the same treatment already given to the R7RS condition levels.
    waṣī-egertim 𒀸𒇽𒀭 simāt-egertim-inanna 𒄿𒂍𒌝 ṭēm-egertim-inanna 𒌋𒁀 dannatum
    𒉡𒈠 nabrûm 𒋻𒈠𒌝 kabtum 𒁀𒄀 lemuttum 𒆜𒂍𒀭 tazkītum 𒈧𒂍𒅆 tašpurtum 𒁀𒁀𒄿 ṭēmum 𒈧𒂍
    šitassûm 𒄿𒁀𒌋)
  (begin
    (define-name-aliases
      (send-log                         waṣī-egertim                 𒀸𒇽𒀭)
      (current-log-fields               simāt-egertim-inanna         𒄿𒂍𒌝)
      (current-log-callback             ṭēm-egertim-inanna           𒌋𒁀)
      (EMERGENCY                        dannatum                     𒉡𒈠)
      (ALERT                            nabrûm                       𒋻𒈠𒌝)
      (CRITICAL                         kabtum                       𒁀𒄀)
      (ERROR                            lemuttum                     𒆜𒂍𒀭)
      (WARNING                          tazkītum                     𒈧𒂍𒅆)
      (NOTICE                           tašpurtum                    𒁀𒁀𒄿)
      (INFO                             ṭēmum                        𒈧𒂍)
      (DEBUG                            šitassûm                     𒄿𒁀𒌋))
))
