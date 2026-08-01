;;
;; 𒀭 ṭuppi minât warḫim u attalî
;; The Tablet of the Reckoning of the Month and of the Eclipse
;; Version: 1.0
;;
;; (curry babylonian-astronomy) exercised entirely in Akkadian/cuneiform.
;; Only three of that module's procedures have Akkadian aliases (the
;; module's own doc explains why babylonian-zigzag doesn't); the rest of
;; the code below -- define, let, when, display, newline, +, <= -- uses
;; the same special-form/procedure aliases as examples/fibonacci-akkadian.scm.
;;
;; 𒌑𒀭:  warḫu   ("month")           — synodic-month-length
;; 𒀭:    attalû  ("eclipse")         — babylonian-next-eclipse-window
;; 𒁹𒌑:  šumu-ša-warḫi ("month-name") — babylonian-month-name
;;

(import (curry babylonian-astronomy))

;;; warḫu ittīšu ("the month, its length") — the System B synodic month
(𒅆 "warhu (synodic month, days, exact): ")   ;; display  label
(𒅆 (𒌑𒀭))                                    ;; display  (synodic-month-length)
(𒁹𒁹𒁹)                                        ;; newline

(𒅆 "warhu (cuneiform): ")
(𒅆 (number->string (𒌑𒀭) 'cuneiform))         ;; display  as cuneiform sexagesimal
(𒁹𒁹𒁹)

;;; šumāt warḫāti ("the names of the months") — Nisannu through Addaru
(𒅁 alāku ((mīnu 1))                          ;; let  (named loop), mīnu = "the month-number"
  (𒌑 (𒉡𒃲𒁹 mīnu 12)                          ;; when (<= mīnu 12)
    (𒅆 mīnu)                                  ;;   display  the number
    (𒅆 ". ")                                  ;;   display  separator
    (𒅆 (𒁹𒌑 mīnu))                            ;;   display  (šumu-ša-warḫi mīnu)
    (𒁹𒁹𒁹)                                    ;;   newline
    (alāku (𒋻𒁹 mīnu 1))))                    ;;   (+ mīnu 1) — next month

;;; attalû arkû ("the next eclipse") — one Saros after a known day number.
;;; 2451545 is J2000.0 (2000-01-01 12:00 UT) as a Julian Day Number --
;;; illustrative, not a transcription of any specific historical eclipse.
(𒅆 "attalu arku (one Saros after JDN 2451545): ")
(𒅆 (𒀭 2451545))                              ;; display  (babylonian-next-eclipse-window ...)
(𒁹𒁹𒁹)
