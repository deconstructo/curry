;;; (srfi s19 time) — SRFI-19: Time Data Types and Procedures.
;;;
;;; Scope notes (see docs/reference/module-time-srfi.md for the full story):
;;;
;;; - TAI (International Atomic Time) requires a leap-second table that
;;;   needs periodic maintenance to stay correct — rather than silently
;;;   giving a plausible-but-wrong answer (accurate only for the current
;;;   leap-second era, wrong for any historical date and wrong again after
;;;   the next leap second is added), every time-tai-producing or
;;;   time-tai-consuming procedure here raises a clear "not supported"
;;;   error instead.
;;; - time-monotonic is numerically identical to time-utc in this
;;;   implementation (curry's underlying monotonic clock has no fixed
;;;   epoch to convert to/from calendar time meaningfully, and the SRFI
;;;   explicitly permits treating the two as interchangeable when a
;;;   separately-epoched monotonic clock isn't distinguished).
;;; - time-process/time-thread current-time queries return the same value
;;;   as time-monotonic — curry has no separate process/thread CPU time
;;;   clock exposed at the Scheme level, only wall/monotonic time.
;;; - current-date has no access to the system's local timezone offset
;;;   (no localtime()/tm_gmtoff binding exists yet) — its optional
;;;   tz-offset argument defaults to 0 (UTC) rather than silently guessing
;;;   a local offset.
;;; - ~Z (symbolic timezone name) is unimplemented, exactly as the SRFI
;;;   text itself specifies ("not-implemented"). ~c/~x/~X ("locale's
;;;   representation") use a fixed reasonable format since curry has no
;;;   locale subsystem to draw from.

(define-library (srfi s19 time)
  (import (curry posix) (scheme base) (scheme write))
  (export
    time-duration time-monotonic time-process time-tai time-thread time-utc
    make-time time? time-type time-nanosecond time-second
    set-time-type! set-time-nanosecond! set-time-second! copy-time
    current-time time-resolution
    time<=? time<? time=? time>=? time>?
    time-difference time-difference! add-duration add-duration!
    subtract-duration subtract-duration!
    make-date date? date-nanosecond date-second date-minute date-hour
    date-day date-month date-year date-zone-offset
    date-year-day date-week-day date-week-number
    current-date current-julian-day current-modified-julian-day
    date->julian-day date->modified-julian-day
    date->time-monotonic date->time-tai date->time-utc
    julian-day->date julian-day->time-monotonic julian-day->time-tai julian-day->time-utc
    modified-julian-day->date modified-julian-day->time-monotonic
    modified-julian-day->time-tai modified-julian-day->time-utc
    time-monotonic->date time-monotonic->julian-day time-monotonic->modified-julian-day
    time-monotonic->time-tai time-monotonic->time-tai!
    time-monotonic->time-utc time-monotonic->time-utc!
    time-tai->date time-tai->julian-day time-tai->modified-julian-day
    time-tai->time-monotonic time-tai->time-monotonic!
    time-tai->time-utc time-tai->time-utc!
    time-utc->date time-utc->julian-day time-utc->modified-julian-day
    time-utc->time-monotonic time-utc->time-monotonic!
    time-utc->time-tai time-utc->time-tai!
    date->string string->date)
  (begin

    ;; ---------------------------------------------------------------------
    ;; Constants
    ;; ---------------------------------------------------------------------

    (define time-duration  'time-duration)
    (define time-monotonic 'time-monotonic)
    (define time-process   'time-process)
    (define time-tai       'time-tai)
    (define time-thread    'time-thread)
    (define time-utc       'time-utc)

    (define (%not-tai who)
      (error (string-append who ": time-tai is not supported (no leap-second table)")))

    ;; ---------------------------------------------------------------------
    ;; Time object — mutable, per the SRFI (set-time-*! exist).
    ;; ---------------------------------------------------------------------

    (define-record-type <time>
      (%make-time type second nanosecond)
      time?
      (type       %time-type       %time-type-set!)
      (second     %time-second     %time-second-set!)
      (nanosecond %time-nanosecond %time-nanosecond-set!))

    ;; Note the SRFI's own constructor argument order: nanosecond, second.
    (define (make-time type nanosecond second)
      (%make-time type second nanosecond))

    (define (time-type t) (%time-type t))
    (define (time-second t) (%time-second t))
    (define (time-nanosecond t) (%time-nanosecond t))
    (define (set-time-type! t v) (%time-type-set! t v))
    (define (set-time-second! t v) (%time-second-set! t v))
    (define (set-time-nanosecond! t v) (%time-nanosecond-set! t v))
    (define (copy-time t) (%make-time (%time-type t) (%time-second t) (%time-nanosecond t)))

    (define (%check-not-tai who t)
      (when (eq? (%time-type t) time-tai) (%not-tai who)))

    ;; ---------------------------------------------------------------------
    ;; Current time / resolution
    ;; ---------------------------------------------------------------------

    (define (current-time . opt)
      (let ((type (if (pair? opt) (car opt) time-utc)))
        (when (eq? type time-tai) (%not-tai "current-time"))
        (let* ((secs (if (memq type (list time-monotonic time-process time-thread))
                         (monotonic-time)
                         (posix-time)))
               (whole (floor secs))
               (frac (- secs whole)))
          (%make-time type (inexact->exact whole)
                      (inexact->exact (round (* frac 1000000000)))))))

    (define (time-resolution . opt) 1) ; nanosecond-labeled fields, but posix-time's actual precision is coarser

    ;; ---------------------------------------------------------------------
    ;; Comparison
    ;; ---------------------------------------------------------------------

    (define (%time->rational t) (+ (%time-second t) (/ (%time-nanosecond t) 1000000000)))

    (define (time=? a b) (= (%time->rational a) (%time->rational b)))
    (define (time<? a b) (< (%time->rational a) (%time->rational b)))
    (define (time<=? a b) (<= (%time->rational a) (%time->rational b)))
    (define (time>? a b) (> (%time->rational a) (%time->rational b)))
    (define (time>=? a b) (>= (%time->rational a) (%time->rational b)))

    ;; ---------------------------------------------------------------------
    ;; Arithmetic — normalize nanoseconds into [0, 10^9) by carrying into
    ;; seconds, matching SRFI-174's convention.
    ;; ---------------------------------------------------------------------

    (define (%normalize-time! t)
      (let* ((s (%time-second t)) (ns (%time-nanosecond t)))
        (let-values (((carry rem) (floor/ ns 1000000000)))
          (%time-second-set! t (+ s carry))
          (%time-nanosecond-set! t rem)))
      t)

    (define (time-difference! a b)
      (%time-second-set! a (- (%time-second a) (%time-second b)))
      (%time-nanosecond-set! a (- (%time-nanosecond a) (%time-nanosecond b)))
      (%normalize-time! a)
      (%time-type-set! a time-duration)
      a)

    (define (time-difference a b) (time-difference! (copy-time a) (copy-time b)))

    (define (add-duration! t dur)
      (%time-second-set! t (+ (%time-second t) (%time-second dur)))
      (%time-nanosecond-set! t (+ (%time-nanosecond t) (%time-nanosecond dur)))
      (%normalize-time! t))

    (define (add-duration t dur) (add-duration! (copy-time t) dur))

    (define (subtract-duration! t dur)
      (%time-second-set! t (- (%time-second t) (%time-second dur)))
      (%time-nanosecond-set! t (- (%time-nanosecond t) (%time-nanosecond dur)))
      (%normalize-time! t))

    (define (subtract-duration t dur) (subtract-duration! (copy-time t) dur))

    ;; ---------------------------------------------------------------------
    ;; Date object — immutable (the SRFI defines no set-date-*!).
    ;; ---------------------------------------------------------------------

    (define-record-type <date>
      (%make-date nanosecond second minute hour day month year zone-offset)
      date?
      (nanosecond  date-nanosecond)
      (second      date-second)
      (minute      date-minute)
      (hour        date-hour)
      (day         date-day)
      (month       date-month)
      (year        date-year)
      (zone-offset date-zone-offset))

    (define (make-date nanosecond second minute hour day month year zone-offset)
      (%make-date nanosecond second minute hour day month year zone-offset))

    ;; ---------------------------------------------------------------------
    ;; Calendar math: proleptic-Gregorian Julian Day Number conversions
    ;; (the standard Fliegel & Van Flandern algorithm and its inverse).
    ;; Julian Day 0 begins at noon; date->julian-day below folds in the
    ;; time-of-day to return an exact rational JD.
    ;; ---------------------------------------------------------------------

    (define (%leap-year? y)
      (and (= 0 (modulo y 4)) (or (not (= 0 (modulo y 100))) (= 0 (modulo y 400)))))

    (define (%days-in-month m y)
      (vector-ref (if (%leap-year? y)
                       #(31 29 31 30 31 30 31 31 30 31 30 31)
                       #(31 28 31 30 31 30 31 31 30 31 30 31))
                  (- m 1)))

    ;; Calendar (y m d) at noon -> integer JDN.
    (define (%ymd->jdn y m d)
      (let* ((a (quotient (- 14 m) 12))
             (y2 (- (+ y 4800) a))
             (m2 (+ m (* 12 a) -3)))
        (+ d
           (quotient (+ (* 153 m2) 2) 5)
           (* 365 y2)
           (quotient y2 4)
           (- (quotient y2 100))
           (quotient y2 400)
           -32045)))

    ;; Integer JDN -> calendar (y m d) at noon.
    (define (%jdn->ymd jdn)
      (let* ((a (+ jdn 32044))
             (b (quotient (+ (* 4 a) 3) 146097))
             (c (- a (quotient (* 146097 b) 4)))
             (d (quotient (+ (* 4 c) 3) 1461))
             (e (- c (quotient (* 1461 d) 4)))
             (m (quotient (+ (* 5 e) 2) 153))
             (day (+ e (- (quotient (+ (* 153 m) 2) 5)) 1))
             (month (+ m 3 (* -12 (quotient m 10))))
             (year (+ (* 100 b) d -4800 (quotient m 10))))
        (list year month day)))

    (define (%weekday-of-jdn jdn) (modulo (+ jdn 1) 7)) ; 0 = Sunday

    (define (date->julian-day date)
      (let* ((jdn (%ymd->jdn (date-year date) (date-month date) (date-day date)))
             (tod (+ (/ (- (date-hour date) 12) 24)
                     (/ (date-minute date) 1440)
                     (/ (date-second date) 86400)
                     (/ (date-nanosecond date) 86400000000000))))
        (+ jdn tod)))

    (define (date->modified-julian-day date) (- (date->julian-day date) 4800001/2))

    (define (julian-day->date jd . opt)
      (let* ((tz (if (pair? opt) (car opt) 0))
             (jd-local (+ jd (/ tz 86400)))
             (jdn (floor (+ jd-local 1/2)))
             (frac (- jd-local jdn -1/2)) ; fraction of day since local midnight, [0,1)
             (ymd (%jdn->ymd (inexact->exact jdn)))
             (secs-of-day (* frac 86400))
             (hour (floor (/ secs-of-day 3600)))
             (rem1 (- secs-of-day (* hour 3600)))
             (minute (floor (/ rem1 60)))
             (rem2 (- rem1 (* minute 60)))
             (second (floor rem2))
             (ns (round (* (- rem2 second) 1000000000))))
        (%make-date (inexact->exact ns) (inexact->exact second) (inexact->exact minute)
                    (inexact->exact hour) (caddr ymd) (cadr ymd) (car ymd) tz)))

    (define (modified-julian-day->date mjd . opt)
      (apply julian-day->date (+ mjd 4800001/2) opt))

    ;; Both of these must use %ymd->jdn (the integer, noon-anchored JDN)
    ;; directly rather than date->julian-day's fractional midnight-relative
    ;; value — flooring a midnight JD (which is always jdn-at-noon minus
    ;; exactly 0.5) systematically undercounts by one.
    (define (date-year-day date)
      (- (%ymd->jdn (date-year date) (date-month date) (date-day date))
         (%ymd->jdn (date-year date) 1 1)))

    (define (date-week-day date)
      (%weekday-of-jdn (%ymd->jdn (date-year date) (date-month date) (date-day date))))

    (define (date-week-number date day-of-week-starting-week)
      (quotient (- (date-year-day date) (modulo (- (date-week-day date) day-of-week-starting-week) 7))
                7))

    ;; strftime %U/%W semantics: date-week-number's raw value treats the
    ;; first occurrence of start-day as the start of week 1, undercounting
    ;; by one for every date once the year doesn't open exactly on
    ;; start-day (i.e. there's a partial "week 0" before the first real
    ;; week) — which is most years. Add that 1 back exactly when Jan 1
    ;; isn't itself start-day.
    (define (%strftime-week-number date start-day)
      (let ((jan1-weekday (%weekday-of-jdn (%ymd->jdn (date-year date) 1 1))))
        (+ (date-week-number date start-day) (if (= jan1-weekday start-day) 0 1))))

    ;; ISO 8601 week number: week 1 is the week (Monday-Sunday) containing
    ;; the year's first Thursday — distinct from %W (which just numbers
    ;; Monday-starting weeks without the Thursday rule), and it can pull
    ;; the last few days of December into next year's week 1, or the first
    ;; few days of January into the previous year's week 52/53.
    (define (%iso-weeks-in-year y)
      (let* ((wd (%weekday-of-jdn (%ymd->jdn y 1 1))) ; 0=Sunday..6=Saturday
             (iso-wd (if (= wd 0) 7 wd)))              ; 1=Monday..7=Sunday
        (if (or (= iso-wd 4) (and (%leap-year? y) (= iso-wd 3))) 53 52)))

    (define (%iso-week-number date)
      (let* ((y (date-year date))
             (ordinal (+ 1 (date-year-day date)))
             (wd (date-week-day date))
             (iso-wd (if (= wd 0) 7 wd))
             (week (quotient (+ (- ordinal iso-wd) 10) 7)))
        (cond
          ((< week 1) (%iso-weeks-in-year (- y 1)))
          ((> week (%iso-weeks-in-year y)) 1)
          (else week))))

    ;; ---------------------------------------------------------------------
    ;; current-date / current-julian-day / current-modified-julian-day
    ;; ---------------------------------------------------------------------

    (define (current-date . opt)
      (let ((tz (if (pair? opt) (car opt) 0)))
        (time-utc->date (current-time time-utc) tz)))

    (define (current-julian-day) (date->julian-day (current-date)))
    (define (current-modified-julian-day) (date->modified-julian-day (current-date)))

    ;; ---------------------------------------------------------------------
    ;; time-utc <-> date / julian day / modified julian day
    ;; ---------------------------------------------------------------------

    (define %jd-epoch-offset 2440587.5) ; JD at 1970-01-01T00:00:00 UTC

    (define (time-utc->julian-day t)
      (%check-not-tai "time-utc->julian-day" t)
      (+ %jd-epoch-offset (/ (+ (%time-second t) (/ (%time-nanosecond t) 1000000000)) 86400)))

    (define (time-utc->modified-julian-day t) (- (time-utc->julian-day t) 4800001/2))

    (define (julian-day->time-utc jd)
      (let ((secs (* (- jd %jd-epoch-offset) 86400)))
        (let-values (((whole frac) (let ((w (floor secs))) (values w (- secs w)))))
          (%make-time time-utc (inexact->exact whole) (inexact->exact (round (* frac 1000000000)))))))

    (define (modified-julian-day->time-utc mjd) (julian-day->time-utc (+ mjd 4800001/2)))

    (define (time-utc->date t . opt)
      (%check-not-tai "time-utc->date" t)
      (apply julian-day->date (time-utc->julian-day t) opt))

    (define (date->time-utc date) (julian-day->time-utc (date->julian-day date)))

    ;; ---------------------------------------------------------------------
    ;; time-monotonic — numerically identical to time-utc in this
    ;; implementation (see module header).
    ;; ---------------------------------------------------------------------

    (define (time-utc->time-monotonic t)
      (%make-time time-monotonic (%time-second t) (%time-nanosecond t)))
    (define (time-utc->time-monotonic! t) (%time-type-set! t time-monotonic) t)
    (define (time-monotonic->time-utc t)
      (%make-time time-utc (%time-second t) (%time-nanosecond t)))
    (define (time-monotonic->time-utc! t) (%time-type-set! t time-utc) t)

    (define (time-monotonic->julian-day t) (time-utc->julian-day (time-monotonic->time-utc t)))
    (define (time-monotonic->modified-julian-day t) (time-utc->modified-julian-day (time-monotonic->time-utc t)))
    (define (time-monotonic->date t . opt) (apply time-utc->date (time-monotonic->time-utc t) opt))
    (define (date->time-monotonic date) (time-utc->time-monotonic (date->time-utc date)))
    (define (julian-day->time-monotonic jd) (time-utc->time-monotonic (julian-day->time-utc jd)))
    (define (modified-julian-day->time-monotonic mjd) (time-utc->time-monotonic (modified-julian-day->time-utc mjd)))

    ;; ---------------------------------------------------------------------
    ;; time-tai — unsupported (see module header)
    ;; ---------------------------------------------------------------------

    (define (date->time-tai date) (%not-tai "date->time-tai"))
    (define (julian-day->time-tai jd) (%not-tai "julian-day->time-tai"))
    (define (modified-julian-day->time-tai mjd) (%not-tai "modified-julian-day->time-tai"))
    (define (time-tai->date t . opt) (%not-tai "time-tai->date"))
    (define (time-tai->julian-day t) (%not-tai "time-tai->julian-day"))
    (define (time-tai->modified-julian-day t) (%not-tai "time-tai->modified-julian-day"))
    (define (time-tai->time-monotonic t) (%not-tai "time-tai->time-monotonic"))
    (define (time-tai->time-monotonic! t) (%not-tai "time-tai->time-monotonic!"))
    (define (time-tai->time-utc t) (%not-tai "time-tai->time-utc"))
    (define (time-tai->time-utc! t) (%not-tai "time-tai->time-utc!"))
    (define (time-monotonic->time-tai t) (%not-tai "time-monotonic->time-tai"))
    (define (time-monotonic->time-tai! t) (%not-tai "time-monotonic->time-tai!"))
    (define (time-utc->time-tai t) (%not-tai "time-utc->time-tai"))
    (define (time-utc->time-tai! t) (%not-tai "time-utc->time-tai!"))

    ;; ---------------------------------------------------------------------
    ;; date->string / string->date
    ;; ---------------------------------------------------------------------

    (define %weekday-abbrev #("Sun" "Mon" "Tue" "Wed" "Thu" "Fri" "Sat"))
    (define %weekday-full   #("Sunday" "Monday" "Tuesday" "Wednesday" "Thursday" "Friday" "Saturday"))
    (define %month-abbrev   #("Jan" "Feb" "Mar" "Apr" "May" "Jun" "Jul" "Aug" "Sep" "Oct" "Nov" "Dec"))
    (define %month-full     #("January" "February" "March" "April" "May" "June" "July"
                               "August" "September" "October" "November" "December"))

    (define (%pad n width)
      (let* ((s (number->string n)) (neg (< n 0)) (digits (if neg (substring s 1 (string-length s)) s)))
        (string-append (if neg "-" "")
                        (make-string (max 0 (- width (string-length digits))) #\0)
                        digits)))

    (define (%blank-pad n width)
      (let ((s (number->string n)))
        (string-append (make-string (max 0 (- width (string-length s))) #\space) s)))

    (define (%hour12 h) (let ((h12 (modulo h 12))) (if (= h12 0) 12 h12)))

    (define (%tz-string offset)
      (let* ((sign (if (< offset 0) "-" "+"))
             (mag (abs offset))
             (hh (quotient mag 3600))
             (mm (quotient (modulo mag 3600) 60)))
        (string-append sign (%pad hh 2) (%pad mm 2))))

    (define (%week-number-sunday date) (%strftime-week-number date 0))
    (define (%week-number-monday date) (%strftime-week-number date 1))

    (define (date->string date . opt)
      (let ((fmt (if (pair? opt) (car opt) "~c"))
            (out (open-output-string)))
        (let loop ((i 0))
          (when (< i (string-length fmt))
            (let ((ch (string-ref fmt i)))
              (if (and (char=? ch #\~) (< (+ i 1) (string-length fmt)))
                  (let ((d (string-ref fmt (+ i 1))))
                    (write-string (%date-directive date d) out)
                    (loop (+ i 2)))
                  (begin (write-char ch out) (loop (+ i 1)))))))
        (get-output-string out)))

    (define (%date-directive date d)
      (case d
        ((#\~) "~")
        ((#\a) (vector-ref %weekday-abbrev (date-week-day date)))
        ((#\A) (vector-ref %weekday-full (date-week-day date)))
        ((#\b #\h) (vector-ref %month-abbrev (- (date-month date) 1)))
        ((#\B) (vector-ref %month-full (- (date-month date) 1)))
        ((#\c) (date->string date "~a ~b ~e ~H:~M:~S ~Y"))
        ((#\d) (%pad (date-day date) 2))
        ((#\D) (string-append (%pad (date-month date) 2) "/" (%pad (date-day date) 2) "/"
                               (%pad (modulo (date-year date) 100) 2)))
        ((#\e) (%blank-pad (date-day date) 2))
        ((#\f) (let* ((s (date-second date)) (ns (date-nanosecond date)))
                 (string-append (%pad s 2) "." (%pad ns 9))))
        ((#\H) (%pad (date-hour date) 2))
        ((#\I) (%pad (%hour12 (date-hour date)) 2))
        ((#\j) (%pad (+ 1 (date-year-day date)) 3))
        ((#\k) (%blank-pad (date-hour date) 2))
        ((#\l) (%blank-pad (%hour12 (date-hour date)) 2))
        ((#\m) (%pad (date-month date) 2))
        ((#\M) (%pad (date-minute date) 2))
        ((#\n) "\n")
        ((#\N) (%pad (date-nanosecond date) 9))
        ((#\p) (if (< (date-hour date) 12) "AM" "PM"))
        ((#\r) (date->string date "~I:~M:~S ~p"))
        ((#\s) (number->string (%time-second (date->time-utc date))))
        ((#\S) (%pad (date-second date) 2))
        ((#\t) "\t")
        ((#\T) (date->string date "~H:~M:~S"))
        ((#\U) (%pad (%week-number-sunday date) 2))
        ((#\V) (%pad (%iso-week-number date) 2))
        ((#\w) (number->string (date-week-day date)))
        ((#\W) (%pad (%week-number-monday date) 2))
        ((#\x) (date->string date "~m/~d/~y"))
        ((#\X) (date->string date "~H:~M:~S"))
        ((#\y) (%pad (modulo (date-year date) 100) 2))
        ((#\Y) (number->string (date-year date)))
        ((#\z) (%tz-string (date-zone-offset date)))
        ((#\Z) "")
        ((#\1) (date->string date "~Y-~m-~d"))
        ((#\2) (date->string date "~H:~M:~S~z"))
        ((#\3) (date->string date "~H:~M:~S"))
        ((#\4) (date->string date "~Y-~m-~dT~H:~M:~S~z"))
        ((#\5) (date->string date "~Y-~m-~dT~H:~M:~S"))
        (else (string (integer->char 126) d))))

    ;; ---- string->date ------------------------------------------------

    (define (%char-numeric-at? s i)
      (and (< i (string-length s)) (char-numeric? (string-ref s i))))

    (define (%skip-while s i pred)
      (let loop ((i i)) (if (and (< i (string-length s)) (pred (string-ref s i))) (loop (+ i 1)) i)))

    (define (%read-int s i)
      ;; Accept a leading '+' as a no-op sign as well as '-' — %tz-string
      ;; always emits an explicit sign (including '+' for a zero/positive
      ;; offset, e.g. UTC prints as "+0000"), so ~z must be able to parse
      ;; its own output back.
      (let* ((neg (and (< i (string-length s)) (char=? (string-ref s i) #\-)))
             (pos (and (< i (string-length s)) (char=? (string-ref s i) #\+)))
             (start (if (or neg pos) (+ i 1) i))
             (end (%skip-while s start char-numeric?)))
        (if (= end start)
            (error "string->date: expected a number" s i)
            (values (* (if neg -1 1) (string->number (substring s start end))) end))))

    (define (%read-alpha s i)
      (let ((end (%skip-while s i char-alphabetic?))) (values (substring s i end) end)))

    (define (string->date input template)
      (let ((year #f) (month #f) (day #f) (hour 0) (minute 0) (second 0) (zone 0)
            (si 0) (ti 0))
        (let loop ()
          (if (>= ti (string-length template))
              (%make-date 0 second minute hour (or day 1) (or month 1) (or year 1970) zone)
              (let ((tc (string-ref template ti)))
                (if (and (char=? tc #\~) (< (+ ti 1) (string-length template)))
                    (let ((d (string-ref template (+ ti 1))))
                      (case d
                        ((#\~) (set! si (+ si 1)) (set! ti (+ ti 2)) (loop))
                        ((#\a #\A #\b #\B #\h)
                         (set! si (%skip-while input si (lambda (c) (not (char-alphabetic? c)))))
                         (let-values (((s ni) (%read-alpha input si)))
                           (when (memv d (list #\b #\B #\h))
                             (let mloop ((i 0))
                               (when (< i 12)
                                 (if (or (%string-prefix-ci? s (vector-ref %month-abbrev i))
                                         (%string-prefix-ci? s (vector-ref %month-full i)))
                                     (set! month (+ i 1))
                                     (mloop (+ i 1))))))
                           (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\d) (set! si (%skip-while input si (lambda (c) (not (char-numeric? c)))))
                         (let-values (((v ni) (%read-int input si))) (set! day v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\e) (set! si (%skip-while input si (lambda (c) (char=? c #\space))))
                         (let-values (((v ni) (%read-int input si))) (set! day v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\H) (set! si (%skip-while input si (lambda (c) (not (char-numeric? c)))))
                         (let-values (((v ni) (%read-int input si))) (set! hour v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\k) (set! si (%skip-while input si (lambda (c) (char=? c #\space))))
                         (let-values (((v ni) (%read-int input si))) (set! hour v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\m) (set! si (%skip-while input si (lambda (c) (not (char-numeric? c)))))
                         (let-values (((v ni) (%read-int input si))) (set! month v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\M) (set! si (%skip-while input si (lambda (c) (not (char-numeric? c)))))
                         (let-values (((v ni) (%read-int input si))) (set! minute v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\S) (set! si (%skip-while input si (lambda (c) (not (char-numeric? c)))))
                         (let-values (((v ni) (%read-int input si))) (set! second v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\y) (let-values (((v ni) (%read-int input si)))
                                 (set! year (if (< v 50) (+ 2000 v) (+ 1900 v))) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\Y) (set! si (%skip-while input si (lambda (c) (not (or (char-numeric? c) (char=? c #\-))))))
                         (let-values (((v ni) (%read-int input si))) (set! year v) (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        ((#\z) (let-values (((v ni) (%read-int input si)))
                                 (set! zone (* (quotient v 100) 3600))
                                 (set! zone (+ zone (* (if (< v 0) -1 1) (modulo (abs v) 100) 60)))
                                 (set! si ni))
                         (set! ti (+ ti 2)) (loop))
                        (else (set! ti (+ ti 2)) (loop))))
                    (begin
                      (when (and (< si (string-length input)) (char=? (string-ref input si) tc))
                        (set! si (+ si 1)))
                      (set! ti (+ ti 1))
                      (loop))))))))

    (define (%string-prefix-ci? prefix s)
      (and (<= (string-length prefix) (string-length s))
           (string-ci=? prefix (substring s 0 (string-length prefix)))))))
