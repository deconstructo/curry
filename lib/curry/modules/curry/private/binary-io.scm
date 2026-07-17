;;; (curry private binary-io) — shared big-endian / IEEE-754 decode helpers.
;;;
;;; Internal support module for (curry fits) and (curry netcdf); not a
;;; public API (all names %-prefixed by convention, curry has no formal
;;; export lists so this is enforced by naming only, same as elsewhere
;;; in the codebase).
;;;
;;; API:
;;;   (%bv-u16be bv i)   (%bv-u32be bv i)   (%bv-u64be bv i)
;;;   (%bv-s16be bv i)   (%bv-s32be bv i)   (%bv-s64be bv i)   -- signed, two's complement
;;;   (%bv-f32be bv i)   (%bv-f64be bv i)   -- IEEE-754 single/double
;;;   (%read-exact-bytes port n) -> bytevector, raises on short read (EOF)
;;;   (%pad-to-multiple n mult)  -> smallest k >= n with (zero? (remainder k mult))

;; ── Big-endian unsigned integer decode ──────────────────────────────────────

(define (%bv-u16be bv i)
  (+ (arithmetic-shift (bytevector-u8-ref bv i) 8)
     (bytevector-u8-ref bv (+ i 1))))

(define (%bv-u32be bv i)
  (+ (arithmetic-shift (bytevector-u8-ref bv i) 24)
     (arithmetic-shift (bytevector-u8-ref bv (+ i 1)) 16)
     (arithmetic-shift (bytevector-u8-ref bv (+ i 2)) 8)
     (bytevector-u8-ref bv (+ i 3))))

(define (%bv-u64be bv i)
  (+ (arithmetic-shift (%bv-u32be bv i) 32)
     (%bv-u32be bv (+ i 4))))

;; ── Big-endian signed (two's complement) integer decode ─────────────────────

(define (%bv-s16be bv i)
  (let ((u (%bv-u16be bv i)))
    (if (>= u #x8000) (- u #x10000) u)))

(define (%bv-s32be bv i)
  (let ((u (%bv-u32be bv i)))
    (if (>= u #x80000000) (- u #x100000000) u)))

(define (%bv-s64be bv i)
  (let ((u (%bv-u64be bv i)))
    (if (>= u #x8000000000000000) (- u #x10000000000000000) u)))

;; ── IEEE-754 decode ──────────────────────────────────────────────────────────
;;
;; Reconstructed via exact rational arithmetic (sign * mantissa * 2^exp),
;; converted to inexact at the very end — curry's bignum tower handles the
;; 52-bit double mantissa without overflow, so no native bit-to-double
;; primitive is needed.

;; single precision: 1 sign / 8 exponent (bias 127) / 23 mantissa
(define (%bv-f32be bv i)
  (let* ((bits (%bv-u32be bv i))
         (sign (if (zero? (arithmetic-shift bits -31)) 1 -1))
         (expo (bitwise-and (arithmetic-shift bits -23) #xFF))
         (mant (bitwise-and bits #x7FFFFF)))
    (exact->inexact
      (cond
        ((and (= expo 0) (= mant 0)) (* sign 0))
        ((= expo #xFF)
         (if (= mant 0)
             (if (= sign 1) (/ 1.0 0.0) (/ -1.0 0.0))   ; ±inf
             (/ 0.0 0.0)))                               ; NaN
        ((= expo 0)
         ;; subnormal: sign * mant/2^23 * 2^-126
         (* sign (/ mant (expt 2 23)) (expt 2 -126)))
        (else
         ;; normal: sign * (1 + mant/2^23) * 2^(expo-127)
         (* sign (+ 1 (/ mant (expt 2 23))) (expt 2 (- expo 127))))))))

;; double precision: 1 sign / 11 exponent (bias 1023) / 52 mantissa
(define (%bv-f64be bv i)
  (let* ((bits (%bv-u64be bv i))
         (sign (if (zero? (arithmetic-shift bits -63)) 1 -1))
         (expo (bitwise-and (arithmetic-shift bits -52) #x7FF))
         (mant (bitwise-and bits #xFFFFFFFFFFFFF)))
    (exact->inexact
      (cond
        ((and (= expo 0) (= mant 0)) (* sign 0))
        ((= expo #x7FF)
         (if (= mant 0)
             (if (= sign 1) (/ 1.0 0.0) (/ -1.0 0.0))
             (/ 0.0 0.0)))
        ((= expo 0)
         (* sign (/ mant (expt 2 52)) (expt 2 -1022)))
        (else
         (* sign (+ 1 (/ mant (expt 2 52))) (expt 2 (- expo 1023))))))))

;; ── Port helpers ─────────────────────────────────────────────────────────────

;; Read exactly n bytes; raises on short read (truncated/corrupt file)
;; rather than silently returning a short bytevector.
(define (%read-exact-bytes port n)
  (let ((bv (read-bytevector n port)))
    (if (or (eof-object? bv) (< (bytevector-length bv) n))
        (error "binary-io: unexpected EOF (wanted" n "bytes)")
        bv)))

(define (%pad-to-multiple n mult)
  (* mult (ceiling (/ n mult))))
