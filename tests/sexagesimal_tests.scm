;;; sexagesimal_tests.scm — v1.2.5 Babylonian/sexagesimal number system tests
;;;
;;; Tests the #s reader prefix, cuneiform reader, number->string with
;;; 'neugebauer and 'cuneiform, string->number with same, the
;;; current-number-notation dynamic parameter, and the (curry sexagesimal)
;;; convenience module.

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

;;; ---- #s reader prefix (Neugebauer literals) ----

;; Single digit (no comma → pure integer)
(check "#s single digit"     #s45        45)
(check "#s zero"             #s0         0)

;; Pure integers (no semicolon → base-60 integer)
(check "#s two-digit"        #s1,11      71)
(check "#s three-digit"      #s1,0,0     3600)
(check "#s large"            #s1,0,0,0   216000)

;; Mixed integer;fractional
(check "#s half"             #s1;30      3/2)
(check "#s sqrt2 approx"     #s1;24,51,10  30547/21600)
(check "#s zero;thirty"      #s0;30      1/2)
(check "#s exact third"      #s0;20      1/3)

;;; ---- Cuneiform reader ----

;; Single-group: 𒁹 = 1 (𒁹 also serves as Akkadian 'define', so use string->number)
(check "cuneiform 1"    (string->number "𒁹" 'cuneiform)   1)
;; Single-group: 𒌋 = 10 (𒌋 also serves as Akkadian 'and', so use string->number)
(check "cuneiform 10"   (string->number "𒌋" 'cuneiform)   10)
;; Single-group: 𒌋𒌋𒁹𒁹𒁹 = 23
(check "cuneiform 23"   𒌋𒌋𒁹𒁹𒁹  23)
;; Two groups with space: 𒁹 𒌋𒁹 = 1*60+11 = 71
(check "cuneiform 71"   𒁹 𒌋𒁹   71)
;; 𒑊 = zero placeholder
(check "cuneiform 0"    𒑊     0)
;; 𒁹 𒑊 = 1*60+0 = 60
(check "cuneiform 60"   𒁹 𒑊   60)

;;; ---- number->string with 'neugebauer ----

(check "n->s integer 71"    (number->string 71 'neugebauer)    "1,11")
(check "n->s integer 3600"  (number->string 3600 'neugebauer)  "1,0,0")
(check "n->s integer 1"     (number->string 1 'neugebauer)     "1")
(check "n->s integer 0"     (number->string 0 'neugebauer)     "0")
(check "n->s rational 3/2"  (number->string 3/2 'neugebauer)   "1;30")
(check "n->s rational 1/2"  (number->string 1/2 'neugebauer)   "0;30")
(check "n->s rational 1/3"  (number->string 1/3 'neugebauer)   "0;20")
(check "n->s rational 1/4"  (number->string 1/4 'neugebauer)   "0;15")
(check "n->s ybc7289"       (number->string 30547/21600 'neugebauer) "1;24,51,10")

;; With #:places for flonum
(check "n->s flonum sqrt2"  (number->string (sqrt 2) 'neugebauer #:places 3) "1;24,51,10")
(check "n->s flonum 1.5"    (number->string 1.5 'neugebauer #:places 2) "1;30")

;; Large-magnitude flonums whose integer part exceeds LONG_MAX/LONG_MIN
;; used to hit undefined behavior on a (long) cast before conversion;
;; just checking these don't crash and round-trip through the digit
;; extractor is the regression coverage (exact digit string isn't the
;; point here, not-crashing and staying finite is).
(check "n->s flonum 1e19 no crash"
       (string? (number->string 1e19 'neugebauer)) #t)
(check "n->s flonum -1e19 no crash"
       (string? (number->string -1e19 'neugebauer)) #t)
(check "n->s flonum 1e19 parses back to an exact integer"
       (exact? (string->number (number->string 1e19 'neugebauer) 'neugebauer)) #t)
;; Right at the LONG_MAX boundary (~9.2e18) - the old (long) cast was still
;; technically well-defined just below it, so this pins the boundary itself.
(check "n->s flonum near LONG_MAX no crash"
       (string? (number->string 9.2e18 'neugebauer)) #t)
;; Past the mpz_to_base60_digits 64-digit overflow guard (unrelated to and
;; unaffected by this fix, but exercised via the same flonum entry point) -
;; falls back to plain scientific-notation printing rather than crashing.
(check "n->s flonum 1e300 falls back without crashing"
       (number->string 1e300 'neugebauer) (number->string 1e300))
(check "n->s flonum 1e308 falls back without crashing"
       (number->string 1e308 'neugebauer) (number->string 1e308))

;;; ---- number->string with 'cuneiform ----

;; Integer → cuneiform
(check "n->s cun 1"   (number->string 1 'cuneiform)  "𒁹")
(check "n->s cun 10"  (number->string 10 'cuneiform) "𒌋")
(check "n->s cun 23"  (number->string 23 'cuneiform) "𒌋𒌋𒁹𒁹𒁹")
(check "n->s cun 71"  (number->string 71 'cuneiform) "𒁹 𒌋𒁹")
(check "n->s cun 60"  (number->string 60 'cuneiform) "𒁹 𒑊")
(check "n->s cun 0"   (number->string 0 'cuneiform)  "𒑊")

;;; ---- string->number with 'neugebauer ----

(check "s->n neu integer 71"    (string->number "1,11" 'neugebauer)      71)
(check "s->n neu integer 3600"  (string->number "1,0,0" 'neugebauer)     3600)
(check "s->n neu rational 3/2"  (string->number "1;30" 'neugebauer)      3/2)
(check "s->n neu rational 1/2"  (string->number "0;30" 'neugebauer)      1/2)
(check "s->n neu rational 1/3"  (string->number "0;20" 'neugebauer)      1/3)
(check "s->n neu ybc7289"       (string->number "1;24,51,10" 'neugebauer) 30547/21600)
(check "s->n neu single digit"  (string->number "45" 'neugebauer)        45)
(check "s->n neu invalid"       (string->number "bad" 'neugebauer)        #f)

;;; ---- string->number with 'cuneiform ----

(check "s->n cun 1"   (string->number "𒁹" 'cuneiform)     1)
(check "s->n cun 10"  (string->number "𒌋" 'cuneiform)     10)
(check "s->n cun 23"  (string->number "𒌋𒌋𒁹𒁹𒁹" 'cuneiform) 23)
(check "s->n cun 71"  (string->number "𒁹 𒌋𒁹" 'cuneiform)  71)
(check "s->n cun 0"   (string->number "𒑊" 'cuneiform)     0)

;;; ---- Round-trip: reader → number->string ----

(check "rt #s 1;30"       (number->string #s1;30 'neugebauer)   "1;30")
(check "rt #s 1,0,0"      (number->string #s1,0,0 'neugebauer)  "1,0,0")
(check "rt cuneiform 71"  (number->string 𒁹 𒌋𒁹 'cuneiform)    "𒁹 𒌋𒁹")

;;; ---- current-number-notation ----

;; Default: #f (decimal)
(check "cnn default" (current-number-notation) #f)

;; Set to neugebauer
(current-number-notation 'neugebauer)
(check "cnn set neugebauer" (current-number-notation) 'neugebauer)

;; number->string should still work normally (notation only affects display)
(check "cnn n->s still works" (number->string 3600 10) "3600")

;; Reset
(current-number-notation #f)
(check "cnn reset" (current-number-notation) #f)

;;; ---- (curry sexagesimal) module ----

(import (curry sexagesimal))

;; rational->sexagesimal
(check "r->s 3/2"     (rational->sexagesimal 3/2)    '(1 30))
(check "r->s 1/3"     (rational->sexagesimal 1/3)    '(0 20))
(check "r->s 1"       (rational->sexagesimal 1)      '(1))
(check "r->s 0"       (rational->sexagesimal 0)      '(0))
(check "r->s ybc7289" (rational->sexagesimal 30547/21600) '(1 24 51 10))

;; sexagesimal->rational
(check "s->r (1 30)"       (sexagesimal->rational '(1 30))        3/2)
(check "s->r (0 20)"       (sexagesimal->rational '(0 20))        1/3)
(check "s->r (1 24 51 10)" (sexagesimal->rational '(1 24 51 10))  30547/21600)
(check "s->r (1)"          (sexagesimal->rational '(1))           1)

;; hms->seconds
(check "hms 1h30m"    (hms->seconds '(1 30 0))   5400)
(check "hms 0h0m30s"  (hms->seconds '(0 0 30))   30)
(check "hms 2h"       (hms->seconds '(2 0 0))    7200)

;; seconds->hms
(check "s->hms 5400"   (seconds->hms 5400)   '(1 30 0))
(check "s->hms 3661"   (seconds->hms 3661)   '(1 1 1))
(check "s->hms 30"     (seconds->hms 30)     '(0 0 30))

;; dms->degrees
(check "dms->deg 1°30′"        (dms->degrees '(1 30 0))   3/2)
(check "dms->deg 0°0′36″"      (dms->degrees '(0 0 36))   1/100)
;; 23°27′ = earth's axial tilt (approx)
(let ((deg (dms->degrees '(23 27 0))))
  (check "dms->deg axial tilt" (and (> deg 23.449) (< deg 23.451)) #t))

;; cuneiform->neugebauer
(check "cun->neu 71"   (cuneiform->neugebauer "𒁹 𒌋𒁹")  "1,11")
(check "cun->neu 1"    (cuneiform->neugebauer "𒁹")       "1")

;; neugebauer->cuneiform
(check "neu->cun 71"   (neugebauer->cuneiform "1,11")    "𒁹 𒌋𒁹")
(check "neu->cun 1"    (neugebauer->cuneiform "1")       "𒁹")

;; YBC 7289 demo
(check "ybc7289 rational"    (sex:ybc7289)  30547/21600)
(check "ybc7289 approx sqrt" (let ((r (exact->inexact (sex:ybc7289))))
                               (and (> r 1.4142) (< r 1.4143)))  #t)

;;; ---- number->string fallback for non-fixnum/bignum/flonum types ----
;;; Regression: num_to_string() used to fall back to the literal string
;;; "#<number>" for any type it didn't special-case, which included plain
;;; rationals as well as complex/quaternion/octonion. It now delegates to the
;;; same writer `display`/`write` already use.

(check "n->s rational 3/2 (radix)"  (number->string 3/2)                      "3/2")
(check "n->s complex"                (number->string (make-rectangular 3 4))   "3+4i")
(check "n->s quaternion"             (number->string (make-quaternion 1 2 3 4)) "1+2i+3j+4k")

;;; ---- Summary ----

(newline)
(display "sexagesimal tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
