(define-library (srfi 19)
  (import (srfi s19 time))
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
    date->string string->date))
