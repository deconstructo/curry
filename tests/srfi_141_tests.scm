;;; srfi_141_tests.scm — SRFI-141 (Integer Division): ceiling/round/
;;; euclidean/balanced families beyond R7RS's own floor/truncate.
;;; Every expected value here was independently checked by hand against
;;; the SRFI's mathematical definitions before being written into the
;;; library, not derived from the implementation itself.

(import (scheme base)
        (srfi s141 division-operators) (srfi 141) (srfi srfi-141))

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

(define (qr proc n d) (call-with-values (lambda () (proc n d)) list))

;;; ceiling/

(check "ceiling/ positive"        (qr ceiling/ 7 2)  (list 4 -1))
(check "ceiling/ negative numer"  (qr ceiling/ -7 2) (list -3 -1))
(check "ceiling-quotient"         (ceiling-quotient 7 2) 4)
(check "ceiling-remainder"        (ceiling-remainder 7 2) -1)

;;; round/ (ties to even)

(check "round/ tie, fq odd -> up to even"   (qr round/ 7 2) (list 4 -1))
(check "round/ tie, fq already even"        (qr round/ 5 2) (list 2 1))
(check "round/ negative tie, fq even"       (qr round/ -7 2) (list -4 1))
(check "round/ non-tie, rounds down"        (qr round/ 7 3) (list 2 1))
(check "round/ non-tie, rounds up"          (qr round/ 8 3) (list 3 -1))

;;; euclidean/ (remainder always in [0, |d|))

(check "euclidean/ negative divisor"           (qr euclidean/ 7 -2)  (list -3 1))
(check "euclidean/ negative dividend"          (qr euclidean/ -7 2)  (list -4 1))
(check "euclidean/ both negative"              (qr euclidean/ -7 -2) (list 4 1))
(check "euclidean/ remainder non-negative"     (>= (euclidean-remainder -7 2) 0) #t)
(check "euclidean/ remainder non-negative, negative divisor"
       (>= (euclidean-remainder 7 -2) 0) #t)

;;; balanced/ (remainder always in [-|d|/2, |d|/2))

(check "balanced/ tie resolves toward q+1 (not the even quotient)"
       (qr balanced/ 6 4) (list 2 -2))
(check "balanced/ tie, dividend 2"    (qr balanced/ 2 4)  (list 1 -2))
(check "balanced/ tie, negative"      (qr balanced/ -2 4) (list 0 -2))
(check "balanced/ non-tie"            (qr balanced/ 8 3)  (list 3 -1))

;; Regression: a tie with a NEGATIVE divisor must resolve the opposite
;; way from a positive divisor -- an earlier version of this file only
;; checked positive-d ties by hand and got this wrong (the remainder
;; landed on the excluded upper boundary |d|/2 instead of the included
;; lower boundary -|d|/2), caught by independent review running these
;; exact cases against a live build, not by static reading.
(check "balanced/ tie, negative divisor (1)"       (qr balanced/ 7 -2)   (list -4 -1))
(check "balanced/ tie, negative divisor (2)"       (qr balanced/ 5 -2)   (list -3 -1))
(check "balanced/ tie, negative divisor, |d|=4"    (qr balanced/ 10 -4)  (list -3 -2))
(check "balanced/ tie, negative divisor, |d|=4 (2)" (qr balanced/ 14 -4) (list -4 -2))
(check "balanced/ tie, both n and d negative"      (qr balanced/ -7 -2)  (list 3 -1))
;; The valid range is asymmetric ([-|d|/2, |d|/2), lower bound
;; inclusive, upper bound exclusive) -- checking (< (abs r) half) would
;; be wrong, since r = -half is a valid boundary value but abs turns it
;; into +half, which is NOT valid; check the two ends separately instead.
(define (%in-balanced-range? r d) (let ((half (/ (abs d) 2))) (and (>= r (- half)) (< r half))))
(check "balanced/ remainder in range for all four sign combinations at a tie"
       (list (%in-balanced-range? (balanced-remainder 6 4) 4)     ; d>0, n>0
             (%in-balanced-range? (balanced-remainder -6 4) 4)    ; d>0, n<0
             (%in-balanced-range? (balanced-remainder 7 -2) -2)   ; d<0, n>0
             (%in-balanced-range? (balanced-remainder -7 -2) -2)) ; d<0, n<0
       (list #t #t #t #t))

(check "balanced/ tie with a negative divisor at bignum scale stays in range"
       (%in-balanced-range? (balanced-remainder (+ (* 8 (expt 2 100)) 4) -8) -8)
       #t)

;;; Bignum sanity: none of these families should ever silently overflow
;;; or lose precision going through curry's numeric tower.
;;;
;;; -2^100 mod 7 = 5, since 2^100 mod 7 = 2 (2^3 = 8 = 1 mod 7, and
;;; 100 = 3*33 + 1, so 2^100 = (2^3)^33 * 2 = 1^33 * 2 = 2 mod 7).

(check "euclidean/ remainder for -2^100 over 7"
       (euclidean-remainder (- (expt 2 100)) 7)
       5)
(check "euclidean/ quotient*divisor+remainder reconstructs the dividend (bignum)"
       (+ (* 7 (euclidean-quotient (- (expt 2 100)) 7)) (euclidean-remainder (- (expt 2 100)) 7))
       (- (expt 2 100)))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
