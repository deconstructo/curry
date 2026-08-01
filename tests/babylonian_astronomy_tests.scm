;;; babylonian_astronomy_tests.scm — (curry babylonian-astronomy) tests
;;;
;;; Verifies the zigzag function against known min/max/period values, the
;;; synodic-month/Saros constants against their published sexagesimal
;;; values, the month-name lookup, and the Akkadian aliases.

(import (curry babylonian-astronomy))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;;; ---- babylonian-zigzag ----

;; Boundary values: min at n=0, max at n=half-period
(check "zigzag min at n=0"        (babylonian-zigzag 10 0 5 0)   0)
(check "zigzag max at n=half"     (babylonian-zigzag 10 0 5 5)   10)
(check "zigzag min at full cycle" (babylonian-zigzag 10 0 5 10)  0)
(check "zigzag midpoint rising"   (babylonian-zigzag 10 0 5 2)   4)
(check "zigzag midpoint falling"  (babylonian-zigzag 10 0 5 7)   6)

;; Wraps for n beyond one period, and for negative n
(check "zigzag wraps forward"     (babylonian-zigzag 10 0 5 15)  10)
(check "zigzag negative n"        (babylonian-zigzag 10 0 5 -3)  6)
(check "zigzag negative full cyc" (babylonian-zigzag 10 0 5 -10) 0)

;; System A daylight-length worked example: max 216 UŠ, min 144 UŠ,
;; 6-month half-period (winter solstice = month 0, summer = month 6)
(check "daylight min (winter)"   (system-a-daylight-length 0)  144)
(check "daylight max (summer)"   (system-a-daylight-length 6)  216)
(check "daylight midpoint"       (system-a-daylight-length 3)  180)
(check "daylight full cycle"     (system-a-daylight-length 12) 144)

;;; ---- Synodic month / Saros ----

;; 29;31,50,8,20 in Neugebauer notation = 29 + 31/60 + 50/3600 + 8/216000 + 20/12960000
(check "synodic month exact value"
       (synodic-month-length)
       765433/25920)
(check "synodic month ~29.53 days"
       (let ((d (exact->inexact (synodic-month-length))))
         (and (> d 29.5305) (< d 29.5307)))
       #t)

;; Saros = 223 synodic months ≈ 6585.32 days
(check "saros ~6585.32 days"
       (let ((d (exact->inexact (saros-length-days))))
         (and (> d 6585.32) (< d 6585.33)))
       #t)
(check "saros is 223 synodic months"
       (saros-length-days)
       (* 223 (synodic-month-length)))

;; Eclipse window is exactly one Saros later
(check "next eclipse window = known day + saros"
       (babylonian-next-eclipse-window 2451545)
       (+ 2451545 (saros-length-days)))

;;; ---- Babylonian month names ----

(check "month 1 is Nisannu"  (babylonian-month-name 1)  "Nisannu")
(check "month 6 is Ululu"    (babylonian-month-name 6)  "Ulūlu")
(check "month 12 is Addaru"  (babylonian-month-name 12) "Addaru")

;;; ---- Akkadian aliases ----

(check "warhu alias == synodic-month-length"
       (warḫu) (synodic-month-length))
(check "attalu alias == eclipse window"
       (attalû 2451545) (babylonian-next-eclipse-window 2451545))
(check "sumu-sa-warhi alias == month-name"
       (šumu-ša-warḫi 3) (babylonian-month-name 3))

;; Cuneiform-glyph forms of the same three aliases
(check "cuneiform warhu alias"
       (𒌑𒀭) (synodic-month-length))
(check "cuneiform attalu alias"
       (𒀭 2451545) (babylonian-next-eclipse-window 2451545))
(check "cuneiform sumu-sa-warhi alias"
       (𒁹𒌑 3) (babylonian-month-name 3))

;;; ---- Summary ----

(newline)
(display "babylonian-astronomy tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
