;;; string->number accepts the radix and exactness prefixes (#392).
;;;
;;; R7RS 6.2.1: #b #o #d #x set the radix, #e #i the exactness, at most one of
;;; each, in either order. The reader accepted them in source, but
;;; (string->number "#x10") was #f.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (s->n s . r) (apply string->number s r))

;;; radix prefixes
(check "#x10" (s->n "#x10") 16)
(check "#xFF" (s->n "#xFF") 255)
(check "#xff lower case" (s->n "#xff") 255)
(check "#X upper-case prefix" (s->n "#XFF") 255)
(check "#b101" (s->n "#b101") 5)
(check "#o17" (s->n "#o17") 15)
(check "#d10" (s->n "#d10") 10)
(check "negative: #x-ff" (s->n "#x-ff") -255)
(check "ratio: #x1/2" (s->n "#x1/2") 1/2)
(check "a bignum: #xFFFFFFFFFFFFFFFFFFFF" (s->n "#xFFFFFFFFFFFFFFFFFFFF") 1208925819614629174706175)
(check "the prefix wins over the radix argument" (s->n "#x10" 2) 16)
(check "the radix argument still works" (s->n "10" 2) 2)

;;; exactness prefixes
(check "#e1.5" (s->n "#e1.5") 3/2)
(check "#e1.1 is 11/10, not the double nearest 1.1" (s->n "#e1.1") 11/10)
(check "#e0.1" (s->n "#e0.1") 1/10)
(check "#e-1.25" (s->n "#e-1.25") -5/4)
(check "#e.5" (s->n "#e.5") 1/2)
(check "#e5." (s->n "#e5.") 5)
(check "#e1e3" (s->n "#e1e3") 1000)
(check "#e1e-2" (s->n "#e1e-2") 1/100)
(check "#e1.5e3" (s->n "#e1.5e3") 1500)
(check "#e1/2" (s->n "#e1/2") 1/2)
(check "#e10" (s->n "#e10") 10)
(check "#e on a long decimal"
       (s->n "#e123456789012345678901234567890.5") 246913578024691357802469135781/2)
(check "#e1+2i" (s->n "#e1+2i") (make-rectangular 1 2))
(check "#e1.5+2.5i" (s->n "#e1.5+2.5i") (make-rectangular 3/2 5/2))
(check "#i1/2" (s->n "#i1/2") 0.5)
(check "#i10" (s->n "#i10") 10.0)
(check "#i1.5" (s->n "#i1.5") 1.5)
(check "#i#x10" (s->n "#i#x10") 16.0)

;;; both prefixes, either order
(check "#e#x10" (s->n "#e#x10") 16)
(check "#x#e10" (s->n "#x#e10") 16)
(check "#x#i1/2" (s->n "#x#i1/2") 0.5)
(check "#i#b101" (s->n "#i#b101") 5.0)

;;; not a number
(check "a repeated radix prefix" (s->n "#x#x10") #f)
(check "both exactness prefixes" (s->n "#e#i1") #f)
(check "an unknown prefix" (s->n "#z10") #f)
(check "a lone #" (s->n "#") #f)
(check "a prefix and nothing else" (s->n "#x") #f)
(check "a digit outside the radix" (s->n "#xg") #f)
(check "#b2" (s->n "#b2") #f)
(check "a prefix with no number" (s->n "#e") #f)
(check "#e." (s->n "#e.") #f)
(check "two points: #e1.5.2" (s->n "#e1.5.2") #f)
(check "an incomplete exponent: #e1e" (s->n "#e1e") #f)
(check "#e on infinity is #f, not an error" (s->n "#e+inf.0") #f)
(check "#e on NaN" (s->n "#e+nan.0") #f)
(check "#e on an exponent too large for a flonum" (s->n "#e1e100000") #f)
(check "a prefix on something that is not a number" (s->n "#ifoo") #f)

;;; #e on a complex number: each part is exact decimal, and an infinity or NaN
;;; in either part is #f, not an error
(check "#e1.1+2.2i" (s->n "#e1.1+2.2i") (make-rectangular 11/10 11/5))
(check "#e1.1i" (s->n "#e1.1i") (make-rectangular 0 11/10))
(check "#e-0.1-0.2i" (s->n "#e-0.1-0.2i") (make-rectangular -1/10 -1/5))
(check "#e1e999+1i is exact, not an error" (exact? (real-part (s->n "#e1e999+1i"))) #t)
(check "#e1e999+1i real part" (real-part (s->n "#e1e999+1i")) (expt 10 999))
(check "#e1e100000+1i" (s->n "#e1e100000+1i") #f)
(check "#e1e100000i" (s->n "#e1e100000i") #f)
(check "#e1+1e100000i" (s->n "#e1+1e100000i") #f)
(check "#e+inf.0+1i" (s->n "#e+inf.0+1i") #f)
(check "#e+nan.0i" (s->n "#e+nan.0i") #f)
(check "#e1+nan.0i" (s->n "#e1+nan.0i") #f)

;;; exponents too large to build an exact number from
(check "#e1e10000 is built" (exact? (s->n "#e1e10000")) #t)
(check "#e1e-10000 is built" (exact? (s->n "#e1e-10000")) #t)
(check "#e1e100000 is #f" (s->n "#e1e100000") #f)
(check "#e1e-100000 is #f, not 0" (s->n "#e1e-100000") #f)

;;; signs and exponents alone are not numbers
(check "+" (s->n "+") #f)
(check "-" (s->n "-") #f)
(check "e5" (s->n "e5") #f)
(check "#e1e+" (s->n "#e1e+") #f)
(check "#e1e5.5" (s->n "#e1e5.5") #f)

;;; a decimal point exists only in radix 10
(check "2 in radix 2 is not a number, not 2.0" (s->n "2" 2) #f)
(check "9 in radix 8" (s->n "9" 8) #f)
(check "1.5 in radix 16" (s->n "1.5" 16) #f)
(check "1e3 in radix 16 is the hex number" (s->n "1e3" 16) 483)
(check "#o8" (s->n "#o8") #f)

;;; unchanged
(check "no prefix: 42" (s->n "42") 42)
(check "no prefix: 1.5" (s->n "1.5") 1.5)
(check "no prefix: ratio" (s->n "1/2") 1/2)
(check "no prefix: not a number" (s->n "abc") #f)
(check "no prefix: empty" (s->n "") #f)
(check "the reader still reads #x10" #x10 16)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
