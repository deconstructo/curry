;;; SRFI-208: NaN procedures.
;;;
;;; https://srfi.schemers.org/srfi-208/ -- dissecting/constructing IEEE
;;; 754 binary64 NaN values by their sign/quiet/payload fields. Ordinary
;;; Scheme arithmetic on a NaN never exposes these bits (an operation is
;;; free to canonicalize or collapse them), so this is only expressible
;;; by bit-punning the underlying double's raw 64-bit representation --
;;; exactly the technique the SRFI's own reference implementation uses
;;; (a C union). curry has no such union exposed to Scheme, so this
;;; library is layered on two small new core primitives added alongside
;;; it, `%flonum-raw-bits`/`%raw-bits->flonum` (src/builtins.c) -- a
;;; memcpy-based double<->uint64 bit-pun, nothing more; every actual
;;; field extraction/construction below is ordinary Scheme bitwise-
;;; integer arithmetic against that raw pattern.
;;;
;;; Bit layout (IEEE 754 binary64; the SRFI's own text explicitly leaves
;;; this implementation-defined): bit 63 is the sign, bits 62-52
;;; (all 1s, 0x7ff) are the exponent field that marks a NaN/infinity,
;;; and of the 52-bit trailing significand, bit 51 (its own MSB) is
;;; treated as the quiet/signaling flag and the low 51 bits are the
;;; payload -- the same convention essentially every real CPU's own FPU
;;; uses (1 = quiet, 0 = signaling), not merely curry's own invention.
;;;
;;; No single-precision support: curry's numeric tower has no native
;;; single-precision float type (see docs/reference/writing-a-module.md-
;;; adjacent numeric-tower docs), so make-nan's optional `float` argument
;;; is validated (must be an inexact real, per the SRFI's own text) but
;;; never changes the constructed precision -- every NaN this library
;;; produces or consumes is binary64. nan=?'s own SRFI text describes
;;; converting mismatched precisions "to a precision at least as high as
;;; the higher-precision value" before comparing; with only one
;;; precision ever in play here, that step is vacuous.

(define-library (srfi s208 nan-procedures)
  (import (scheme base))
  (export make-nan nan-negative? nan-quiet? nan-payload nan=?)
  (begin

    (define %payload-bits 51)
    (define %payload-mask (- (expt 2 %payload-bits) 1))
    (define %quiet-bit    (expt 2 %payload-bits))
    (define %sign-bit     (expt 2 63))
    (define %exponent-field (arithmetic-shift 2047 52))

    (define (%check-nan who nan)
      (if (not (nan? nan))
          (error (string-append who ": not a NaN") nan)))

    (define (make-nan negative? quiet? payload . maybe-float)
      (if (not (pair? maybe-float))
          (if #f #f)
          (if (not (and (real? (car maybe-float)) (inexact? (car maybe-float))))
              (error "make-nan: float argument must be an inexact real" (car maybe-float))))
      (if (not (and (integer? payload) (exact? payload) (> payload 0)))
          (error "make-nan: payload must be a positive exact integer" payload))
      (if (> payload %payload-mask)
          (error "make-nan: payload is larger than a NaN can hold" payload))
      (%raw-bits->flonum
        (+ (if negative? %sign-bit 0)
           %exponent-field
           (if quiet? %quiet-bit 0)
           payload)))

    (define (nan-negative? nan)
      (%check-nan "nan-negative?" nan)
      (not (zero? (bitwise-and (%flonum-raw-bits nan) %sign-bit))))

    (define (nan-quiet? nan)
      (%check-nan "nan-quiet?" nan)
      (not (zero? (bitwise-and (%flonum-raw-bits nan) %quiet-bit))))

    (define (nan-payload nan)
      (%check-nan "nan-payload" nan)
      (bitwise-and (%flonum-raw-bits nan) %payload-mask))

    (define (nan=? nan1 nan2)
      (%check-nan "nan=?" nan1)
      (%check-nan "nan=?" nan2)
      (and (eq? (nan-negative? nan1) (nan-negative? nan2))
           (eq? (nan-quiet? nan1) (nan-quiet? nan2))
           (= (nan-payload nan1) (nan-payload nan2))))))
