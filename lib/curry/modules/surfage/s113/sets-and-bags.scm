(define-library (surfage s113 sets-and-bags)
  (import (scheme base) (surfage s128 comparators))
  (export
    ; sets — thin comparator-adapter wrapper over curry's native set (src/set.h)
    set set? set-contains? set-empty? set-disjoint?
    set-member set-adjoin set-adjoin! set-delete set-delete! set-delete-all set-delete-all!
    set-size set-find set-count set-any? set-every?
    set-map set-for-each set-fold set-filter set-filter! set-remove set-remove! set-partition
    set-copy set->list list->set
    set=? set<=? set<? set>=? set>?
    set-union set-union! set-intersection set-intersection!
    set-difference set-difference! set-xor set-xor!
    set-comparator
    ; bags — pure-Scheme multiset built on a comparator-adapted hash table
    bag bag? bag-contains? bag-empty?
    bag-size bag-unique-size bag-element-count
    bag-adjoin bag-adjoin! bag-delete bag-delete!
    bag-for-each bag-fold bag-map bag-filter bag-any? bag-every? bag-count
    bag-copy bag->list list->bag bag->alist alist->bag
    bag-union bag-union! bag-intersection bag-sum bag-sum! bag-product bag-product!
    bag=? bag-comparator)
  (begin

    ; Both sets and bags are bucketed under curry's three native equivalence
    ; modes (eq?/eqv?/equal?) exactly as (surfage s125 hash-tables) does —
    ; see that library's header comment for the rationale and its limits.
    ;
    ; This library reuses several of curry's native set primitive NAMES
    ; (set-union, set-copy, set=?, set-add!, ...) for its own, differently-
    ; shaped SRFI-113 API (variadic where the SRFI wants variadic, tracking
    ; a comparator per set for set-comparator/list->set/etc). The native
    ; procedures are captured under private %native- names up front, before
    ; any of the public names below are rebound, so the wrappers always call
    ; the primitive rather than recursing into themselves.

    (define %native-set-add!      set-add!)
    (define %native-set-delete!   set-delete!)   ; single-element, mutating
    (define %native-set-delete    set-delete)    ; single-element, non-destructive
    (define %native-set-member?   set-member?)
    (define %native-set->list     set->list)
    (define %native-list->set     list->set)
    (define %native-set-union     set-union)
    (define %native-set-intersection set-intersection)
    (define %native-set-difference set-difference)
    (define %native-set-symmetric-difference set-symmetric-difference)
    (define %native-set-subset?   set-subset?)
    (define %native-set-size      set-size)
    (define %native-set-copy      set-copy)
    (define %native-set-for-each  set-for-each)
    (define %native-set-map       set-map)
    (define %native-set-filter    set-filter)
    (define %native-set-filter!   set-filter!)
    (define %native-set-fold      set-fold)
    (define %native-set-any?      set-any?)
    (define %native-set-every?    set-every?)
    (define %native-set-count     set-count)
    (define %native-set-find      set-find)
    (define %native-set-empty?    set-empty?)

    (define %set-comparators (make-hash-table 0)) ; eq?-keyed: set -> comparator

    (define (%comparator->mode cmp)
      (cond ((eq? cmp eq-comparator) 0)
            ((eq? cmp eqv-comparator) 1)
            (else 2)))

    (define (%tag! s comparator) (hash-table-set! %set-comparators s comparator) s)

    (define (set-comparator s) (hash-table-ref %set-comparators s equal-comparator))

    (define (%new-set comparator) (%tag! (make-set (%comparator->mode comparator)) comparator))

    (define (set comparator . elts)
      (let ((s (%new-set comparator)))
        (for-each (lambda (e) (%native-set-add! s e)) elts)
        s))

    (define set-contains? %native-set-member?)
    (define set-empty? %native-set-empty?)
    (define set-size %native-set-size)
    (define set->list %native-set->list)
    (define set-for-each %native-set-for-each)
    (define set-fold %native-set-fold)
    (define set-any? %native-set-any?)
    (define set-every? %native-set-every?)
    (define set-count %native-set-count)
    (define set-find %native-set-find)

    (define (set-disjoint? s1 s2)
      (not (set-any? (lambda (e) (set-contains? s2 e)) s1)))

    (define (set-member s elt default) (if (set-contains? s elt) elt default))

    (define (set-adjoin s . elts)
      (let ((new (%native-set-copy s)))
        (for-each (lambda (e) (%native-set-add! new e)) elts)
        (%tag! new (set-comparator s))))

    (define (set-adjoin! s . elts)
      (for-each (lambda (e) (%native-set-add! s e)) elts)
      s)

    (define (set-delete s . elts)
      (fold-left (lambda (acc e) (%tag! (%native-set-delete acc e) (set-comparator s))) s elts))

    (define (set-delete! s . elts)
      (for-each (lambda (e) (%native-set-delete! s e)) elts)
      s)

    (define (set-delete-all s elts) (apply set-delete s elts))
    (define (set-delete-all! s elts) (apply set-delete! s elts))

    (define (set-map comparator proc s) (%tag! (%native-set-map proc s) comparator))
    (define (set-filter pred s) (%tag! (%native-set-filter pred s) (set-comparator s)))
    (define (set-filter! pred s) (%native-set-filter! pred s) s)
    (define (set-remove pred s) (set-filter (lambda (e) (not (pred e))) s))
    (define (set-remove! pred s) (set-filter! (lambda (e) (not (pred e))) s))
    (define (set-partition pred s) (values (set-filter pred s) (set-remove pred s)))

    (define (set-copy s) (%tag! (%native-set-copy s) (set-comparator s)))

    (define (list->set comparator lst) (%tag! (%native-list->set lst (%comparator->mode comparator)) comparator))

    (define (set=? . ss)
      (or (null? ss) (null? (cdr ss))
          (and (set-equal-2? (car ss) (cadr ss)) (apply set=? (cdr ss)))))
    (define (set-equal-2? s1 s2) (and (%native-set-subset? s1 s2) (%native-set-subset? s2 s1)))

    (define (set<=? s1 s2) (%native-set-subset? s1 s2))
    (define (set>=? s1 s2) (%native-set-subset? s2 s1))
    (define (set<? s1 s2) (and (set<=? s1 s2) (not (= (set-size s1) (set-size s2)))))
    (define (set>? s1 s2) (and (set>=? s1 s2) (not (= (set-size s1) (set-size s2)))))

    (define (set-union s1 s2) (%tag! (%native-set-union s1 s2) (set-comparator s1)))
    (define (set-union! s1 s2) (for-each (lambda (e) (%native-set-add! s1 e)) (set->list s2)) s1)
    (define (set-intersection s1 s2) (%tag! (%native-set-intersection s1 s2) (set-comparator s1)))
    (define (set-intersection! s1 s2)
      (for-each (lambda (e) (if (not (set-contains? s2 e)) (%native-set-delete! s1 e))) (set->list s1))
      s1)
    (define (set-difference s1 s2) (%tag! (%native-set-difference s1 s2) (set-comparator s1)))
    (define (set-difference! s1 s2)
      (for-each (lambda (e) (if (set-contains? s2 e) (%native-set-delete! s1 e))) (set->list s1))
      s1)
    (define (set-xor s1 s2) (%tag! (%native-set-symmetric-difference s1 s2) (set-comparator s1)))
    (define (set-xor! s1 s2) (%tag! (%native-set-symmetric-difference s1 s2) (set-comparator s1)))

    ;; ------------------------------------------------------------------
    ;; Bags — element -> count, built on a comparator-adapted hash table.
    ;; Simplified relative to the full SRFI-113 bag API (no bag-search!,
    ;; no separate mutable/immutable distinction beyond the usual `!` suffix
    ;; convention).
    ;; ------------------------------------------------------------------

    (define %bag-comparators (make-hash-table 0))

    (define (bag? obj) (and (hash-table? obj) (if (hash-table-ref %bag-comparators obj #f) #t #f)))

    (define (bag-comparator b) (hash-table-ref %bag-comparators b))

    (define (%new-bag comparator)
      (let ((h (make-hash-table (%comparator->mode comparator))))
        (hash-table-set! %bag-comparators h comparator)
        h))

    (define (bag comparator . elts)
      (let ((b (%new-bag comparator)))
        (for-each (lambda (e) (bag-adjoin! b e)) elts)
        b))

    (define (bag-element-count b elt) (hash-table-ref b elt 0))
    (define (bag-contains? b elt) (> (bag-element-count b elt) 0))
    (define (bag-unique-size b) (hash-table-size b))
    (define (bag-size b) (fold-left (lambda (acc kv) (+ acc (cdr kv))) 0 (hash-table->alist b)))
    (define (bag-empty? b) (= (bag-unique-size b) 0))

    (define (bag-adjoin! b . elts)
      (for-each (lambda (e) (hash-table-set! b e (+ 1 (bag-element-count b e)))) elts)
      b)

    (define (bag-adjoin b . elts)
      (let ((new (bag-copy b))) (for-each (lambda (e) (bag-adjoin! new e)) elts) new))

    (define (bag-delete! b . elts)
      (for-each (lambda (e)
                  (let ((n (bag-element-count b e)))
                    (if (<= n 1) (hash-table-delete! b e) (hash-table-set! b e (- n 1)))))
                elts)
      b)

    (define (bag-delete b . elts) (apply bag-delete! (bag-copy b) elts))

    (define (bag-copy b)
      (let ((new (%new-bag (bag-comparator b))))
        (for-each (lambda (kv) (hash-table-set! new (car kv) (cdr kv))) (hash-table->alist b))
        new))

    (define (bag->alist b) (hash-table->alist b))

    (define (alist->bag comparator alist)
      (let ((b (%new-bag comparator)))
        (for-each (lambda (kv) (hash-table-set! b (car kv) (cdr kv))) alist)
        b))

    (define (bag->list b)
      (apply append (map (lambda (kv) (make-list (cdr kv) (car kv))) (bag->alist b))))

    (define (list->bag comparator lst)
      (let ((b (%new-bag comparator))) (for-each (lambda (e) (bag-adjoin! b e)) lst) b))

    (define (bag-for-each proc b) (for-each proc (bag->list b)))

    (define (bag-fold proc knil b) (fold-left (lambda (acc e) (proc e acc)) knil (bag->list b)))

    (define (bag-map comparator proc b) (list->bag comparator (map proc (bag->list b))))

    (define (bag-filter pred b) (list->bag (bag-comparator b) (filter pred (bag->list b))))

    (define (bag-any? pred b)
      (let loop ((l (bag->list b))) (and (pair? l) (or (pred (car l)) (loop (cdr l))))))
    (define (bag-every? pred b)
      (let loop ((l (bag->list b))) (or (null? l) (and (pred (car l)) (loop (cdr l))))))
    (define (bag-count pred b) (length (filter pred (bag->list b))))

    (define (bag-union b1 b2)
      (let ((new (bag-copy b1)))
        (for-each (lambda (kv) (hash-table-set! new (car kv) (max (bag-element-count new (car kv)) (cdr kv))))
                  (bag->alist b2))
        new))
    (define (bag-union! b1 b2)
      (for-each (lambda (kv) (hash-table-set! b1 (car kv) (max (bag-element-count b1 (car kv)) (cdr kv))))
                (bag->alist b2))
      b1)

    (define (bag-intersection b1 b2)
      (let ((new (%new-bag (bag-comparator b1))))
        (for-each (lambda (kv) (let ((n (min (cdr kv) (bag-element-count b2 (car kv)))))
                                  (if (> n 0) (hash-table-set! new (car kv) n))))
                  (bag->alist b1))
        new))

    (define (bag-sum b1 b2)
      (let ((new (bag-copy b1)))
        (for-each (lambda (kv) (hash-table-set! new (car kv) (+ (bag-element-count new (car kv)) (cdr kv))))
                  (bag->alist b2))
        new))
    (define (bag-sum! b1 b2)
      (for-each (lambda (kv) (hash-table-set! b1 (car kv) (+ (bag-element-count b1 (car kv)) (cdr kv))))
                (bag->alist b2))
      b1)

    (define (bag-product n b)
      (let ((new (bag-copy b)))
        (for-each (lambda (kv) (hash-table-set! new (car kv) (* n (cdr kv)))) (bag->alist b))
        new))
    (define (bag-product! n b)
      (for-each (lambda (kv) (hash-table-set! b (car kv) (* n (cdr kv)))) (bag->alist b))
      b)

    (define (bag=? b1 b2)
      (and (= (bag-size b1) (bag-size b2))
           (let loop ((l (bag->alist b1)))
             (or (null? l) (and (= (cdar l) (bag-element-count b2 (caar l))) (loop (cdr l)))))))))
