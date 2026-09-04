;;; image_tests.scm — (curry image) basic correctness + issue #167
;;; regression (forged image vector causes an OOB heap read/write).
;;;
;;; No dedicated test file existed for this module before.

(import (scheme base) (curry image))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

;;; ── Basic correctness ────────────────────────────────────────────────

(define img (image-make 4 4 3))
(check "image-width"    (image-width img)    4)
(check "image-height"   (image-height img)   4)
(check "image-channels" (image-channels img) 3)
(image-set! img 1 2 0 200)
(check "image-ref after image-set!" (image-ref img 1 2 0) 200)
(check "image-ref default is 0" (image-ref img 0 0 0) 0)
(check "image-pixels length" (bytevector-length (image-pixels img)) (* 4 4 3))

;;; ── Issue #167: forged image vector causes an OOB heap read/write ───
;;;
;;; check_image previously only verified slot 0 was a fixnum -- not even
;;; that the argument was a vector at all (curry_vector_ref does an
;;; unchecked cast), and never that slot 3 was really a bytevector or
;;; that its length matched the claimed width*height*channels. Confirmed
;;; reproducible SIGSEGV (an out-of-bounds heap WRITE, not just a read)
;;; pre-fix via a forged (vector 100000 100000 4 tiny-string).
(check "image-width rejects a non-vector argument (was a reproducible SIGSEGV)"
  (raises? (lambda () (image-width 42))) #t)
(check "image-set! rejects a forged vector with an oversized claimed size (was a reproducible SIGSEGV -- OOB write)"
  (raises? (lambda ()
             (image-set! (vector 100000 100000 4 (make-string 1 #\a)) 99999 99999 3 255)))
  #t)
(check "image-ref rejects a forged vector with a wrong-typed slot 3 (was a reproducible SIGSEGV)"
  (raises? (lambda () (image-ref (vector 10 10 3 42) 0 0 0))) #t)
(check "image-set! rejects a real bytevector that's too short for the claimed dimensions"
  (raises? (lambda ()
             (image-set! (vector 100 100 4 (make-bytevector 4 0)) 99 99 3 255)))
  #t)
(check "image-ref rejects dimensions that overflow width*height*channels"
  (raises? (lambda ()
             (image-ref (vector 4294967295 4294967295 4 (make-bytevector 16 0)) 0 0 0)))
  #t)
(check "image-ref rejects a wrong-length vector (not 4 elements)"
  (raises? (lambda () (image-ref (vector 10 10 3) 0 0 0))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
