;;; (curry babylonian-astronomy) — Babylonian mathematical-astronomy toolkit
;;;
;;; A handful of genuinely-attested Babylonian astronomical techniques
;;; (Neugebauer, *A History of Ancient Mathematical Astronomy*, the "ACT"
;;; corpus), not a full reimplementation of the MUL.APIN star-almanac
;;; system (a much larger star-rising/setting-times project on its own):
;;;
;;;   - the "zigzag function": the general linear ramp-and-reflect
;;;     technique System A astronomy used for periodic quantities (length
;;;     of daylight, lunar/solar velocity, ...);
;;;   - the System B mean synodic month constant, 29;31,50,8,20 days,
;;;     entered here as an actual sexagesimal literal;
;;;   - the Saros period (223 synodic months) used in Goal-Year-text
;;;     eclipse prediction, as period-relation arithmetic on that constant;
;;;   - the twelve Babylonian civil calendar month names.
;;;
;;; Illustrative note: the daylight-length example below uses commonly
;;; cited System A parameters (max/min in UŠ, a 12-month period) as a
;;; worked illustration of the zigzag technique, not a transcription of
;;; any specific tablet -- exact parameters vary across the different
;;; System A/B schemes and periods attested in the tablets.

(define-library (curry babylonian-astronomy)
  (import (scheme base))
  (import (curry private lang-aliases))
  (export
    babylonian-zigzag
    system-a-daylight-length
    babylonian-synodic-month
    synodic-month-length
    babylonian-saros-months
    saros-length-days
    babylonian-next-eclipse-window
    babylonian-month-names
    babylonian-month-name
    ;; Akkadian synonyms -- lib/curry/modules/curry/private/lang-aliases.scm
    warḫu 𒌑𒀭
    attalû 𒀭
    šumu-ša-warḫi 𒁹𒌑)
  (begin

;;; ---- The zigzag function ----

;;; (babylonian-zigzag max min half-period n)
;;;
;;; The core System A technique: a value that rises linearly from `min` to
;;; `max` over `half-period` steps, then falls linearly back over the next
;;; `half-period` steps, repeating with full period (2 * half-period). `n`
;;; is the step index (any integer; negative and large `n` wrap correctly).
;;; Returns min at n ≡ 0, max at n ≡ half-period (mod the full period).
(define (babylonian-zigzag max min half-period n)
  (let* ((d     (/ (- max min) half-period))
         (cycle (* 2 half-period))
         (phase (modulo n cycle)))
    (if (<= phase half-period)
        (+ min (* phase d))
        (- max (* (- phase half-period) d)))))

;;; (system-a-daylight-length month)
;;;
;;; A worked example of babylonian-zigzag: the length of daylight at
;;; Babylon's latitude, in UŠ (1 UŠ = 4 minutes; 360 UŠ = 1 full day),
;;; using commonly cited System A parameters -- max 3,36 = 216 UŠ (14h24m,
;;; summer solstice) and min 2,24 = 144 UŠ (9h36m, winter solstice), over a
;;; 6-month rise and 6-month fall (12-month full period). `month` counts
;;; from the winter solstice (month 0 = minimum).
(define (system-a-daylight-length month)
  (babylonian-zigzag 216 144 6 month))

;;; ---- Synodic month and the Saros eclipse cycle ----

;;; The System B mean synodic month: 29;31,50,8,20 days (≈ 29.530594 days),
;;; a real sexagesimal constant attested in the ACT ephemerides -- entered
;;; here as an actual Neugebauer-notation literal rather than a decimal
;;; approximation, matching the modern value (≈ 29.530589 days) to five
;;; sexagesimal places.
(define babylonian-synodic-month
  (string->number "29;31,50,8,20" 'neugebauer))

;;; (synodic-month-length) — babylonian-synodic-month as a procedure, for
;;; symmetry with the other exports (and so it has an Akkadian alias).
(define (synodic-month-length) babylonian-synodic-month)

;;; The Saros: 223 synodic months (≈ 6585.32 days, "18 years 11⅓ days"),
;;; the period relation Babylonian Goal-Year texts used to predict which
;;; months could have an eclipse, by adding one Saros to a known eclipse.
(define babylonian-saros-months 223)

(define (saros-length-days)
  (* babylonian-saros-months babylonian-synodic-month))

;;; (babylonian-next-eclipse-window known-eclipse-day)
;;;
;;; Given the day number (any consistent day-count epoch, e.g. a Julian Day
;;; Number) of a known historical eclipse, returns the day number of the
;;; next occurrence of the same Saros series -- the core Goal-Year-text
;;; eclipse-prediction technique: eclipses recur every 223 synodic months
;;; because that period is also very close to a whole number of anomalistic
;;; and draconic months, so the Moon returns to a similar position relative
;;; to its node and to perigee.
(define (babylonian-next-eclipse-window known-eclipse-day)
  (+ known-eclipse-day (saros-length-days)))

;;; ---- Babylonian civil calendar ----

;;; The twelve Babylonian civil calendar month names (a lunisolar
;;; calendar; a 13th intercalary month, Addaru II, was inserted in leap
;;; years -- not represented here).
(define babylonian-month-names
  #("Nisannu" "Ayaru" "Simanu" "Duʾuzu" "Abu" "Ulūlu"
    "Tašrītu" "Araḫsamnu" "Kislīmu" "Ṭebētu" "Šabāṭu" "Addaru"))

;;; (babylonian-month-name n) — the name of the nth month (1-12).
(define (babylonian-month-name n)
  (vector-ref babylonian-month-names (- n 1)))

;;; ---- Akkadian aliases ----
;;; See lib/curry/modules/curry/private/lang-aliases.scm for the pattern.
;;; Only the most central names get an alias here -- e.g. "zigzag function"
;;; is Neugebauer's modern analytical term for the technique, not an
;;; attested ancient name, so babylonian-zigzag is left English-only rather
;;; than forcing a coined ancient-sounding word onto a modern concept.

(define-name-aliases
  ;; warḫu — "month" (the synodic month's length)
  (synodic-month-length       warḫu           𒌑𒀭)
  ;; attalû — "eclipse" (attested: e.g. "attalû ša Sîn", "eclipse of the Moon")
  (babylonian-next-eclipse-window  attalû     𒀭)
  ;; šumu-ša-warḫi — "name of the month"
  (babylonian-month-name       šumu-ša-warḫi  𒁹𒌑))

  )) ;; end begin, define-library
