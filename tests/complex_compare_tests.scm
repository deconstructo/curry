;;; = on complex numbers, and the ordering predicates refusing them (#379).
;;;
;;; (= 1+2i 1+2i) used to raise "exact integer required, got complex", and
;;; (= 1+2i 1.) was #t because only the real part was compared.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))

;;; Equality ─────────────────────────────────────────────────────────────────
(check "exact complex, equal" (= 1+2i 1+2i) #t)
(check "exact complex, different imaginary part" (= 1+2i 1+3i) #f)
(check "exact complex, different real part" (= 1+2i 2+2i) #f)
(check "flonum complex, equal" (= 1.+2.i 1.+2.i) #t)
(check "flonum complex, different" (= 1.+2.i 1.+3.i) #f)
(check "exact and flonum parts compare by value" (= 1+2i 1.+2.i) #t)
(check "complex against an exact real" (= 1+2i 1) #f)
(check "an exact real against a complex" (= 1 1+2i) #f)
(check "complex against a flonum (was #t: the imaginary part was ignored)" (= 1+2i 1.) #f)
(check "a flonum against a complex" (= 1. 1.+2.i) #f)
(check "a real number against its own complex form" (= 3 (make-rectangular 3 0)) #t)
(check "rational parts" (= (make-rectangular 1/2 1/3) (make-rectangular 1/2 1/3)) #t)
(check "rational parts, different" (= (make-rectangular 1/2 1/3) (make-rectangular 1/2 1/4)) #f)
(check "bignum parts"
       (= (make-rectangular (expt 10 30) 1) (make-rectangular (expt 10 30) 1)) #t)
(check "chain, all equal" (= 1+2i 1+2i 1+2i) #t)
(check "chain, the last differs" (= 1+2i 1+2i 3) #f)
(check "chain, the first differs" (= 3 1+2i 1+2i) #f)
(check "eqv? and equal? are unchanged" (list (eqv? 1+2i 1+2i) (equal? 1+2i 1+2i)) '(#t #t))
(check "zero? is unchanged" (list (zero? 0+1i) (zero? 0.+0.i)) '(#f #t))

;;; make-rectangular accepts complex parts, so a value can nest very deeply;
;;; comparing two of them raises a catchable error instead of overflowing the
;;; C stack.
(define deep
  (let loop ((i 0) (z 1)) (if (< i 200000) (loop (+ i 1) (make-rectangular z 1)) z)))
(check "= on a very deeply nested complex does not crash"
       (guard (e (#t 'raised-or-answered)) (= deep deep) 'raised-or-answered)
       'raised-or-answered)

;;; Ordering is not defined ──────────────────────────────────────────────────
(check "< raises for a complex operand" (raises? (lambda () (< 1+2i 3))) #t)
(check "< raises for a complex second operand" (raises? (lambda () (< 3 1+2i))) #t)
(check "> raises" (raises? (lambda () (> 1+2i 1+3i))) #t)
(check "<= raises" (raises? (lambda () (<= 1+2i 1+2i))) #t)
(check ">= raises" (raises? (lambda () (>= 1.+2.i 1.))) #t)
(check "a chain raises even if an earlier pair is already false"
       (raises? (lambda () (< 5 1 1+2i))) #t)
(check "a chain raises when the complex operand is in the middle"
       (raises? (lambda () (< 1 1+2i 5))) #t)
(check "max and min still raise" (list (raises? (lambda () (max 1+2i 3)))
                                       (raises? (lambda () (min 1+2i 3)))) '(#t #t))
(check "ordering real numbers is unchanged" (list (< 1 2 3) (> 3 2 1) (<= 1 1 2) (>= 2 2 1) (< 1 2.5) (> 1/2 1/3))
       '(#t #t #t #t #t #t))

;;; A compiled (and, with the JIT, native) function behaves the same ─────────
(define (same? a b) (= a b))
(define (less? a b) (< a b))
(define (loop n acc)
  (if (= n 0)
      acc
      (loop (- n 1) (and (same? 1+2i 1+2i) (not (same? 1+2i 1.)) (not (raises? (lambda () (less? 1 2)))) acc))))
(check "after 200 calls, equality still right" (loop 200 #t) #t)
(check "after 200 calls, the ordering still raises"
       (raises? (lambda () (less? 1+2i 2))) #t)

;;; Summary ──────────────────────────────────────────────────────────────────
(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
