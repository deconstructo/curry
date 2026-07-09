;;; sets_module_tests.scm — tests for (curry sets): multisets and logical sets

(import (curry logic))
(import (curry sets))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define-syntax check-approx
  (syntax-rules ()
    ((_ label expr expected eps)
     (let ((got expr))
       (if (< (abs (- got expected)) eps)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected ≈ ") (write expected) (newline)
             (display "  got:       ") (write got) (newline)))))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Multisets
;;; ══════════════════════════════════════════════════════════════════════════

;;; Construction and access
(let ((ms (list->multiset '(a b a c a b))))
  (check "multiset-count a"      (multiset-count ms 'a) 3)
  (check "multiset-count b"      (multiset-count ms 'b) 2)
  (check "multiset-count c"      (multiset-count ms 'c) 1)
  (check "multiset-count absent" (multiset-count ms 'z) 0)
  (check "multiset-member? a"    (multiset-member? ms 'a) #t)
  (check "multiset-member? z"    (multiset-member? ms 'z) #f)
  (check "multiset-size"         (multiset-size ms) 3)
  (check "multiset-total"        (multiset-total ms) 6))

(check "multiset-empty? empty"     (multiset-empty? (make-multiset)) #t)
(check "multiset-empty? non-empty" (multiset-empty? (multiset 'x)) #f)

;;; multiset=?
(check "multiset=? equal"
  (multiset=? (list->multiset '(a a b)) (list->multiset '(a b a))) #t)
(check "multiset=? different counts"
  (multiset=? (list->multiset '(a a b)) (list->multiset '(a b))) #f)
(check "multiset=? different elements"
  (multiset=? (list->multiset '(a b)) (list->multiset '(a c))) #f)

;;; Mutation
(let ((ms (list->multiset '(a b))))
  (multiset-add! ms 'a)
  (check "multiset-add!"    (multiset-count ms 'a) 2)
  (multiset-add-n! ms 'b 3)
  (check "multiset-add-n!"  (multiset-count ms 'b) 4)
  (multiset-remove! ms 'a)
  (check "multiset-remove!" (multiset-count ms 'a) 1)
  (multiset-remove-all! ms 'b)
  (check "multiset-remove-all!" (multiset-member? ms 'b) #f))

;;; Non-destructive variants
(let* ((ms (list->multiset '(a a b)))
       (ms2 (multiset-add ms 'b)))
  (check "multiset-add non-destructive: new" (multiset-count ms2 'b) 2)
  (check "multiset-add non-destructive: old" (multiset-count ms  'b) 1))

;;; multiset-subset?
(check "multiset-subset? true"
  (multiset-subset? (list->multiset '(a a b))
                    (list->multiset '(a a a b b))) #t)
(check "multiset-subset? false"
  (multiset-subset? (list->multiset '(a a a))
                    (list->multiset '(a a b)))   #f)

;;; Algebra
(let ((a (list->multiset '(a a b)))
      (b (list->multiset '(a b b c))))
  (check "multiset-union: a"
    (multiset-count (multiset-union a b) 'a) 2)      ; max(2,1) = 2
  (check "multiset-union: b"
    (multiset-count (multiset-union a b) 'b) 2)      ; max(1,2) = 2
  (check "multiset-union: c"
    (multiset-count (multiset-union a b) 'c) 1)      ; max(0,1) = 1

  (check "multiset-intersection: a"
    (multiset-count (multiset-intersection a b) 'a) 1) ; min(2,1) = 1
  (check "multiset-intersection: b"
    (multiset-count (multiset-intersection a b) 'b) 1) ; min(1,2) = 1
  (check "multiset-intersection: c"
    (multiset-count (multiset-intersection a b) 'c) 0) ; min(0,1) = 0

  (check "multiset-sum: a"
    (multiset-count (multiset-sum a b) 'a) 3)          ; 2+1 = 3
  (check "multiset-sum: b"
    (multiset-count (multiset-sum a b) 'b) 3)          ; 1+2 = 3

  (check "multiset-difference: a"
    (multiset-count (multiset-difference a b) 'a) 1)   ; max(0, 2-1) = 1
  (check "multiset-difference: b"
    (multiset-count (multiset-difference a b) 'b) 0)   ; max(0, 1-2) = 0
  (check "multiset-difference: c"
    (multiset-member? (multiset-difference a b) 'c) #f)); max(0, 0-1) = 0

(check "multiset-scale"
  (multiset-count (multiset-scale (list->multiset '(a a b)) 3) 'a) 6)

;;; Higher-order
(let ((ms (list->multiset '(a a b c))))
  (check "multiset-fold: total"
    (multiset-fold (lambda (acc elem count) (+ acc count)) 0 ms)
    4))

(check "multiset-any? true"  (multiset-any? (lambda (e c) (> c 1)) (list->multiset '(a a b))) #t)
(check "multiset-any? false" (multiset-any? (lambda (e c) (> c 1)) (list->multiset '(a b c))) #f)
(check "multiset-every? true"
  (multiset-every? (lambda (e c) (> c 0)) (list->multiset '(a a b))) #t)
(check "multiset-every? false"
  (multiset-every? (lambda (e c) (> c 1)) (list->multiset '(a a b))) #f)

(check "multiset-filter: keep > 1"
  (multiset=? (multiset-filter (lambda (e c) (> c 1))
                               (list->multiset '(a a b c c c)))
              (list->multiset '(a a c c c)))
  #t)

;;; multiset-map: count merging when two elements collapse to same target
(let ((ms (multiset-map (lambda (e c) (cons (abs e) c))
                        (list->multiset '(1 1 -1 2)))))
  (check "multiset-map: abs collapses ±1"  (multiset-count ms 1) 3)
  (check "multiset-map: 2 unchanged"       (multiset-count ms 2) 1))

;;; multiset->list round-trip
(let* ((lst '(x x y z z z))
       (ms  (list->multiset lst)))
  (check "multiset->list length" (length (multiset->list ms)) 6))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logical sets — classical
;;; ══════════════════════════════════════════════════════════════════════════

(let ((s (logical-set classical-logic 'a 'b 'c)))
  (check "classical lset-contains? a" (logical-set-contains? s 'a) #t)
  (check "classical lset-contains? z" (logical-set-contains? s 'z) #f)
  (check "classical lset-member a"    (logical-set-member s 'a)    #t)
  (check "classical lset-member z"    (logical-set-member s 'z)    #f)  ; bottom=#f
  (check "classical lset-size"        (logical-set-size s)         3))

(let ((a (logical-set classical-logic 'a 'b 'c))
      (b (logical-set classical-logic 'b 'c 'd)))
  (let ((u (logical-set-union a b))
        (i (logical-set-intersection a b))
        (d (logical-set-difference a b)))
    (check "classical union size"        (logical-set-size u) 4)
    (check "classical intersection size" (logical-set-size i) 2)
    (check "classical intersection has b" (logical-set-contains? i 'b) #t)
    (check "classical difference: a in"  (logical-set-contains? d 'a) #t)
    (check "classical difference: b out" (logical-set-contains? d 'b) #f)))

;;; logical-set↔set interop
(let* ((core-s  (list->set '(x y z)))
       (logic-s (set->logical-set classical-logic core-s))
       (back    (logical-set->set logic-s)))
  (check "set->logical-set->set roundtrip"
    (set=? core-s back) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logical sets — fuzzy
;;; ══════════════════════════════════════════════════════════════════════════

(let ((warm (fuzzy-set 'freezing 0.0 'cool 0.2 'tepid 0.5 'warm 0.8 'hot 1.0)))
  (check-approx "fuzzy-set member warm"    (logical-set-member warm 'warm)    0.8 1e-9)
  (check-approx "fuzzy-set member hot"     (logical-set-member warm 'hot)     1.0 1e-9)
  (check-approx "fuzzy-set member cool"    (logical-set-member warm 'cool)    0.2 1e-9)
  (check        "fuzzy-set contains? warm" (logical-set-contains? warm 'warm) #t)
  (check        "fuzzy-set contains? cool" (logical-set-contains? warm 'cool) #f)

  ;; Alpha-cut at 0.5 yields {warm, hot}
  (let ((cut (fuzzy-alpha-cut warm 0.5)))
    (check "alpha-cut size"         (set-size cut) 2)
    (check "alpha-cut has warm"     (set-member? cut 'warm) #t)
    (check "alpha-cut has hot"      (set-member? cut 'hot)  #t)
    (check "alpha-cut excludes cool" (set-member? cut 'cool) #f)))

;;; Fuzzy union: max of degrees
(let ((a (fuzzy-set 'x 0.3 'y 0.8))
      (b (fuzzy-set 'y 0.5 'z 0.9)))
  (let ((u (logical-set-union a b)))
    (check-approx "fuzzy union x"   (logical-set-member u 'x) 0.3 1e-9)
    (check-approx "fuzzy union y"   (logical-set-member u 'y) 0.8 1e-9)  ; max(0.8,0.5)
    (check-approx "fuzzy union z"   (logical-set-member u 'z) 0.9 1e-9)))

;;; Fuzzy intersection: min of degrees
(let ((a (fuzzy-set 'x 0.7 'y 0.4))
      (b (fuzzy-set 'x 0.5 'y 0.9)))
  (let ((i (logical-set-intersection a b)))
    (check-approx "fuzzy isect x"   (logical-set-member i 'x) 0.5 1e-9)  ; min(0.7,0.5)
    (check-approx "fuzzy isect y"   (logical-set-member i 'y) 0.4 1e-9)))  ; min(0.4,0.9)

;;; Fuzzy complement
(let ((s (fuzzy-set 'x 0.7 'y 0.2)))
  (let ((c (logical-set-complement s)))
    (check-approx "fuzzy complement x" (logical-set-member c 'x) 0.3 1e-9)
    (check-approx "fuzzy complement y" (logical-set-member c 'y) 0.8 1e-9)))

;;; Fuzzy difference a\b: meet(a_tv, not(b_tv))
(let ((a (fuzzy-set 'x 0.8 'y 0.6 'z 0.4))
      (b (fuzzy-set 'x 0.3 'y 0.9)))
  (let ((d (logical-set-difference a b)))
    ;; x: min(0.8, 1-0.3) = min(0.8, 0.7) = 0.7
    (check-approx "fuzzy diff x"    (logical-set-member d 'x) 0.7 1e-9)
    ;; y: min(0.6, 1-0.9) = min(0.6, 0.1) = 0.1
    (check-approx "fuzzy diff y"    (logical-set-member d 'y) 0.1 1e-9)
    ;; z: in a only: min(0.4, not(0.0)) = min(0.4, 1.0) = 0.4
    (check-approx "fuzzy diff z"    (logical-set-member d 'z) 0.4 1e-9)))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logical sets — Belnap (paraconsistent)
;;; ══════════════════════════════════════════════════════════════════════════

;;; The key property: contradictions don't explode
(let ((s (belnap-set)))
  (logical-set-assert! s 'paris 'T)   ; source A: Paris is in Europe
  (logical-set-assert! s 'paris 'F)   ; source B: Paris is NOT (wrong data)
  (check "belnap set: contradiction → B"  (logical-set-member s 'paris) 'B)
  (check "belnap set: B is 'true'"        (logical-set-contains? s 'paris) #t)
  (check "belnap set: contradiction detected"
    (length (belnap-set-contradictions s)) 1))

(let ((s (belnap-set 'P 'T 'Q 'B 'R 'N 'S 'F)))
  (check "belnap-set accessor T"  (logical-set-member s 'P) 'T)
  (check "belnap-set accessor B"  (logical-set-member s 'Q) 'B)
  (check "belnap-set accessor N"  (logical-set-member s 'R) 'N)
  (check "belnap-set accessor F"  (logical-set-member s 'S) 'F)
  (check "belnap unknowns"  (length (belnap-set-unknowns s)) 1)
  (check "belnap contradictions" (length (belnap-set-contradictions s)) 1))

;;; Belnap union / intersection / difference
(let ((a (belnap-set 'x 'T 'y 'F))
      (b (belnap-set 'y 'T 'z 'N)))
  (let ((u (logical-set-union a b)))
    (check "belnap union x"  (logical-set-member u 'x) 'T)   ; join(T,N)=T
    (check "belnap union y"  (logical-set-member u 'y) 'T)   ; join(F,T)=T
    (check "belnap union z"  (logical-set-member u 'z) 'N))  ; join(N,N)=N

  (let ((i (logical-set-intersection a b)))
    (check "belnap isect x"  (logical-set-member i 'x) 'N)   ; meet(T,N)=N
    (check "belnap isect y"  (logical-set-member i 'y) 'F))  ; meet(F,T)=F

  (let ((d (logical-set-difference a b)))
    ;; x: meet(T, belnap-not(N)) = meet(T, N) = N
    (check "belnap diff x"   (logical-set-member d 'x) 'N)
    ;; y: meet(F, belnap-not(T)) = meet(F, F) = F
    (check "belnap diff y"   (logical-set-member d 'y) 'F)))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logical-set higher-order
;;; ══════════════════════════════════════════════════════════════════════════

(let ((s (alist->logical-set fuzzy-logic '((a . 0.9) (b . 0.3) (c . 0.7)))))
  (check "lset-any? high degree"   (logical-set-any?   (lambda (e tv) (> tv 0.8)) s) #t)
  (check "lset-any? impossible"    (logical-set-any?   (lambda (e tv) (> tv 0.99)) s) #f)
  (check "lset-every? > 0"         (logical-set-every? (lambda (e tv) (> tv 0.0)) s) #t)
  (check "lset-every? > 0.5"       (logical-set-every? (lambda (e tv) (> tv 0.5)) s) #f)

  (let ((high (logical-set-filter (lambda (e tv) (> tv 0.5)) s)))
    (check "lset-filter: 2 high members" (logical-set-size high) 2)
    (check "lset-filter: a in"  (logical-set-contains? high 'a) #t)
    (check "lset-filter: b out" (logical-set-contains? high 'b) #f))

  (check "lset-fold: degree sum"
    (< (abs (- (logical-set-fold (lambda (acc e tv) (+ acc tv)) 0.0 s)
               1.9))
       1e-9)
    #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Convenience: fuzzy-set with implicit full membership
;;; ══════════════════════════════════════════════════════════════════════════

(let ((s (fuzzy-set 'x 'y 'z)))
  (check-approx "fuzzy-set bare elem x" (logical-set-member s 'x) 1.0 1e-9)
  (check-approx "fuzzy-set bare elem y" (logical-set-member s 'y) 1.0 1e-9))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Summary
;;; ══════════════════════════════════════════════════════════════════════════

(newline)
(display "Sets module tests: ") (display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(when (> fail 0) (error "test failures" fail))
