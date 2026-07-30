;;; (srfi s174 posix-timespecs) tests

(import (srfi s174 posix-timespecs))

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

(define t (timespec 100 500000000))
(check "timespec-seconds" (timespec-seconds t) 100)
(check "timespec-nanoseconds" (timespec-nanoseconds t) 500000000)
(check "timespec?" (timespec? t) #t)
(check "timespec? on a non-timespec" (timespec? 5) #f)

(check "timespec->inexact" (timespec->inexact t) 100.5)

(check "inexact->timespec on a positive value"
       (let ((t2 (inexact->timespec 100.5)))
         (list (timespec-seconds t2) (timespec-nanoseconds t2)))
       '(100 500000000))

(check "inexact->timespec on an exact whole number"
       (let ((t2 (inexact->timespec 5.0)))
         (list (timespec-seconds t2) (timespec-nanoseconds t2)))
       '(5 0))

(check "inexact->timespec floors toward negative infinity for negative values"
       (let ((t2 (inexact->timespec -1.25)))
         (list (timespec-seconds t2) (timespec-nanoseconds t2)))
       '(-2 750000000))

(check "timespec=? on equal timespecs" (timespec=? (timespec 5 0) (timespec 5 0)) #t)
(check "timespec=? on differing nanoseconds" (timespec=? (timespec 5 0) (timespec 5 1)) #f)
(check "timespec=? on differing seconds" (timespec=? (timespec 5 0) (timespec 6 0)) #f)

(check "timespec<? by nanoseconds within the same second"
       (timespec<? (timespec 5 0) (timespec 5 1)) #t)
(check "timespec<? by seconds, ignoring a larger nanoseconds on the left"
       (timespec<? (timespec 4 999999999) (timespec 5 0)) #t)
(check "timespec<? is false for equal timespecs"
       (timespec<? (timespec 5 5) (timespec 5 5)) #f)
(check "timespec<? is false when the left is later"
       (timespec<? (timespec 6 0) (timespec 5 0)) #f)

(check "timespec-hash is deterministic"
       (= (timespec-hash (timespec 5 100)) (timespec-hash (timespec 5 100)))
       #t)
(check "timespec-hash is non-negative"
       (>= (timespec-hash (timespec -500 100)) 0)
       #t)
(check "timespec-hash distinguishes different timespecs"
       (= (timespec-hash (timespec 5 100)) (timespec-hash (timespec 5 101)))
       #f)

(check "timespec rejects out-of-range negative nanoseconds"
       (guard (e (#t 'caught)) (timespec 5 -1))
       'caught)
(check "timespec rejects out-of-range nanoseconds >= 10^9"
       (guard (e (#t 'caught)) (timespec 5 1000000000))
       'caught)
(check "timespec rejects a non-exact-integer seconds argument"
       (guard (e (#t 'caught)) (timespec 5.5 0))
       'caught)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
