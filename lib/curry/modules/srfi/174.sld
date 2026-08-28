(define-library (srfi 174)
  (import (srfi s174 posix-timespecs))
  (import (curry private lang-aliases))
  (export
    timespec timespec? timespec-seconds timespec-nanoseconds
    inexact->timespec timespec->inexact timespec=? timespec<? timespec-hash
    ;; Akkadian synonyms -- riqqum: "raw, bare" (reused from the raw-byte
    ;; sense already used for u8-ready?), for the low-level seconds+
    ;; nanoseconds pair as opposed to a full calendar adannum.
    adan-riqqim 𒄀𒌝 adan-riqqim? 𒀭𒄿𒆠 adan-riqqi-kabtim 𒇽𒀭𒅆 adan-riqqi-ṣiḫrim
    𒌑𒄀 lā-kinattu-ana-adan-riqqim 𒈷𒂗 adan-riqqum-ana-lā-kinattim 𒅁𒈷𒂍
    mitḫār-adan-riqqim? 𒀸𒄀 ṣeḫēr-adan-riqqim? 𒆜𒀸𒋻 kunukku-adan-riqqim 𒅁𒅆)
  (begin
    (define-name-aliases
      (timespec                 adan-riqqim                  𒄀𒌝)
      (timespec?                adan-riqqim?                 𒀭𒄿𒆠)
      (timespec-seconds         adan-riqqi-kabtim            𒇽𒀭𒅆)
      (timespec-nanoseconds     adan-riqqi-ṣiḫrim            𒌑𒄀)
      (inexact->timespec        lā-kinattu-ana-adan-riqqim   𒈷𒂗)
      (timespec->inexact        adan-riqqum-ana-lā-kinattim  𒅁𒈷𒂍)
      (timespec=?               mitḫār-adan-riqqim?          𒀸𒄀)
      (timespec<?               ṣeḫēr-adan-riqqim?           𒆜𒀸𒋻)
      (timespec-hash            kunukku-adan-riqqim          𒅁𒅆))))
