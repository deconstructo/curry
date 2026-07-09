;;; (curry sets) — Generalized set theories
;;;
;;; This module provides two abstractions that extend the core hash-set:
;;;
;;;   <multiset>     — elements with integer multiplicities (bags)
;;;                    Supports union (max), intersection (min), sum (+), difference.
;;;
;;;   <logical-set>  — elements with membership in any (curry logic) logic.
;;;                    Under classical-logic:   membership ∈ {#f, #t}   ≡ plain set
;;;                    Under fuzzy-logic:       membership ∈ [0.0, 1.0] = fuzzy set
;;;                    Under belnap-four:       membership ∈ {N, F, T, B} = paraconsistent set
;;;                    Under probabilistic-logic: membership ∈ [0.0, 1.0] = probabilistic set
;;;                    Algebra (∪, ∩, complement, \) is pointwise via the logic's join/meet/not.
;;;
;;; Requires (curry logic) — but caller typically imports it too for the logic values.
;;;
;;; Example:
;;;   (import (curry logic))
;;;   (import (curry sets))
;;;
;;;   ;; Fuzzy set of "warm" temperatures
;;;   (let ((warm (fuzzy-set 20 0.3  25 0.7  30 1.0  35 0.9  40 0.5)))
;;;     (fuzzy-alpha-cut warm 0.6))  => classical set {25, 30, 35, 40}
;;;
;;;   ;; Paraconsistent set: conflicting sources
;;;   (let ((s (belnap-set)))
;;;     (logical-set-assert! s 'paris 'T)    ; source A: Paris is in Europe
;;;     (logical-set-assert! s 'paris 'F)    ; source B: Paris is not in Europe (typo)
;;;     (logical-set-member s 'paris))       => 'B  (both — contradiction, not explosion)
;;;
;;;   ;; Multiset word frequency
;;;   (let ((words (list->multiset '(the cat sat on the mat the cat))))
;;;     (multiset-count words 'the))         => 3

(import (curry logic))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Internal helpers
;;; ══════════════════════════════════════════════════════════════════════════

(define (%elements-union-list table-a table-b)
  ;; Return a deduplicated list of all keys present in either hash-table.
  (let ((seen (make-set)))
    (for-each (lambda (k) (set-add! seen k)) (hash-table-keys table-a))
    (for-each (lambda (k) (set-add! seen k)) (hash-table-keys table-b))
    (set->list seen)))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Multisets (bags)
;;;
;;; A multiset is a hash-table mapping elements to their count (a positive
;;; integer).  Elements with count 0 are removed from the table.
;;;
;;; The two natural "union" operations:
;;;   multiset-union        — takes the MAX of each element's count
;;;   multiset-sum          — takes the SUM of counts (bag concatenation)
;;;
;;; multiset-union + multiset-intersection form a distributive lattice.
;;; multiset-sum   + multiset-intersection form the free commutative monoid.
;;; ══════════════════════════════════════════════════════════════════════════

(define-record-type <multiset>
  (%make-multiset table)
  multiset?
  (table %ms-table))

;;; ── Construction ──

(define (make-multiset)
  (%make-multiset (make-hash-table)))

(define (multiset . elems)
  (list->multiset elems))

(define (list->multiset lst)
  (let ((ms (make-multiset)))
    (for-each (lambda (e) (multiset-add! ms e)) lst)
    ms))

(define (alist->multiset alist)
  ;; Each pair is (element . count).
  (let ((ms (make-multiset)))
    (for-each (lambda (pair) (multiset-set-count! ms (car pair) (cdr pair))) alist)
    ms))

(define (multiset-copy ms)
  (alist->multiset (multiset->alist ms)))

;;; ── Access ──

(define (multiset-count ms elem)
  (hash-table-ref (%ms-table ms) elem 0))

(define (multiset-member? ms elem)
  (> (multiset-count ms elem) 0))

(define (multiset-size ms)
  ;; Number of distinct elements.
  (hash-table-size (%ms-table ms)))

(define (multiset-total ms)
  ;; Sum of all counts.
  (fold-left + 0 (hash-table-values (%ms-table ms))))

(define (multiset-empty? ms)
  (= (hash-table-size (%ms-table ms)) 0))

(define (multiset-elements ms)
  (hash-table-keys (%ms-table ms)))

(define (multiset->alist ms)
  (hash-table->alist (%ms-table ms)))

(define (multiset->list ms)
  ;; Returns a list with each element repeated according to its count.
  (let ((result '()))
    (for-each
      (lambda (pair)
        (let loop ((n (cdr pair)))
          (when (> n 0)
            (set! result (cons (car pair) result))
            (loop (- n 1)))))
      (multiset->alist ms))
    result))

;;; ── Mutation ──

(define (multiset-set-count! ms elem n)
  (if (<= n 0)
      (hash-table-delete! (%ms-table ms) elem)
      (hash-table-set! (%ms-table ms) elem n)))

(define (multiset-add! ms elem)
  (multiset-set-count! ms elem (+ (multiset-count ms elem) 1)))

(define (multiset-add-n! ms elem n)
  (when (> n 0)
    (multiset-set-count! ms elem (+ (multiset-count ms elem) n))))

(define (multiset-remove! ms elem)
  (let ((c (multiset-count ms elem)))
    (when (> c 0) (multiset-set-count! ms elem (- c 1)))))

(define (multiset-remove-all! ms elem)
  (hash-table-delete! (%ms-table ms) elem))

;;; ── Non-destructive variants ──

(define (multiset-add ms elem)
  (let ((r (multiset-copy ms))) (multiset-add! r elem) r))

(define (multiset-remove ms elem)
  (let ((r (multiset-copy ms))) (multiset-remove! r elem) r))

;;; ── Equality ──

(define (multiset=? a b)
  (and (= (hash-table-size (%ms-table a)) (hash-table-size (%ms-table b)))
       (let loop ((pairs (multiset->alist a)))
         (cond ((null? pairs) #t)
               ((= (cdar pairs) (multiset-count b (caar pairs)))
                (loop (cdr pairs)))
               (else #f)))))

(define (multiset-subset? a b)
  (let loop ((pairs (multiset->alist a)))
    (cond ((null? pairs) #t)
          ((<= (cdar pairs) (multiset-count b (caar pairs)))
           (loop (cdr pairs)))
          (else #f))))

;;; ── Algebra ──

(define (%ms-combine f a b)
  ;; Apply binary count function f over the union of both element sets.
  (let ((r (make-multiset)))
    (for-each
      (lambda (e)
        (let ((c (f (multiset-count a e) (multiset-count b e))))
          (when (> c 0) (multiset-set-count! r e c))))
      (%elements-union-list (%ms-table a) (%ms-table b)))
    r))

(define (multiset-union a b)
  ;; max of counts: largest multiplicity from either bag
  (%ms-combine max a b))

(define (multiset-intersection a b)
  ;; min of counts: shared copies only
  (%ms-combine min a b))

(define (multiset-sum a b)
  ;; + of counts: all copies from both bags (bag concatenation)
  (%ms-combine + a b))

(define (multiset-difference a b)
  ;; max(0, count_a - count_b): copies in a that b doesn't account for
  (%ms-combine (lambda (ca cb) (max 0 (- ca cb))) a b))

(define (multiset-scale ms n)
  ;; Multiply every count by n.
  (let ((r (make-multiset)))
    (for-each (lambda (pair)
                (let ((c (* (cdr pair) n)))
                  (when (> c 0) (multiset-set-count! r (car pair) c))))
              (multiset->alist ms))
    r))

;;; ── Higher-order (proc receives element + count) ──

(define (multiset-for-each proc ms)
  (for-each (lambda (pair) (proc (car pair) (cdr pair))) (multiset->alist ms)))

(define (multiset-map proc ms)
  ;; (proc elem count) → (new-elem . new-count)
  ;; Counts for duplicate new-elems are summed.
  (let ((r (make-multiset)))
    (for-each (lambda (pair)
                (let ((res (proc (car pair) (cdr pair))))
                  (multiset-add-n! r (car res) (cdr res))))
              (multiset->alist ms))
    r))

(define (multiset-filter pred ms)
  ;; (pred elem count) → bool: keep element with its count
  (let ((r (make-multiset)))
    (for-each (lambda (pair)
                (when (pred (car pair) (cdr pair))
                  (multiset-set-count! r (car pair) (cdr pair))))
              (multiset->alist ms))
    r))

(define (multiset-fold proc init ms)
  ;; (proc acc elem count) → acc
  (fold-left (lambda (acc pair) (proc acc (car pair) (cdr pair)))
             init
             (multiset->alist ms)))

(define (multiset-any? pred ms)
  (let loop ((pairs (multiset->alist ms)))
    (cond ((null? pairs) #f)
          ((pred (caar pairs) (cdar pairs)) #t)
          (else (loop (cdr pairs))))))

(define (multiset-every? pred ms)
  (let loop ((pairs (multiset->alist ms)))
    (cond ((null? pairs) #t)
          ((not (pred (caar pairs) (cdar pairs))) #f)
          (else (loop (cdr pairs))))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logical sets
;;;
;;; A <logical-set> maps elements to truth-values in a given (curry logic)
;;; logic.  Elements absent from the table have implicit membership equal to
;;; `logic-bottom` (the "no information" value).
;;;
;;; logical-set-assert! accumulates evidence using `logic-combine` — exactly
;;; the same mechanism as `kb-assert!` in the logic KB.  The difference is
;;; that a logical-set is about MEMBERSHIP (what elements are in the set),
;;; not about propositions.
;;;
;;; Set algebra is pointwise:
;;;   union        → (join  mem_a(x)  mem_b(x)) for all known x
;;;   intersection → (meet  mem_a(x)  mem_b(x)) for all known x
;;;   complement   → (not   mem_s(x))            for each x in s
;;;   difference   → a ∩ ¬b, computed directly to handle implicit membership
;;;
;;; NOTE on complement and difference:
;;;   The complement of a finite logical-set is only defined over its known
;;;   elements.  Elements not in the table have implicit membership `bottom`,
;;;   whose complement is `(complement bottom)` — not stored explicitly.
;;;   `logical-set-difference` handles this correctly without materialising
;;;   an infinite complement set.
;;; ══════════════════════════════════════════════════════════════════════════

(define-record-type <logical-set>
  (%make-logical-set logic table)
  logical-set?
  (logic logical-set-logic)
  (table %lset-table))

;;; ── Construction ──

(define (make-logical-set logic)
  (%make-logical-set logic (make-hash-table)))

(define (logical-set logic . elems)
  ;; All elements get top (= fully a member) membership.
  (let ((s (make-logical-set logic)))
    (for-each (lambda (e) (logical-set-assert! s e (logic-top logic))) elems)
    s))

(define (list->logical-set logic lst)
  (apply logical-set logic lst))

(define (alist->logical-set logic alist)
  ;; Each pair is (element . truth-value).
  (let ((s (make-logical-set logic)))
    (for-each (lambda (pair) (logical-set-assert! s (car pair) (cdr pair))) alist)
    s))

(define (logical-set-copy s)
  (alist->logical-set (logical-set-logic s) (logical-set->alist s)))

;;; ── Access ──

(define (logical-set-member s elem)
  ;; Returns the truth-value of elem's membership (bottom if absent).
  (hash-table-ref (%lset-table s)
                  elem
                  (logic-bottom (logical-set-logic s))))

(define (logical-set-contains? s elem)
  ;; Boolean: is membership "true enough" (designated in the logic)?
  ((logic-entails? (logical-set-logic s)) (logical-set-member s elem)))

(define (logical-set-empty? s)
  (= (hash-table-size (%lset-table s)) 0))

(define (logical-set-size s)
  (hash-table-size (%lset-table s)))

(define (logical-set->alist s)
  (hash-table->alist (%lset-table s)))

(define (logical-set-elements s)
  (hash-table-keys (%lset-table s)))

;;; ── Mutation ──

(define (logical-set-assert! s elem tv)
  ;; Combine new evidence tv with existing membership via logic-combine,
  ;; and store the result even if it equals bottom.  This makes explicitly-
  ;; tracked "unknown" elements (e.g. Belnap 'N) visible via logical-set-size,
  ;; logical-set-elements, and belnap-set-unknowns.
  ;; Use logical-set-retract! to remove an element from tracking entirely.
  (let* ((logic (logical-set-logic s))
         (old   (logical-set-member s elem))
         (new   ((logic-combine logic) old tv)))
    (hash-table-set! (%lset-table s) elem new)
    new))

(define (logical-set-retract! s elem)
  (hash-table-delete! (%lset-table s) elem))

;;; ── Algebra ──

(define (%lset-pointwise op logic a b)
  ;; Apply binary pointwise operation over the union of known elements.
  ;; Elements absent from a set contribute logic-bottom.
  (let ((bot (logic-bottom logic))
        (r   (make-logical-set logic)))
    (for-each
      (lambda (elem)
        (let ((tv (op (logical-set-member a elem)
                      (logical-set-member b elem))))
          (unless (equal? tv bot)
            (hash-table-set! (%lset-table r) elem tv))))
      (%elements-union-list (%lset-table a) (%lset-table b)))
    r))

(define (logical-set-union a b)
  (%lset-pointwise (logic-join (logical-set-logic a)) (logical-set-logic a) a b))

(define (logical-set-intersection a b)
  (%lset-pointwise (logic-meet (logical-set-logic a)) (logical-set-logic a) a b))

(define (logical-set-complement s)
  ;; Negate the truth-value of each explicit element.
  ;; Implicit elements (absent) retain their implicit complement: (¬ bottom).
  (let* ((logic (logical-set-logic s))
         (neg   (logic-complement logic))
         (bot   (logic-bottom logic))
         (r     (make-logical-set logic)))
    (for-each
      (lambda (pair)
        (let ((tv (neg (cdr pair))))
          (unless (equal? tv bot)
            (hash-table-set! (%lset-table r) (car pair) tv))))
      (hash-table->alist (%lset-table s)))
    r))

(define (logical-set-difference a b)
  ;; a \ b — elements of a whose membership is not entailed by b.
  ;; Equivalent to a ∩ ¬b, but computed directly so that elements in a
  ;; but absent from b correctly inherit (¬ bottom) from the complement.
  (let* ((logic (logical-set-logic a))
         (meet  (logic-meet logic))
         (neg   (logic-complement logic))
         (bot   (logic-bottom logic))
         (r     (make-logical-set logic)))
    (for-each
      (lambda (pair)
        (let* ((elem (car pair))
               (a-tv (cdr pair))
               (b-tv (logical-set-member b elem))
               (tv   (meet a-tv (neg b-tv))))
          (unless (equal? tv bot)
            (hash-table-set! (%lset-table r) elem tv))))
      (hash-table->alist (%lset-table a)))
    r))

(define (logical-set-symmetric-difference a b)
  (logical-set-union (logical-set-difference a b)
                     (logical-set-difference b a)))

;;; ── Higher-order (proc receives element + truth-value) ──

(define (logical-set-for-each proc s)
  (for-each (lambda (pair) (proc (car pair) (cdr pair)))
            (hash-table->alist (%lset-table s))))

(define (logical-set-map proc s)
  ;; (proc elem tv) → (new-elem . new-tv)
  (let ((r (make-logical-set (logical-set-logic s))))
    (for-each
      (lambda (pair)
        (let ((res (proc (car pair) (cdr pair))))
          (hash-table-set! (%lset-table r) (car res) (cdr res))))
      (hash-table->alist (%lset-table s)))
    r))

(define (logical-set-filter pred s)
  ;; (pred elem tv) → bool: keep this element?
  (let ((r (make-logical-set (logical-set-logic s))))
    (for-each
      (lambda (pair)
        (when (pred (car pair) (cdr pair))
          (hash-table-set! (%lset-table r) (car pair) (cdr pair))))
      (hash-table->alist (%lset-table s)))
    r))

(define (logical-set-fold proc init s)
  ;; (proc acc elem tv) → acc
  (fold-left (lambda (acc pair) (proc acc (car pair) (cdr pair)))
             init
             (hash-table->alist (%lset-table s))))

(define (logical-set-any? pred s)
  ;; (pred elem tv) → bool; short-circuits
  (let loop ((pairs (hash-table->alist (%lset-table s))))
    (cond ((null? pairs) #f)
          ((pred (caar pairs) (cdar pairs)) #t)
          (else (loop (cdr pairs))))))

(define (logical-set-every? pred s)
  (let loop ((pairs (hash-table->alist (%lset-table s))))
    (cond ((null? pairs) #t)
          ((not (pred (caar pairs) (cdar pairs))) #f)
          (else (loop (cdr pairs))))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Convenience constructors and derived operations
;;; ══════════════════════════════════════════════════════════════════════════

;;; Fuzzy set — elements with optional degree arguments
;;; (fuzzy-set 'hot 1.0 'warm 0.6 'cool)  → cool gets degree 1.0 (fully a member)
(define (fuzzy-set . args)
  (let ((s (make-logical-set fuzzy-logic)))
    (let loop ((items args))
      (unless (null? items)
        (if (and (pair? (cdr items)) (number? (cadr items)))
            (begin
              (logical-set-assert! s (car items) (exact->inexact (cadr items)))
              (loop (cddr items)))
            (begin
              (logical-set-assert! s (car items) 1.0)
              (loop (cdr items))))))
    s))

;;; Belnap set — elements with optional Belnap value
;;; (belnap-set 'P 'T 'Q 'B 'R)  → R gets 'T
(define (belnap-set . args)
  (let ((s (make-logical-set belnap-four))
        (belnap-values '(N F T B)))
    (let loop ((items args))
      (unless (null? items)
        (if (and (pair? (cdr items)) (memq (cadr items) belnap-values))
            (begin
              (logical-set-assert! s (car items) (cadr items))
              (loop (cddr items)))
            (begin
              (logical-set-assert! s (car items) 'T)
              (loop (cdr items))))))
    s))

;;; Fuzzy alpha-cut: classical set of elements with degree > alpha
(define (fuzzy-alpha-cut fs alpha)
  (let ((r (make-set)))
    (logical-set-for-each
      (lambda (elem degree)
        (when (> degree alpha) (set-add! r elem)))
      fs)
    r))

;;; Paraconsistent diagnostics for belnap sets
(define (belnap-set-contradictions s)
  ;; Elements where membership is 'B (both true and false).
  (map car (filter (lambda (p) (eq? (cdr p) 'B)) (logical-set->alist s))))

(define (belnap-set-unknowns s)
  ;; Elements where membership is 'N (no information either way).
  (map car (filter (lambda (p) (eq? (cdr p) 'N)) (logical-set->alist s))))

;;; Convert a classical logical-set to a core hash-set (for interop)
(define (logical-set->set s)
  (let ((r (make-set)))
    (logical-set-for-each
      (lambda (elem tv)
        (when ((logic-entails? (logical-set-logic s)) tv)
          (set-add! r elem)))
      s)
    r))

;;; Lift a core hash-set into a logical-set under the given logic
(define (set->logical-set logic s)
  (let ((ls (make-logical-set logic)))
    (for-each (lambda (e) (logical-set-assert! ls e (logic-top logic)))
              (set->list s))
    ls))
