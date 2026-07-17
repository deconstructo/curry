;;; FITS reader/writer tests — (curry fits)
;;;
;;; Self-contained: writes fixtures with fits-write-image, reads them back
;;; with fits-read-image, and checks values/headers. No external files or
;;; tools needed (FITS has no widely-installed CLI reader to cross-check
;;; against the way ncdump/h5dump exist for the other formats).

(import (curry fits))

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

(define (check-num label got expected)
  (check label (= got expected) #t))

(define (tmppath name) (string-append "/tmp/" name))

;;; ════════════════════════════════════════════════════════════
;;; § 1  2-D roundtrip
;;; ════════════════════════════════════════════════════════════

(let ((path (tmppath "curry-fits-test-2d.fits"))
      (t (make-tensor (list 2 3) 0.0)))
  (tensor-set! t 0 0 1.5) (tensor-set! t 0 1 2.5) (tensor-set! t 0 2 3.5)
  (tensor-set! t 1 0 4.5) (tensor-set! t 1 1 5.5) (tensor-set! t 1 2 6.5)
  (fits-write-image path t)
  (call-with-values
    (lambda () (fits-read-image path))
    (lambda (t2 header)
      (check "2d shape" (tensor-shape t2) (list 2 3))
      (check-num "2d [0,0]" (tensor-ref t2 0 0) 1.5)
      (check-num "2d [0,2]" (tensor-ref t2 0 2) 3.5)
      (check-num "2d [1,0]" (tensor-ref t2 1 0) 4.5)
      (check-num "2d [1,2]" (tensor-ref t2 1 2) 6.5)
      (check "2d BITPIX" (fits-header-ref header "BITPIX") -64)
      (check "2d NAXIS" (fits-header-ref header "NAXIS") 2))))

;;; ════════════════════════════════════════════════════════════
;;; § 2  1-D roundtrip, negative/zero/large values
;;; ════════════════════════════════════════════════════════════

(let ((path (tmppath "curry-fits-test-1d.fits"))
      (t (make-tensor (list 3) 0.0)))
  (tensor-set! t 0 -1.25) (tensor-set! t 1 0.0) (tensor-set! t 2 1000000.5)
  (fits-write-image path t)
  (call-with-values
    (lambda () (fits-read-image path))
    (lambda (t2 header)
      (check-num "1d negative" (tensor-ref t2 0) -1.25)
      (check-num "1d zero" (tensor-ref t2 1) 0.0)
      (check-num "1d large" (tensor-ref t2 2) 1000000.5))))

;;; ════════════════════════════════════════════════════════════
;;; § 3  3-D roundtrip (axis-order correctness)
;;; ════════════════════════════════════════════════════════════

(let ((path (tmppath "curry-fits-test-3d.fits"))
      (t (make-tensor (list 2 3 4) 0.0)))
  (let loop ((i 0))
    (when (< i 24)
      (let* ((a (quotient i 12)) (b (quotient (remainder i 12) 4)) (c (remainder i 4)))
        (tensor-set! t a b c (exact->inexact i)))
      (loop (+ i 1))))
  (fits-write-image path t)
  (call-with-values
    (lambda () (fits-read-image path))
    (lambda (t2 header)
      (check "3d shape" (tensor-shape t2) (list 2 3 4))
      (check-num "3d [0,0,0]" (tensor-ref t2 0 0 0) 0)
      (check-num "3d [1,2,3]" (tensor-ref t2 1 2 3) 23)
      (check-num "3d [0,1,2]" (tensor-ref t2 0 1 2) 6))))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Custom header cards
;;; ════════════════════════════════════════════════════════════

(let ((path (tmppath "curry-fits-test-header.fits"))
      (t (make-tensor (list 2) 0.0)))
  (tensor-set! t 0 1.0) (tensor-set! t 1 2.0)
  (fits-write-image path t (quote #:header)
    (list (cons "OBJECT" "M31") (cons "EXPTIME" 30) (cons "SIMPLE2" #t)))
  (call-with-values
    (lambda () (fits-read-image path))
    (lambda (t2 header)
      (check "header OBJECT" (fits-header-ref header "OBJECT") "M31")
      (check "header EXPTIME" (fits-header-ref header "EXPTIME") 30)
      (check "header boolean" (fits-header-ref header "SIMPLE2") #t)
      (check "header case-insensitive" (fits-header-ref header "object") "M31")
      (check "header missing default"
             (fits-header-ref header "NOPE" (quote missing)) (quote missing)))))

;;; ════════════════════════════════════════════════════════════
;;; § 5  Overlong keyword is rejected, not silently corrupted
;;; ════════════════════════════════════════════════════════════

(let ((path (tmppath "curry-fits-test-badkey.fits"))
      (t (make-tensor (list 1) 0.0)))
  (check "overlong keyword raises"
    (guard (e (#t #t))
      (fits-write-image path t (quote #:header) (list (cons "SIMULATED" #t)))
      #f)
    #t))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
