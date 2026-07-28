(define-library (surfage s174 posix-timespecs)
  (import (scheme base))
  (export
    timespec timespec? timespec-seconds timespec-nanoseconds
    inexact->timespec timespec->inexact
    timespec=? timespec<? timespec-hash)
  (begin

    ;; nanoseconds must be in [0, 10^9) — represented this way rather than
    ;; allowing e.g. negative nanoseconds with a compensating seconds value,
    ;; matching the SRFI's stated valid domain for the component.
    (define (%check-nanoseconds who ns)
      (unless (and (exact-integer? ns) (>= ns 0) (< ns 1000000000))
        (error (string-append who ": nanoseconds must be an exact integer in [0, 10^9)") ns)))

    (define-record-type <timespec>
      (%make-timespec seconds nanoseconds)
      timespec?
      (seconds     timespec-seconds)
      (nanoseconds timespec-nanoseconds))

    (define (timespec seconds nanoseconds)
      (unless (exact-integer? seconds) (error "timespec: seconds must be an exact integer" seconds))
      (%check-nanoseconds "timespec" nanoseconds)
      (%make-timespec seconds nanoseconds))

    ;; Floor toward negative infinity so a negative inexact value (an
    ;; instant before the epoch) still produces a non-negative nanoseconds
    ;; component, e.g. -1.25 -> seconds -2, nanoseconds 750000000 (i.e.
    ;; -2 + 0.75 = -1.25), not seconds -1 with negative nanoseconds.
    (define (inexact->timespec x)
      (let* ((s (floor x))
             (frac (- x s))
             (ns (min 999999999 (max 0 (round (* frac 1000000000))))))
        (%make-timespec (inexact->exact s) (inexact->exact ns))))

    (define (timespec->inexact ts)
      (+ (exact->inexact (timespec-seconds ts))
         (/ (exact->inexact (timespec-nanoseconds ts)) 1000000000.0)))

    (define (timespec=? a b)
      (and (= (timespec-seconds a) (timespec-seconds b))
           (= (timespec-nanoseconds a) (timespec-nanoseconds b))))

    (define (timespec<? a b)
      (or (< (timespec-seconds a) (timespec-seconds b))
          (and (= (timespec-seconds a) (timespec-seconds b))
               (< (timespec-nanoseconds a) (timespec-nanoseconds b)))))

    ;; Simple, deterministic combination of the two components into a
    ;; non-negative exact integer — equal timespecs always hash equally,
    ;; which is all the SRFI requires; the specific values aren't meant to
    ;; match any other implementation's.
    (define (timespec-hash ts)
      (abs (+ (* (timespec-seconds ts) 1000000007) (timespec-nanoseconds ts))))))
