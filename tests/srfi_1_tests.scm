;;; srfi_1_tests.scm — (srfi 1) list library: the SRFI-1 procedures added
;;; in this session (curry previously only had a small ~30-name subset),
;;; plus regression coverage for two real bugs found while adding them:
;;;
;;;   1. `fold` was implemented via fold-left's argument convention
;;;      (accumulator first), not SRFI-1's actual convention (elements
;;;      first, accumulator last) -- silently wrong for any non-
;;;      commutative kons, e.g. (fold cons '() '(1 2 3)) must be (3 2 1),
;;;      not the fold-left-shaped (((() . 1) . 2) . 3).
;;;   2. The (srfi 1)/(srfi srfi-1) bare-number shims imported (curry
;;;      private lang-aliases) -- a plain, non-define-library file whose
;;;      environment chains up to GLOBAL_ENV -- AFTER importing (srfi s1
;;;      lists), so re-defined core names (member/assoc/reduce all
;;;      already exist as core primitives) got silently re-clobbered
;;;      back to the stale core version. Fixed by importing lang-aliases
;;;      FIRST, so (srfi s1 lists)'s own bindings win last.

(import (srfi 1))

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

;;; ---- fold / fold-right / reduce / reduce-right ----

(check "fold: elements first, accumulator last (not fold-left order)"
       (fold cons '() (list 1 2 3)) '(3 2 1))
(check "fold: acc-first order still shows for a cons where acc is first arg"
       (fold cons '() (list 1 2 3)) (fold (lambda (e acc) (cons e acc)) '() (list 1 2 3)))
(check "fold-right: identity for cons" (fold-right cons '() (list 1 2 3)) '(1 2 3))
(check "fold: multi-list" (fold (lambda (a b acc) (+ a b acc)) 0 '(1 2 3) '(10 20 30)) 66)
(check "reduce: commutative op" (reduce + 0 (list 1 2 3 4)) 10)
(check "reduce: uses first element as seed, not ridentity"
       (reduce cons 'fallback (list 1 2 3)) '(3 2 . 1))
(check "reduce: empty list returns ridentity" (reduce + 0 (list)) 0)
(check "reduce-right: empty list returns ridentity" (reduce-right + 0 (list)) 0)
(check "reduce-right: non-commutative" (reduce-right cons 'fallback (list 1 2 3)) '(1 2 . 3))

;;; ---- member / assoc with optional predicate ----

(check "member: default equal?" (member 3 (list 1 2 3 4)) '(3 4))
(check "member: not found" (member 99 (list 1 2 3)) #f)
(check "member: custom predicate" (member 2.0 (list 1 2 3) =) '(2 3))
(check "assoc: default equal?" (assoc 2 (list (list 1 'a) (list 2 'b))) '(2 b))
(check "assoc: custom predicate" (assoc 2.0 (list (list 1 'a) (list 2 'b)) =) '(2 b))

;;; ---- Constructors ----

(check "xcons" (xcons 2 1) '(1 . 2))
(check "cons*" (cons* 1 2 (list 3 4)) '(1 2 3 4))
(check "cons* single arg" (cons* (list 1 2)) '(1 2))
;; regression: cons*/take/take-while/unfold originally built their
;; result via naive non-tail cons-recursion. curry's per-function
;; stack-overflow guard doesn't catch that shape of recursion inside a
;; define-library body (a separate, pre-existing core VM gap) -- it
;; SIGSEGVs the whole process instead of raising a catchable error, at
;; a few hundred to a few thousand elements. Found live by independent
;; security review. Fixed by rewriting all four as accumulator-based
;; tail loops; these checks exercise sizes well past the old crash
;; thresholds (750 for cons*, ~5000 for take/unfold) without needing
;; to actually reproduce the crash in the test suite itself.
(check "cons* at a size past the old crash threshold"
       (length (apply cons* (append (iota 2000) (list (list))))) 2000)
(check "take at a size past the old crash threshold"
       (length (take (iota 200000) 20000)) 20000)
(check "take-while at a size past the old crash threshold"
       (length (take-while (lambda (x) (< x 20000)) (iota 200000))) 20000)
(check "unfold at a size past the old crash threshold"
       (length (unfold (lambda (i) (= i 20000)) (lambda (i) i) (lambda (i) (+ i 1)) 0)) 20000)
(check "list-tabulate" (list-tabulate 4 (lambda (i) (* i i))) '(0 1 4 9))
(check "circular-list?" (circular-list? (circular-list 1 2 3)) #t)
(check "circular-list? on a proper list" (circular-list? (list 1 2 3)) #f)
;; regression: dotted-list?'s original implementation did its own naive
;; (cdr p) walk with no cycle detection, hanging forever on a circular
;; list -- found live by independent security review. Fixed by deriving
;; it from proper-list?/circular-list? (both already cycle-safe) instead
;; of a third manual walk. These three predicates are exactly the ones
;; SRFI-1 requires to correctly classify ANY list shape without hanging.
(check "dotted-list? on a circular list terminates and is false"
       (dotted-list? (circular-list 1 2 3)) #f)

;;; ---- Predicates ----

(check "proper-list? on a proper list" (proper-list? (list 1 2 3)) #t)
(check "proper-list? on a dotted pair" (proper-list? (cons 1 2)) #f)
(check "proper-list? on the empty list" (proper-list? (list)) #t)
(check "dotted-list? on a dotted pair" (dotted-list? (cons 1 2)) #t)
(check "dotted-list? on a proper list" (dotted-list? (list 1 2)) #f)
(check "null-list?" (null-list? (list)) #t)
(check "not-pair?" (not-pair? (list)) #t)
(check "list=" (list= = (list 1 2) (list 1.0 2.0) (list 1 2)) #t)
(check "list= mismatch" (list= = (list 1 2) (list 1 3)) #f)

;;; ---- Selectors ----

(check "sixth" (sixth (list 1 2 3 4 5 6)) 6)
(check "tenth" (tenth (list 1 2 3 4 5 6 7 8 9 10)) 10)
(check "take-right" (take-right (list 1 2 3 4 5) 2) '(4 5))
(check "drop-right" (drop-right (list 1 2 3 4 5) 2) '(1 2 3))
(check "split-at"
       (call-with-values (lambda () (split-at (list 1 2 3 4 5) 2)) list)
       '((1 2) (3 4 5)))
(check "last" (last (list 1 2 3)) 3)

;;; ---- Fold-family helpers: unfold / unfold-right / map! / pair-for-each ----

(check "unfold" (unfold (lambda (x) (> x 5)) (lambda (x) (* x x)) (lambda (x) (+ x 1)) 1)
       '(1 4 9 16 25))
(check "unfold-right" (unfold-right zero? (lambda (x) (* x x)) (lambda (x) (- x 1)) 5)
       '(1 4 9 16 25))
(check "pair-for-each"
       (let ((acc '()))
         (pair-for-each (lambda (p) (set! acc (cons (car p) acc))) (list 1 2 3))
         (reverse acc))
       '(1 2 3))
(check "append-map" (append-map (lambda (x) (list x x)) (list 1 2 3)) '(1 1 2 2 3 3))
(check "filter-map" (filter-map (lambda (x) (and (even? x) (* x x))) (list 1 2 3 4)) '(4 16))

;;; ---- Searching ----

(check "find" (find even? (list 1 3 5 4 7)) 4)
(check "find: not found" (find even? (list 1 3 5)) #f)
(check "find-tail" (find-tail even? (list 1 3 5 4 7)) '(4 7))
(check "span" (call-with-values (lambda () (span odd? (list 1 3 5 4 7))) list) '((1 3 5) (4 7)))
(check "break" (call-with-values (lambda () (break even? (list 1 3 5 4 7))) list) '((1 3 5) (4 7)))
(check "list-index" (list-index even? (list 1 3 5 4)) 3)
(check "list-index: multi-list" (list-index < (list 3 1 4 1) (list 2 5 1 2)) 1)

;;; ---- Deleting duplicates ----

(check "delete-duplicates" (delete-duplicates (list 1 2 1 3 2 4)) '(1 2 3 4))
(check "delete-duplicates: custom pred"
       (delete-duplicates (list 1 2.0 1.0 3) =) '(1 2.0 3))

;;; ---- Append / concatenate / reverse ----

(check "concatenate" (concatenate (list (list 1 2) (list 3 4))) '(1 2 3 4))
(check "append!" (append! (list 1 2) (list 3 4)) '(1 2 3 4))
(check "append-reverse" (append-reverse (list 3 2 1) (list 4 5)) '(1 2 3 4 5))
(check "reverse!" (reverse! (list 1 2 3)) '(3 2 1))

;;; ---- Zip / unzip ----

(check "zip" (zip (list 1 2 3) (list 10 20 30)) '((1 10) (2 20) (3 30)))
(check "unzip1" (unzip1 (list (list 1 'a) (list 2 'b))) '(1 2))
(check "unzip2"
       (call-with-values (lambda () (unzip2 (list (list 1 10) (list 2 20)))) list)
       '((1 2) (10 20)))

;;; ---- Association lists ----

(check "alist-cons" (alist-cons 1 'a (list)) '((1 . a)))
(check "alist-copy independent"
       (let* ((a (list (cons 1 'x))) (b (alist-copy a)))
         (set-car! (car b) 99)
         (caar a))
       1)
(check "del-assq" (del-assq 1 (list (cons 1 'a) (cons 2 'b))) '((2 . b)))

;;; ---- Lists as sets ----

(check "lset<=" (lset<= = (list 1 2) (list 1 2 3)) #t)
(check "lset<=: false" (lset<= = (list 1 2 4) (list 1 2 3)) #f)
(check "lset=" (lset= = (list 1 2) (list 2 1)) #t)
(check "lset-union"
       (lset= = (lset-union = (list 1 2) (list 2 3)) (list 1 2 3)) #t)
(check "lset-intersection" (lset-intersection = (list 1 2 3) (list 2 3 4)) '(2 3))
(check "lset-difference" (lset-difference = (list 1 2 3) (list 2)) '(1 3))
(check "lset-adjoin: skips already-present elements"
       (lset= = (lset-adjoin = (list 1 2) 2 3) (list 1 2 3)) #t)

;;; ---- any / every: multi-list forms, and any's return value ----

(check "any: returns predicate's true value, not just #t"
       (any (lambda (x) (and (even? x) x)) (list 1 3 4 5)) 4)
(check "any: multi-list" (any < (list 1 5 3) (list 2 1 4)) #t)
(check "every: multi-list" (every < (list 1 2 3) (list 2 3 4)) #t)
(check "every: returns last predicate value on success"
       (every (lambda (x) (and (odd? x) x)) (list 1 3 5)) 5)

;;; ---- Tier 2 gap-closing additions ----

(call-with-values (lambda () (car+cdr (cons 1 2)))
  (lambda (a b) (check "car+cdr" (list a b) (list 1 2))))

(check "pair-fold walks pairs left-to-right"
       (pair-fold (lambda (pair acc) (cons (car pair) acc)) '() (list 1 2 3))
       (list 3 2 1))
(check "pair-fold-right walks pairs right-to-left"
       (pair-fold-right (lambda (pair acc) (cons (car pair) acc)) '() (list 1 2 3))
       (list 1 2 3))

(check "map-in-order single list" (map-in-order (lambda (x) (* x x)) (list 1 2 3 4))
       (list 1 4 9 16))
(check "map-in-order multi-list" (map-in-order + (list 1 2 3) (list 10 20 30))
       (list 11 22 33))
(let ((order '()))
  (map-in-order (lambda (x) (set! order (cons x order))) (list 1 2 3 4 5))
  (check "map-in-order applies strictly left-to-right" (reverse order) (list 1 2 3 4 5)))

(check "filter! same as filter" (filter! odd? (list 1 2 3 4 5)) (list 1 3 5))
(check "remove! same as remove" (remove! odd? (list 1 2 3 4 5)) (list 2 4))
(call-with-values (lambda () (partition! odd? (list 1 2 3 4 5)))
  (lambda (yes no) (check "partition!" (list yes no) (list (list 1 3 5) (list 2 4)))))

(check "length+ on a proper list" (length+ (list 1 2 3)) 3)
(check "length+ on a circular list is #f" (length+ (circular-list 1 2 3)) #f)

(check "except-last-pair drops the last element" (except-last-pair (list 1 2 3 4))
       (list 1 2 3))
(check "except-last-pair! same as except-last-pair" (except-last-pair! (list 1 2 3 4))
       (list 1 2 3))

(call-with-values (lambda () (lset-diff+intersection eqv? (list 1 2 3 4) (list 2 4)))
  (lambda (diff inter)
    (check "lset-diff+intersection: difference" diff (list 1 3))
    (check "lset-diff+intersection: intersection" inter (list 2 4))))

;;; ---- Summary ----

(newline)
(display "srfi-1 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
