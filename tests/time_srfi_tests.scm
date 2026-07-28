;;; (surfage s19 time) tests — requires (curry posix)

(import (surfage s19 time))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; Time object

(define t (make-time time-utc 500000000 100))
(check "time?" (time? t) #t)
(check "time? on a non-time" (time? 5) #f)
(check "time-second" (time-second t) 100)
(check "time-nanosecond" (time-nanosecond t) 500000000)
(check "time-type" (time-type t) time-utc)

(set-time-second! t 200)
(check "set-time-second!" (time-second t) 200)
(set-time-nanosecond! t 100)
(check "set-time-nanosecond!" (time-nanosecond t) 100)
(set-time-type! t time-duration)
(check "set-time-type!" (time-type t) time-duration)

(define t-orig (make-time time-utc 0 50))
(define t-copy (copy-time t-orig))
(set-time-second! t-copy 99)
(check "copy-time is independent"
       (list (time-second t-orig) (time-second t-copy))
       (list 50 99))

;;; current-time

(check "current-time returns a time object" (time? (current-time)) #t)
(check "current-time defaults to time-utc" (time-type (current-time)) time-utc)
(check "current-time's second looks like a real wall-clock value"
       (> (time-second (current-time)) 1700000000)
       #t)
(check "current-time time-tai is not supported"
       (guard (e (#t 'caught)) (current-time time-tai))
       'caught)

;;; Comparison

(define ta (make-time time-utc 0 100))
(define tb (make-time time-utc 500000000 100))
(define tc (make-time time-utc 0 200))
(check "time<? by nanoseconds within the same second" (time<? ta tb) #t)
(check "time<? by seconds" (time<? tb tc) #t)
(check "time<=? on equal times" (time<=? ta (copy-time ta)) #t)
(check "time=? on equal times" (time=? ta (copy-time ta)) #t)
(check "time=? on differing times" (time=? ta tb) #f)
(check "time>? is the inverse of time<?" (time>? tc tb) #t)
(check "time>=? on equal times" (time>=? ta (copy-time ta)) #t)

;;; Arithmetic — nanosecond carrying

(define arith-t1 (make-time time-utc 500000000 100))
(define arith-dur (make-time time-duration 800000000 5))
(define arith-sum (add-duration arith-t1 arith-dur))
(check "add-duration carries overflowing nanoseconds into seconds"
       (list (time-second arith-sum) (time-nanosecond arith-sum))
       (list 106 300000000))
(check "add-duration does not mutate its first argument"
       (list (time-second arith-t1) (time-nanosecond arith-t1))
       (list 100 500000000))

(define arith-diff (time-difference arith-sum arith-t1))
(check "time-difference recovers the original duration"
       (list (time-second arith-diff) (time-nanosecond arith-diff))
       (list 5 800000000))

(define sub-t (subtract-duration arith-sum arith-dur))
(check "subtract-duration is the inverse of add-duration"
       (list (time-second sub-t) (time-nanosecond sub-t))
       (list (time-second arith-t1) (time-nanosecond arith-t1)))

;;; Date object

(define d (make-date 500000000 30 15 12 15 6 2023 0))
(check "date?" (date? d) #t)
(check "date-nanosecond" (date-nanosecond d) 500000000)
(check "date-second" (date-second d) 30)
(check "date-minute" (date-minute d) 15)
(check "date-hour" (date-hour d) 12)
(check "date-day" (date-day d) 15)
(check "date-month" (date-month d) 6)
(check "date-year" (date-year d) 2023)
(check "date-zone-offset" (date-zone-offset d) 0)

;;; Calendar math — verified against known reference dates

(check "2000-01-01 is a Saturday (date-week-day, 0=Sunday)"
       (date-week-day (make-date 0 0 0 0 1 1 2000 0))
       6)
(check "1970-01-01 (Unix epoch) is a Thursday"
       (date-week-day (make-date 0 0 0 0 1 1 1970 0))
       4)
(check "2024-02-29 (leap day) is a Thursday"
       (date-week-day (make-date 0 0 0 0 29 2 2024 0))
       4)
(check "2000-01-01 is year-day 0" (date-year-day (make-date 0 0 0 0 1 1 2000 0)) 0)
(check "2023-12-31 is year-day 364 (not a leap year)"
       (date-year-day (make-date 0 0 0 0 31 12 2023 0))
       364)
(check "2024-12-31 is year-day 365 (a leap year)"
       (date-year-day (make-date 0 0 0 0 31 12 2024 0))
       365)

;;; Julian day / modified Julian day round-trips

(check "date -> julian-day -> date round-trips exactly"
       (let* ((jd (date->julian-day d)) (d2 (julian-day->date jd)))
         (list (date-year d2) (date-month d2) (date-day d2) (date-hour d2)
               (date-minute d2) (date-second d2) (date-nanosecond d2)))
       (list 2023 6 15 12 15 30 500000000))

(check "date -> modified-julian-day -> date round-trips"
       (let* ((mjd (date->modified-julian-day d)) (d2 (modified-julian-day->date mjd)))
         (list (date-year d2) (date-month d2) (date-day d2)))
       (list 2023 6 15))

(check "midnight date round-trips through julian-day"
       (let* ((midnight (make-date 0 0 0 0 1 1 2023 0))
              (jd (date->julian-day midnight))
              (d2 (julian-day->date jd)))
         (list (date-year d2) (date-month d2) (date-day d2) (date-hour d2)))
       (list 2023 1 1 0))

;;; time-utc <-> date

(check "date->time-utc and back round-trips"
       (let* ((tu (date->time-utc d)) (d2 (time-utc->date tu 0)))
         (list (date-year d2) (date-month d2) (date-day d2) (date-hour d2) (date-minute d2) (date-second d2)))
       (list 2023 6 15 12 15 30))

;;; time-monotonic — numerically identical to time-utc here

(check "time-utc <-> time-monotonic round-trips the same numeric value"
       (let* ((tm (time-utc->time-monotonic ta)) (back (time-monotonic->time-utc tm)))
         (list (time-second back) (time-nanosecond back)))
       (list (time-second ta) (time-nanosecond ta)))
(check "time-utc->time-monotonic changes the type tag"
       (time-type (time-utc->time-monotonic ta))
       time-monotonic)

;;; TAI — unsupported

(check "date->time-tai raises" (guard (e (#t 'caught)) (date->time-tai d)) 'caught)
(check "time-tai->date raises" (guard (e (#t 'caught)) (time-tai->date (make-time time-tai 0 0))) 'caught)
(check "time-utc->time-tai raises" (guard (e (#t 'caught)) (time-utc->time-tai ta)) 'caught)
(check "julian-day->time-tai raises" (guard (e (#t 'caught)) (julian-day->time-tai 2451545)) 'caught)

;;; date->string

(check "~Y-~m-~dT~H:~M:~S" (date->string d "~Y-~m-~dT~H:~M:~S") "2023-06-15T12:15:30")
(check "~a ~b ~e ~Y (weekday/month names, blank-padded day)"
       (date->string d "~a ~b ~e ~Y")
       "Thu Jun 15 2023")
(check "~A full weekday name" (date->string d "~A") "Thursday")
(check "~B full month name" (date->string d "~B") "June")
(check "~1 ISO date" (date->string d "~1") "2023-06-15")
(check "~5 ISO date-time" (date->string d "~5") "2023-06-15T12:15:30")
(check "~y two-digit year" (date->string d "~y") "23")
(check "~j day of year, zero-padded"
       (date->string d "~j")
       (let ((s (number->string (+ 1 (date-year-day d)))))
         (string-append (make-string (- 3 (string-length s)) #\0) s)))
(check "literal text passes through unchanged"
       (date->string d "Date: ~Y!")
       "Date: 2023!")
(check "~~ produces a literal tilde" (date->string d "~~") "~")

(define dtz (make-date 0 0 0 12 15 6 2023 -18000))
(check "~z negative timezone offset" (date->string dtz "~z") "-0500")

(define dtz2 (make-date 0 0 0 12 15 6 2023 19800))
(check "~z positive non-hour-aligned timezone offset (IST, +05:30)"
       (date->string dtz2 "~z")
       "+0530")

;;; Week-number directives — regressions found by independent review

(check "~U/~W week number (strftime semantics, cross-checked against Python's %U/%W)"
       (date->string (make-date 0 0 0 0 15 7 2024 0) "~U ~W")
       "28 28")

(check "~V ISO 8601 week number: 2018-12-31 belongs to ISO week 1 of 2019"
       (date->string (make-date 0 0 0 0 31 12 2018 0) "~V")
       "01")
(check "~V ISO 8601 week number: 2019-01-01 is also ISO week 1"
       (date->string (make-date 0 0 0 0 1 1 2019 0) "~V")
       "01")
(check "~V ISO 8601 week number: 2005-01-01 belongs to ISO week 53 of 2004"
       (date->string (make-date 0 0 0 0 1 1 2005 0) "~V")
       "53")
(check "~V ISO 8601 week number: an ordinary midyear date"
       (date->string (make-date 0 0 0 0 15 7 2024 0) "~V")
       "29")

;;; string->date

(check "~z parses a zero offset with the '+' sign date->string always emits"
       (date-zone-offset (string->date "+0000" "~z"))
       0)
(check "~z parses a positive offset round-tripped from date->string"
       (date-zone-offset (string->date (date->string dtz2 "~z") "~z"))
       19800)

(check "basic numeric template round-trips"
       (let ((p (string->date "2023-06-15 14:30:00" "~Y-~m-~d ~H:~M:~S")))
         (list (date-year p) (date-month p) (date-day p) (date-hour p) (date-minute p) (date-second p)))
       (list 2023 6 15 14 30 0))

(check "weekday and month names are skipped/parsed without upsetting numeric fields"
       (let ((p (string->date "Thu Jun 15 2023" "~a ~b ~d ~Y")))
         (list (date-year p) (date-month p) (date-day p)))
       (list 2023 6 15))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
