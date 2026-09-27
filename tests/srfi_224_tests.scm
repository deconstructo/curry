;;; srfi_224_tests.scm — (srfi 224) Integer Mappings (fxmappings):
;;; construction, accessors, updaters (alter/update as the workhorse
;;; primitives), whole-mapping queries, traversal, filtering,
;;; conversion, comparison, set-theoretic combination, and sorted-key
;;; interval queries.

(import (scheme base) (srfi 224) (srfi 128))

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

(define m (fxmapping 1 'a 2 'b 3 'c))

;;; ---- construction / basic accessors ----

(check "fxmapping? true for a real fxmapping" (fxmapping? m) #t)
(check "fxmapping? false for a non-fxmapping" (fxmapping? 5) #f)
(check "fxmapping-size" (fxmapping-size m) 3)
(check "fxmapping-ref finds an existing key" (fxmapping-ref m 2 (lambda () 'nf)) 'b)
(check "fxmapping-ref calls failure for a missing key" (fxmapping-ref m 9 (lambda () 'nf)) 'nf)
(check "fxmapping-ref's success proc is honored" (fxmapping-ref m 2 (lambda () 'nf) (lambda (v) (list 'got v))) '(got b))
(check "fxmapping-ref/default" (fxmapping-ref/default m 9 'default) 'default)
(check "fxmapping-contains? true" (fxmapping-contains? m 1) #t)
(check "fxmapping-contains? false" (fxmapping-contains? m 9) #f)
(check "fxmapping-empty? false for a non-empty mapping" (fxmapping-empty? m) #f)
(check "fxmapping-empty? true for the empty mapping" (fxmapping-empty? (fxmapping)) #t)
(check "fxmapping-disjoint? true for non-overlapping key sets"
       (fxmapping-disjoint? (fxmapping 1 'a) (fxmapping 2 'b)) #t)
(check "fxmapping-disjoint? false when keys overlap"
       (fxmapping-disjoint? (fxmapping 1 'a) (fxmapping 1 'b)) #f)
(check "earlier association wins on a duplicate key in the constructor"
       (fxmapping-ref (fxmapping 1 'first 1 'second) 1 (lambda () 'nf)) 'first)
(check "fxmapping rejects a non-exact-integer key"
       (guard (e (#t 'caught)) (fxmapping 1.5 'x)) 'caught)
(check "fxmapping rejects an odd number of key/value arguments"
       (guard (e (#t 'caught)) (fxmapping 1)) 'caught)

(call-with-values (lambda () (fxmapping-min m)) (lambda (k v) (check "fxmapping-min" (list k v) '(1 a))))
(call-with-values (lambda () (fxmapping-max m)) (lambda (k v) (check "fxmapping-max" (list k v) '(3 c))))
(check "fxmapping-min raises on an empty mapping" (guard (e (#t 'caught)) (fxmapping-min (fxmapping))) 'caught)

;;; ---- updaters ----

(check "fxmapping-adjoin preserves an existing value"
       (fxmapping-ref (fxmapping-adjoin m 1 'zz) 1 (lambda () 'nf)) 'a)
(check "fxmapping-adjoin adds a genuinely new key"
       (fxmapping-ref (fxmapping-adjoin m 9 'new) 9 (lambda () 'nf)) 'new)
;; combine is called as (proc k NEW OLD) -- the just-supplied value
;; first, the existing one second (per the spec's own text and worked
;; example) -- a real bug, found by review, had these swapped; this
;; check uses a non-commutative combiner (plain list, not equal either
;; way round) so a regression would fail loudly rather than
;; coincidentally still matching.
(check "fxmapping-adjoin/combinator calls proc as (k new old)"
       (fxmapping-ref (fxmapping-adjoin/combinator m (lambda (k new old) (list new old)) 1 'zz) 1 (lambda () 'nf))
       '(zz a))
(check "fxmapping-set overwrites an existing value"
       (fxmapping-ref (fxmapping-set m 1 'zz) 1 (lambda () 'nf)) 'zz)
(check "fxmapping-adjust transforms an existing value"
       (fxmapping-ref (fxmapping-adjust m 2 (lambda (k v) (list k v))) 2 (lambda () 'nf)) '(2 b))
(check "fxmapping-adjust is a no-op for a missing key" (fxmapping->alist (fxmapping-adjust m 9 (lambda (k v) 'x))) (fxmapping->alist m))
(check "fxmapping-delete removes the given keys" (fxmapping-contains? (fxmapping-delete m 1 2) 1) #f)
(check "fxmapping-delete-all removes every key in the list" (fxmapping-size (fxmapping-delete-all m '(1 2))) 1)

(check "fxmapping-delete-min removes the least-key association" (fxmapping->alist (fxmapping-delete-min m)) '((2 . b) (3 . c)))
(check "fxmapping-delete-max removes the greatest-key association" (fxmapping->alist (fxmapping-delete-max m)) '((1 . a) (2 . b)))
(check "fxmapping-delete-min raises on empty" (guard (e (#t 'caught)) (fxmapping-delete-min (fxmapping))) 'caught)
(call-with-values (lambda () (fxmapping-pop-min m))
  (lambda (k v m2) (check "fxmapping-pop-min" (list k v (fxmapping->alist m2)) '(1 a ((2 . b) (3 . c))))))
(call-with-values (lambda () (fxmapping-pop-max m))
  (lambda (k v m2) (check "fxmapping-pop-max" (list k v (fxmapping->alist m2)) '(3 c ((1 . a) (2 . b))))))

;;; ---- fxmapping-update / fxmapping-alter (the workhorse primitives) ----

(check "fxmapping-update replaces an existing value via the given proc"
       (fxmapping-ref (fxmapping-update m 2 (lambda (k v replace delete) (replace (list 'upd v)))) 2 (lambda () 'nf))
       '(upd b))
(check "fxmapping-update raises on a missing key with no failure given"
       (guard (e (#t 'caught)) (fxmapping-update m 99 (lambda (k v r d) v))) 'caught)
(check "fxmapping-update's failure thunk result is returned directly (not re-wrapped)"
       (fxmapping-update m 99 (lambda (k v r d) v) (lambda () 'used-failure)) 'used-failure)

(check "fxmapping-alter's success branch can replace"
       (fxmapping-ref (fxmapping-alter m 2 (lambda (i ig) (ig)) (lambda (k v r d) (r (list 'altered v)))) 2 (lambda () 'nf))
       '(altered b))
(check "fxmapping-alter's success branch can delete"
       (fxmapping-contains? (fxmapping-alter m 2 (lambda (i ig) (ig)) (lambda (k v r d) (d))) 2) #f)
(check "fxmapping-alter's failure branch can insert"
       (fxmapping-ref (fxmapping-alter m 42 (lambda (i ig) (i 'inserted)) (lambda (k v r d) v)) 42 (lambda () 'nf))
       'inserted)
(check "fxmapping-alter's failure branch can ignore, leaving the mapping unchanged"
       (fxmapping->alist (fxmapping-alter m 42 (lambda (i ig) (ig)) (lambda (k v r d) v)))
       (fxmapping->alist m))

;;; ---- whole mapping operations ----

(check "fxmapping-count" (fxmapping-count (lambda (k v) (even? k)) m) 1)
(check "fxmapping-any? true" (fxmapping-any? (lambda (k v) (eq? v 'b)) m) #t)
(check "fxmapping-any? false" (fxmapping-any? (lambda (k v) (eq? v 'z)) m) #f)
(check "fxmapping-every? true" (fxmapping-every? (lambda (k v) (symbol? v)) m) #t)
(check "fxmapping-every? vacuously true on the empty mapping" (fxmapping-every? (lambda (k v) #f) (fxmapping)) #t)
(call-with-values (lambda () (fxmapping-find (lambda (k v) (eq? v 'b)) m (lambda () 'nf)))
  (lambda (k v) (check "fxmapping-find locates a match" (list k v) '(2 b))))
(check "fxmapping-find calls failure when nothing matches"
       (fxmapping-find (lambda (k v) (eq? v 'z)) m (lambda () 'nf)) 'nf)

;;; ---- traversal ----

(check "fxmapping-map transforms values, preserving keys"
       (fxmapping->alist (fxmapping-map (lambda (k v) (list k v)) m))
       '((1 1 a) (2 2 b) (3 3 c)))
(let ((acc '()))
  (fxmapping-for-each (lambda (k v) (set! acc (cons (cons k v) acc))) m)
  (check "fxmapping-for-each visits every association in ascending key order" (reverse acc) '((1 . a) (2 . b) (3 . c))))
(check "fxmapping-fold is a left fold in ascending key order" (fxmapping-fold (lambda (k v acc) (cons k acc)) '() m) '(3 2 1))
(check "fxmapping-fold-right is a right fold in descending key order" (fxmapping-fold-right (lambda (k v acc) (cons k acc)) '() m) '(1 2 3))
(check "fxmapping-map->list" (fxmapping-map->list (lambda (k v) v) m) '(a b c))
(check "fxmapping-relation-map transforms both keys and values"
       (fxmapping->alist (fxmapping-relation-map (lambda (k v) (values (* k 10) v)) m))
       '((10 . a) (20 . b) (30 . c)))

;;; ---- filtering ----

(check "fxmapping-filter keeps matching associations" (fxmapping->alist (fxmapping-filter (lambda (k v) (even? k)) m)) '((2 . b)))
(check "fxmapping-remove drops matching associations" (fxmapping->alist (fxmapping-remove (lambda (k v) (even? k)) m)) '((1 . a) (3 . c)))
(call-with-values (lambda () (fxmapping-partition (lambda (k v) (even? k)) m))
  (lambda (a b) (check "fxmapping-partition" (list (fxmapping->alist a) (fxmapping->alist b)) '(((2 . b)) ((1 . a) (3 . c))))))

;;; ---- conversion ----

(check "fxmapping->alist is in ascending key order" (fxmapping->alist m) '((1 . a) (2 . b) (3 . c)))
(check "fxmapping->decreasing-alist is in descending key order" (fxmapping->decreasing-alist m) '((3 . c) (2 . b) (1 . a)))
(check "fxmapping-keys" (fxmapping-keys m) '(1 2 3))
(check "fxmapping-values" (fxmapping-values m) '(a b c))
(let ((g (fxmapping->generator m)) (acc '()))
  (let loop () (let ((v (g))) (if (not (eof-object? v)) (begin (set! acc (cons v acc)) (loop)))))
  (check "fxmapping->generator yields every pair in ascending order" (reverse acc) '((1 . a) (2 . b) (3 . c))))

;;; ---- comparison ----

(check "fxmapping=? true regardless of construction order"
       (fxmapping=? eq-comparator m (fxmapping 3 'c 2 'b 1 'a)) #t)
(check "fxmapping=? false when content differs" (fxmapping=? eq-comparator m (fxmapping 1 'a 2 'b)) #f)
(check "fxmapping<=? true for a subset" (fxmapping<=? eq-comparator (fxmapping 1 'a) m) #t)
(check "fxmapping<? true for a proper subset" (fxmapping<? eq-comparator (fxmapping 1 'a) m) #t)
(check "fxmapping<? false for a mapping compared to itself" (fxmapping<? eq-comparator m m) #f)
(check "fxmapping<=? true for a mapping compared to itself" (fxmapping<=? eq-comparator m m) #t)
(check "fxmapping>=? true for a superset" (fxmapping>=? eq-comparator m (fxmapping 1 'a)) #t)
(check "fxmapping>? true for a proper superset" (fxmapping>? eq-comparator m (fxmapping 1 'a)) #t)

;;; ---- set-theoretic operations ----

(define m1 (fxmapping 1 'a 2 'b))
(define m2 (fxmapping 2 'B 3 'c))

(check "fxmapping-union keeps the first mapping's value on key collision"
       (fxmapping->alist (fxmapping-union m1 m2)) '((1 . a) (2 . b) (3 . c)))
(check "fxmapping-intersection keeps the first mapping's value"
       (fxmapping->alist (fxmapping-intersection m1 m2)) '((2 . b)))
(check "fxmapping-difference removes keys present in the second mapping"
       (fxmapping->alist (fxmapping-difference m1 m2)) '((1 . a)))
(check "fxmapping-xor keeps only keys unique to one side"
       (fxmapping->alist (fxmapping-xor m1 m2)) '((1 . a) (3 . c)))
(check "fxmapping-union/combinator combines colliding values"
       (fxmapping->alist (fxmapping-union/combinator (lambda (k a b) (list a b)) m1 m2))
       '((1 . a) (2 b B) (3 . c)))
(check "fxmapping-intersection/combinator combines colliding values"
       (fxmapping->alist (fxmapping-intersection/combinator (lambda (k a b) (list a b)) m1 m2))
       '((2 b B)))

;;; ---- submappings / interval queries ----

(define m3 (fxmapping 1 'a 2 'b 3 'c 4 'd 5 'e))

(check "fxmapping-open-interval excludes both endpoints" (fxmapping-keys (fxmapping-open-interval m3 1 5)) '(2 3 4))
(check "fxmapping-closed-interval includes both endpoints" (fxmapping-keys (fxmapping-closed-interval m3 1 5)) '(1 2 3 4 5))
(check "fxmapping-open-closed-interval excludes low, includes high" (fxmapping-keys (fxmapping-open-closed-interval m3 1 5)) '(2 3 4 5))
(check "fxmapping-closed-open-interval includes low, excludes high" (fxmapping-keys (fxmapping-closed-open-interval m3 1 5)) '(1 2 3 4))
(check "fxsubmapping=" (fxmapping-keys (fxsubmapping= m3 3)) '(3))
(check "fxsubmapping<" (fxmapping-keys (fxsubmapping< m3 3)) '(1 2))
(check "fxsubmapping<=" (fxmapping-keys (fxsubmapping<= m3 3)) '(1 2 3))
(check "fxsubmapping>" (fxmapping-keys (fxsubmapping> m3 3)) '(4 5))
(check "fxsubmapping>=" (fxmapping-keys (fxsubmapping>= m3 3)) '(3 4 5))
(call-with-values (lambda () (fxmapping-split m3 3))
  (lambda (a b) (check "fxmapping-split" (list (fxmapping-keys a) (fxmapping-keys b)) '((1 2 3) (4 5)))))

;;; ---- unfold / accumulate ----

(check "fxmapping-unfold builds a mapping by repeated stepping"
       (fxmapping->alist (fxmapping-unfold (lambda (n) (> n 3)) (lambda (n) (values n (* n n))) (lambda (n) (+ n 1)) 1))
       '((1 . 1) (2 . 4) (3 . 9)))
(check "fxmapping-accumulate builds a mapping via an abort-capable step proc"
       (fxmapping->alist (fxmapping-accumulate (lambda (abort n) (if (> n 3) (abort) (values n (* n n) (+ n 1)))) 1))
       '((1 . 1) (2 . 4) (3 . 9)))
(check "fxmapping-accumulate's abort returns what's accumulated so far"
       (fxmapping->alist (fxmapping-accumulate (lambda (abort n) (if (= n 2) (abort) (values n (* n 100) (+ n 1)))) 1))
       '((1 . 100)))
;; abort-with-result must accept and forward arbitrary extra values
;; alongside the built fxmapping, as ADDITIONAL return values -- per
;; the spec's own worked example, which calls it with an argument. A
;; real bug, found by review: an earlier draft made this a zero-
;; argument-only thunk, raising instead of accepting a value.
(call-with-values
  (lambda () (fxmapping-accumulate (lambda (abort n) (if (> n 3) (abort 'finished) (values n (* n n) (+ n 1)))) 1))
  (lambda (m . extra)
    (check "fxmapping-accumulate's abort-with-result accepts and returns an extra value"
           (list (fxmapping->alist m) extra) '(((1 . 1) (2 . 4) (3 . 9)) (finished)))))
(call-with-values
  (lambda () (fxmapping-accumulate (lambda (abort n) (abort)) 1))
  (lambda (m . extra)
    (check "fxmapping-accumulate's abort with no extra values gives an empty extra list"
           (list (fxmapping->alist m) extra) '(() ()))))

;;; ---- alist conversions ----

(check "alist->fxmapping" (fxmapping->alist (alist->fxmapping (list (cons 1 'a) (cons 2 'b)))) '((1 . a) (2 . b)))
;; Same (k new old) argument order as fxmapping-adjoin/combinator,
;; verified here with the spec's own worked example (string-append is
;; non-commutative, so a swapped-argument regression fails loudly).
(check "alist->fxmapping/combinator calls proc as (k new old), matching the spec's own example"
       (fxmapping->alist (alist->fxmapping/combinator (lambda (k s t) (string-append s " " t))
                            (list (cons 1 "riker") (cons 2 "yar") (cons 2 "tasha"))))
       '((1 . "riker") (2 . "tasha yar")))

;;; ---- Summary ----

(newline)
(display "srfi-224 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
