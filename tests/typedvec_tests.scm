;;; typedvec_tests.scm — SRFI-4 core typed vectors: (curry typedvec) directly,
;;; plus the combined (srfi 4) / (srfi srfi-4) / (srfi s4 uniform-vectors)
;;; wrapper libraries (which also re-export the pre-existing f64vector kind).

(import (curry typedvec))
(import (srfi 4))

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

;;; ---- u8vector: construction, ref/set!, bounds ----

(define v8 (u8vector 1 2 3))
(check "u8vector?" (u8vector? v8) #t)
(check "u8vector? on wrong kind" (u8vector? (s8vector 1)) #f)
(check "u8vector-length" (u8vector-length v8) 3)
(check "u8vector-ref" (u8vector-ref v8 1) 2)
(u8vector-set! v8 1 99)
(check "u8vector-set!" (u8vector-ref v8 1) 99)
(check "u8vector->list" (u8vector->list (u8vector 5 6 7)) '(5 6 7))
(check "list->u8vector" (u8vector->list (list->u8vector '(9 8 7))) '(9 8 7))
(check "make-u8vector default fill" (u8vector->list (make-u8vector 3)) '(0 0 0))
(check "make-u8vector with fill" (u8vector->list (make-u8vector 3 7)) '(7 7 7))

(check "u8vector-set! out of range high raises"
       (guard (e (#t 'raised)) (u8vector-set! v8 0 300))
       'raised)
(check "u8vector-set! out of range low raises"
       (guard (e (#t 'raised)) (u8vector-set! v8 0 -1))
       'raised)
(check "u8vector-ref out of bounds raises"
       (guard (e (#t 'raised)) (u8vector-ref v8 99))
       'raised)

;;; ---- s8vector: signed boundary values ----

(define v8s (s8vector -128 0 127))
(check "s8vector min boundary" (s8vector-ref v8s 0) -128)
(check "s8vector max boundary" (s8vector-ref v8s 2) 127)
(check "s8vector-set! overflow raises"
       (guard (e (#t 'raised)) (s8vector-set! v8s 0 128))
       'raised)
(check "s8vector-set! underflow raises"
       (guard (e (#t 'raised)) (s8vector-set! v8s 0 -129))
       'raised)

;;; ---- u16/s16 ----

(check "u16vector max" (u16vector-ref (u16vector 65535) 0) 65535)
(check "s16vector min/max" (s16vector->list (s16vector -32768 32767)) '(-32768 32767))

;;; ---- u32/s32 ----

(check "u32vector max" (u32vector-ref (u32vector 4294967295) 0) 4294967295)
(check "s32vector min/max"
       (s32vector->list (s32vector -2147483648 2147483647))
       '(-2147483648 2147483647))

;;; ---- u64/s64: exact-bignum round-tripping past signed-long range ----

(define big-u64 18446744073709551615) ; UINT64_MAX
(define v64u (u64vector big-u64 0))
(check "u64vector round-trips UINT64_MAX exactly" (u64vector-ref v64u 0) big-u64)
(check "u64vector element is exact integer" (integer? (u64vector-ref v64u 0)) #t)

(define v64s (s64vector -9223372036854775808 9223372036854775807))
(check "s64vector round-trips INT64_MIN" (s64vector-ref v64s 0) -9223372036854775808)
(check "s64vector round-trips INT64_MAX" (s64vector-ref v64s 1) 9223372036854775807)

;;; ---- copy / copy! / append / fill! ----

(check "u8vector-copy" (u8vector->list (u8vector-copy (u8vector 1 2 3 4) 1 3)) '(2 3))
(let ((dst (make-u8vector 4 0)))
  (u8vector-copy! dst 1 (u8vector 10 20 30))
  (check "u8vector-copy!" (u8vector->list dst) '(0 10 20 30)))
(check "u8vector-append"
       (u8vector->list (u8vector-append (u8vector 1 2) (u8vector 3 4)))
       '(1 2 3 4))
(let ((v (make-u8vector 3 0)))
  (u8vector-fill! v 5)
  (check "u8vector-fill!" (u8vector->list v) '(5 5 5)))

;;; ---- external representation (#u8vec(...), #s64vec(...), etc.) ----
;;; Deliberately NOT "#u8(...)"/"#s8(...)": those exact prefixes are
;;; already claimed by the reader (R7RS bytevector syntax and the
;;; sexagesimal-literal reader, respectively) -- reusing them would make
;;; write/read round-trip to the wrong type or silently corrupt. The
;;; "vec" suffix avoids both collisions; see src/port.c's comment.

(check "u8vector write representation"
       (let ((p (open-output-string)))
         (write (u8vector 1 2 3) p)
         (get-output-string p))
       "#u8vec(1 2 3)")
(check "s64vector write representation round-trips boundary values"
       (let ((p (open-output-string)))
         (write (s64vector -9223372036854775808 9223372036854775807) p)
         (get-output-string p))
       "#s64vec(-9223372036854775808 9223372036854775807)")
(check "u64vector write representation for UINT64_MAX"
       (let ((p (open-output-string)))
         (write (u64vector 18446744073709551615) p)
         (get-output-string p))
       "#u64vec(18446744073709551615)")

;;; ---- printed representation no longer collides with reader syntax ----
;;; (regression: found by code review -- the original "#u8(...)"/
;;; "#s8(...)" forms collided with R7RS bytevector syntax and the
;;; sexagesimal-literal reader, so writing a u8vector and reading it
;;; back silently produced a *bytevector*, and writing an s8vector..
;;; s64vector and reading it back silently corrupted the read stream
;;; instead of raising.)

(check "#u8(...) still reads as a bytevector, not confused with u8vector"
       (bytevector? (read (open-input-string "#u8(1 2 3)")))
       #t)
(check "#u8vec(...) is not (yet) reader syntax -- raises cleanly instead of misparsing"
       (guard (e (#t 'raised)) (read (open-input-string "#u8vec(1 2 3)")))
       'raised)
(check "#s8vec(...) is not (yet) reader syntax -- raises cleanly instead of silently corrupting"
       (guard (e (#t 'raised)) (read (open-input-string "#s8vec(1 2 3)")))
       'raised)

;;; ---- input validation regressions found by independent review ----

(check "TAGvector-copy! end argument must be a fixnum, not silently reinterpreted"
       (guard (e (#t 'raised))
         (let ((to (make-u8vector 5 0)) (from (u8vector 9 9 9)))
           (u8vector-copy! to 0 from 0 #t)))
       'raised)
(check "make-TAGvector rejects a length exceeding uint32_t range instead of silently truncating"
       (guard (e (#t 'raised)) (make-u8vector 4294967297 7))
       'raised)

;;; ---- (srfi 4) re-exports both the 8 integer kinds and f64vector ----

(check "(srfi 4) exports u8vector" (u8vector? (u8vector 1)) #t)
(check "(srfi 4) exports f64vector" (f64vector? (f64vector 1.5 2.5)) #t)
(check "(srfi 4) f64vector-ref" (f64vector-ref (f64vector 1.5 2.5) 1) 2.5)

;;; ---- Summary ----

(newline)
(display "typedvec/srfi-4 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
