;;; srfi_225_tests.scm — (srfi 225) Dictionaries: the generic dict-*
;;; interface over Dictionary Type Objects (DTOs), the DTO construction/
;;; introspection machinery (make-dto/dto-ref/dto?), dictionary-error
;;; conditions, and the shipped DTOs (alist-based, srfi-69-dto,
;;; hash-table-dto/srfi-126-dto).

(import (scheme base) (srfi 225) (srfi 69) (srfi 126) (srfi 128))

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

;; alist->sorted-alist: dict-keys/dict->alist order is otherwise
;; insertion-dependent (most-recent-first, per the SRFI's own "new keys
;; added to the beginning" rule for alists) -- sort by key's own
;; write-representation so assertions are order-independent.
(define (akey<? a b) (string<? (%write-str (car a)) (%write-str (car b))))
(define (%write-str x)
  (call-with-port (open-output-string) (lambda (p) (write x p) (get-output-string p))))
(define (sorted-alist alist)
  (let loop ((remaining alist) (acc '()))
    (if (null? remaining)
        (reverse acc)
        (let find-min ((rest (cdr remaining)) (best (car remaining)) (before '()) (after '()))
          (cond
            ((null? rest)
             (loop (append (reverse before) after) (cons best acc)))
            ((akey<? (car rest) best)
             (find-min (cdr rest) (car rest) (cons best (append (reverse before) '())) (cdr rest)))
            (else (find-min (cdr rest) best (cons (car rest) before) after)))))))

;; sort-list: tiny local insertion sort so this file has no extra SRFI
;; dependency just for test assertions.
(define (sort-list lst . cmp)
  (let ((less? (if (pair? cmp) (car cmp) (lambda (a b) (string<? (%write-str a) (%write-str b))))))
    (define (insert x sorted)
      (cond ((null? sorted) (list x))
            ((less? x (car sorted)) (cons x sorted))
            (else (cons (car sorted) (insert x (cdr sorted))))))
    (let loop ((remaining lst) (acc '()))
      (if (null? remaining) acc
          (loop (cdr remaining) (insert (car remaining) acc))))))

;;; ---- alist DTOs: dto? / dictionary? ----

(check "dto? true for equal-alist-dto" (dto? equal-alist-dto) #t)
(check "dto? false for a non-dto" (dto? 5) #f)
(check "dictionary? true for an alist" (dictionary? equal-alist-dto '((a . 1))) #t)
(check "dictionary? true for the empty list" (dictionary? equal-alist-dto '()) #t)
(check "dictionary? false for a non-alist" (dictionary? equal-alist-dto 5) #f)

;;; ---- basic set!/ref/ref-default/size/contains?/empty? ----

(define d0 (dict-set! equal-alist-dto '() 'a 1 'b 2))
(check "dict-ref finds an existing key" (dict-ref equal-alist-dto d0 'a) 1)
(check "dict-ref/default returns default for a missing key"
       (dict-ref/default equal-alist-dto d0 'z 'nope) 'nope)
(check "dict-ref/default returns the value for an existing key"
       (dict-ref/default equal-alist-dto d0 'b 'nope) 2)
(check "dict-ref raises dictionary-error for a missing key with no failure thunk"
       (guard (e ((dictionary-error? e) 'caught)) (dict-ref equal-alist-dto d0 'z))
       'caught)
(check "dict-ref's failure thunk is honored" (dict-ref equal-alist-dto d0 'z (lambda () 'fell-back)) 'fell-back)
(check "dict-ref's success proc is honored" (dict-ref equal-alist-dto d0 'a (lambda () #f) (lambda (v) (* v 100))) 100)
(check "dict-size" (dict-size equal-alist-dto d0) 2)
(check "dict-empty? false for a non-empty dict" (dict-empty? equal-alist-dto d0) #f)
(check "dict-empty? true for the empty alist" (dict-empty? equal-alist-dto '()) #t)
(check "dict-contains? true" (dict-contains? equal-alist-dto d0 'a) #t)
(check "dict-contains? false" (dict-contains? equal-alist-dto d0 'z) #f)

;;; ---- purity: alist updates never mutate the original ----

(check "equal-alist-dto is pure" (dict-pure? equal-alist-dto d0) #t)
(define d1 (dict-set! equal-alist-dto d0 'a 999))
(check "dict-set! on a new key overriding an existing one updates the value"
       (dict-ref equal-alist-dto d1 'a) 999)
(check "the original dict is unaffected by a pure update"
       (dict-ref equal-alist-dto d0 'a) 1)

;;; ---- adjoin! / replace! / delete! / delete-all! ----

(check "dict-adjoin! keeps the existing value for an already-present key"
       (dict-ref equal-alist-dto (dict-adjoin! equal-alist-dto d0 'a 111) 'a) 1)
(check "dict-adjoin! adds a genuinely new key"
       (dict-ref equal-alist-dto (dict-adjoin! equal-alist-dto d0 'c 3) 'c) 3)
(check "dict-replace! replaces an existing key's value"
       (dict-ref equal-alist-dto (dict-replace! equal-alist-dto d0 'a 42) 'a) 42)
(check "dict-replace! is a no-op for a missing key"
       (sorted-alist (dict->alist equal-alist-dto (dict-replace! equal-alist-dto d0 'z 42)))
       (sorted-alist (dict->alist equal-alist-dto d0)))
(check "dict-delete! removes a key" (dict-contains? equal-alist-dto (dict-delete! equal-alist-dto d0 'a) 'a) #f)
(check "dict-delete! with multiple keys removes all of them"
       (dict-size equal-alist-dto (dict-delete! equal-alist-dto d0 'a 'b)) 0)
(check "dict-delete-all! removes every key in the given list"
       (dict-size equal-alist-dto (dict-delete-all! equal-alist-dto d0 '(a b))) 0)

;;; ---- update! / update/default! / intern! / pop! ----

(check "dict-update! applies updater to an existing value"
       (dict-ref equal-alist-dto (dict-update! equal-alist-dto d0 'a (lambda (v) (+ v 1))) 'a) 2)
(check "dict-update! with a default success/failure raises on a missing key"
       (guard (e ((dictionary-error? e) 'caught)) (dict-update! equal-alist-dto d0 'z (lambda (v) v)))
       'caught)
(check "dict-update/default! uses the given default for a missing key"
       (dict-ref equal-alist-dto (dict-update/default! equal-alist-dto d0 'z (lambda (v) (+ v 1)) 10) 'z) 11)
(call-with-values (lambda () (dict-intern! equal-alist-dto d0 'a (lambda () 'unused)))
  (lambda (d v) (check "dict-intern! on an existing key returns its current value, unchanged" v 1)))
(call-with-values (lambda () (dict-intern! equal-alist-dto d0 'c (lambda () 3)))
  (lambda (d v)
    (check "dict-intern! on a missing key returns the freshly-computed value" v 3)
    (check "dict-intern! on a missing key adds it" (dict-ref equal-alist-dto d 'c) 3)))
(check "dict-pop! raises on an empty dict"
       (guard (e ((dictionary-error? e) 'caught)) (dict-pop! equal-alist-dto '()))
       'caught)
(call-with-values (lambda () (dict-pop! equal-alist-dto d0))
  (lambda (d k v)
    (check "dict-pop! removes the popped key from the returned dict" (dict-contains? equal-alist-dto d k) #f)
    (check "dict-pop! popped value matches what dict-ref would have given"
           v (dict-ref equal-alist-dto d0 k))))

;;; ---- map / filter / remove ----

(check "dict-map transforms every value, keys unchanged"
       (sorted-alist (dict->alist equal-alist-dto (dict-map equal-alist-dto (lambda (k v) (* v 10)) d0)))
       (sorted-alist '((a . 10) (b . 20))))
(check "dict-filter keeps only matching associations"
       (sorted-alist (dict->alist equal-alist-dto (dict-filter equal-alist-dto (lambda (k v) (> v 1)) d0)))
       '((b . 2)))
(check "dict-remove drops matching associations"
       (sorted-alist (dict->alist equal-alist-dto (dict-remove equal-alist-dto (lambda (k v) (> v 1)) d0)))
       '((a . 1)))

;;; ---- whole-dictionary operations ----

(check "dict-count" (dict-count equal-alist-dto (lambda (k v) (> v 1)) d0) 1)
(check "dict-any finds a truthy result" (dict-any equal-alist-dto (lambda (k v) (and (> v 1) v)) d0) 2)
(check "dict-any returns #f when nothing matches" (dict-any equal-alist-dto (lambda (k v) (> v 100)) d0) #f)
(check "dict-every true when all match" (dict-every equal-alist-dto (lambda (k v) (> v 0)) d0) #t)
(check "dict-every false when one fails" (dict-every equal-alist-dto (lambda (k v) (> v 1)) d0) #f)
(check "dict-keys" (sort-list (dict-keys equal-alist-dto d0)) '(a b))
(check "dict-values" (sort-list (dict-values equal-alist-dto d0)) '(1 2))
(call-with-values (lambda () (dict-entries equal-alist-dto d0))
  (lambda (ks vs)
    (check "dict-entries keys" (sort-list ks) '(a b))
    (check "dict-entries values" (sort-list vs) '(1 2))))
(check "dict-fold sums all values" (dict-fold equal-alist-dto (lambda (k v acc) (+ v acc)) 0 d0) 3)
(check "dict-map->list" (sort-list (dict-map->list equal-alist-dto (lambda (k v) (cons k v)) d0)
                                    (lambda (a b) (string<? (%write-str a) (%write-str b))))
       (list (cons 'a 1) (cons 'b 2)))
(check "dict->alist" (sorted-alist (dict->alist equal-alist-dto d0)) '((a . 1) (b . 2)))

;;; ---- dict=? ----

(check "dict=? true for equal dicts built independently"
       (dict=? equal-alist-dto = d0 (dict-set! equal-alist-dto '() 'b 2 'a 1)) #t)
(check "dict=? false when a value differs"
       (dict=? equal-alist-dto = d0 (dict-set! equal-alist-dto '() 'a 1 'b 999)) #f)
(check "dict=? false when sizes differ"
       (dict=? equal-alist-dto = d0 (dict-set! equal-alist-dto '() 'a 1)) #f)

;;; ---- for-each / generator / accumulators ----

(let ((seen '()))
  (dict-for-each equal-alist-dto (lambda (k v) (set! seen (cons (cons k v) seen))) d0)
  (check "dict-for-each visits every association" (sorted-alist seen) '((a . 1) (b . 2))))

(let ((g (dict->generator equal-alist-dto d0)) (seen '()))
  (let loop ()
    (let ((v (g)))
      (if (not (eof-object? v)) (begin (set! seen (cons v seen)) (loop)))))
  (check "dict->generator yields every (key . value) pair" (sorted-alist seen) '((a . 1) (b . 2))))

(let ((acc (dict-set!-accumulator equal-alist-dto '())))
  (acc (cons 'p 1))
  (acc (cons 'q 2))
  (check "dict-set!-accumulator builds up a dict"
         (sorted-alist (dict->alist equal-alist-dto (acc (eof-object)))) '((p . 1) (q . 2))))

(let ((acc (dict-adjoin!-accumulator equal-alist-dto (dict-set! equal-alist-dto '() 'p 'orig))))
  (acc (cons 'p 'overwritten-attempt))
  (acc (cons 'q 2))
  (check "dict-adjoin!-accumulator keeps pre-existing keys' values"
         (sorted-alist (dict->alist equal-alist-dto (acc (eof-object)))) '((p . orig) (q . 2))))

;;; ---- make-dto validation ----

(check "make-dto raises when a required proc-id is missing"
       (guard (e ((dictionary-error? e) 'caught)) (make-dto dictionary?-id (lambda (x) #t)))
       'caught)
(check "make-dto raises on an odd argument count"
       (guard (e ((dictionary-error? e) 'caught)) (make-dto dictionary?-id))
       'caught)
(check "dto-ref returns a required proc-id's own procedure directly"
       ((dto-ref equal-alist-dto dictionary?-id) '((a . 1))) #t)
(check "dto-ref derives a working default for an unset optional proc-id"
       (sort-list ((dto-ref equal-alist-dto dict-keys-id) d0)) '(a b))
(check "dto-ref caches the derived default (same procedure object on repeated calls)"
       (eq? (dto-ref equal-alist-dto dict-keys-id) (dto-ref equal-alist-dto dict-keys-id)) #t)

;;; ---- dictionary-error ----

(check "dictionary-error? true for a raised dictionary error"
       (guard (e (#t (dictionary-error? e))) (dictionary-error "boom" 1 2))
       #t)
(check "dictionary-message" (guard (e (#t (dictionary-message e))) (dictionary-error "boom" 1 2)) "boom")
(check "dictionary-irritants" (guard (e (#t (dictionary-irritants e))) (dictionary-error "boom" 1 2)) '(1 2))
(check "dictionary-error? false for an ordinary error"
       (guard (e (#t (dictionary-error? e))) (error "not a dictionary error"))
       #f)

;;; ---- srfi-69-dto ----

(define h (make-hash-table))
(hash-table-set! h 'a 1)
(hash-table-set! h 'b 2)
(check "srfi-69-dto dictionary?" (dictionary? srfi-69-dto h) #t)
(check "srfi-69-dto is impure" (dict-pure? srfi-69-dto h) #f)
(check "srfi-69-dto dict-ref" (dict-ref srfi-69-dto h 'a) 1)
(check "srfi-69-dto dict-set! mutates in place"
       (begin (dict-set! srfi-69-dto h 'c 3) (hash-table-ref/default h 'c #f)) 3)
(check "srfi-69-dto dict-set! returns the same (mutated) table"
       (eq? (dict-set! srfi-69-dto h 'd 4) h) #t)
(check "srfi-69-dto dict-size" (dict-size srfi-69-dto h) 4)
(check "srfi-69-dto dict-delete!" (begin (dict-delete! srfi-69-dto h 'd) (dict-contains? srfi-69-dto h 'd)) #f)
(check "srfi-69-dto dict-keys" (sort-list (dict-keys srfi-69-dto h)) '(a b c))
(let ((mapped (dict-map srfi-69-dto (lambda (k v) (* v 10)) h)))
  (check "srfi-69-dto dict-map produces a table with transformed values"
         (dict-ref srfi-69-dto mapped 'a) 10)
  (check "srfi-69-dto dict-map doesn't mutate the original table"
         (hash-table-ref/default h 'a #f) 1))
(check "srfi-69-dto dict-fold" (dict-fold srfi-69-dto (lambda (k v acc) (+ v acc)) 0 h) 6)
(check "srfi-69-dto dict-comparator returns a comparator" (comparator? (dict-comparator srfi-69-dto h)) #t)

;;; ---- hash-table-dto / srfi-126-dto (same underlying wrapper) ----

(check "hash-table-dto and srfi-126-dto are the same DTO" (eq? hash-table-dto srfi-126-dto) #t)
(define ht (make-eqv-hashtable))
(hashtable-set! ht 1 'one)
(check "hash-table-dto dictionary?" (dictionary? hash-table-dto ht) #t)
(check "hash-table-dto dict-ref" (dict-ref hash-table-dto ht 1) 'one)
(check "hash-table-dto dict-set! mutates in place"
       (begin (dict-set! hash-table-dto ht 2 'two) (hashtable-ref ht 2 #f)) 'two)
(check "hash-table-dto dict-size" (dict-size hash-table-dto ht) 2)
(check "hash-table-dto dict-contains? false for a missing key" (dict-contains? hash-table-dto ht 999) #f)
(check "hash-table-dto is impure" (dict-pure? hash-table-dto ht) #f)

;;; ---- Summary ----

(newline)
(display "srfi-225 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
