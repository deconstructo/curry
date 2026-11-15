;;; The reader's #e is exact for decimal text (#400), and exact?/inexact? know
;;; about complex numbers (#401).
;;;
;;; #e1.1 was read as the double nearest 1.1 made exact, 2476979795053773/
;;; 2251799813685248, not 11/10. (exact? 1+2i) was #f, and so was (inexact? 1+2i):
;;; the predicates only looked at real types.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;;; ---- #e in source (#400) ----
(check "#e1.1" #e1.1 11/10)
(check "#e0.1" #e0.1 1/10)
(check "#e1.5" #e1.5 3/2)
(check "#e-1.25" #e-1.25 -5/4)
(check "#e.5" #e.5 1/2)
(check "#e1e3" #e1e3 1000)
(check "#e1e-2" #e1e-2 1/100)
(check "#e1/2" #e1/2 1/2)
(check "#e10" #e10 10)
(check "#e+1.1" #e+1.1 11/10)
(check "#e1.1+2.2i" (real-part #e1.1+2.2i) 11/10)
(check "#e1.1+2.2i imaginary part" (imag-part #e1.1+2.2i) 11/5)
(check "#e#x10" #e#x10 16)
(check "#x#e10" #x#e10 16)
(check "#x#i1/2" #x#i1/2 0.5)
(check "#b#e101" #b#e101 5)
(check "#e in a list" '(#e1.1 #e2.5) '(11/10 5/2))
(check "#e is exact" (exact? #e1.1) #t)
(check "the reader and string->number agree on #e1.1" #e1.1 (string->number "#e1.1"))
(check "#i is still inexact" #i1/3 (/ 1.0 3))
(check "a plain decimal is still a flonum" 1.1 (exact->inexact 11/10))

;;; ---- exact? and inexact? on complex numbers (#401) ----
(check "exact? 1+2i" (exact? 1+2i) #t)
(check "exact? 1/2+3i" (exact? 1/2+3i) #t)
(check "exact? of make-rectangular with exact parts" (exact? (make-rectangular 1 2)) #t)
(check "exact? of #e1+2i" (exact? (string->number "#e1+2i")) #t)
(check "exact? 1.0+2i" (exact? 1.0+2i) #f)
(check "exact? 1+2.5i" (exact? 1+2.5i) #f)
(check "exact? 1.5+2.5i" (exact? 1.5+2.5i) #f)
(check "inexact? 1+2i" (inexact? 1+2i) #f)
(check "inexact? 1.0+2i" (inexact? 1.0+2i) #t)
(check "inexact? 1+2.5i" (inexact? 1+2.5i) #t)
(check "inexact? 1.5+2.5i" (inexact? 1.5+2.5i) #t)
(check "exact? of the inexact conversion" (exact? (inexact 1+2i)) #f)
(check "inexact? of the exact conversion" (inexact? (exact 1.5+2.5i)) #f)
(check "exact? of the exact conversion" (exact? (exact 1.5+2.5i)) #t)

;;; ---- unchanged for the real types ----
(check "exact? 1" (exact? 1) #t)
(check "exact? 1/2" (exact? 1/2) #t)
(check "exact? a bignum" (exact? (expt 10 30)) #t)
(check "exact? 1.5" (exact? 1.5) #f)
(check "inexact? 1.5" (inexact? 1.5) #t)
(check "inexact? 1" (inexact? 1) #f)
(check "exact? of a non-number" (exact? "x") #f)
(check "inexact? of a non-number" (inexact? 'a) #f)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
