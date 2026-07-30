(define-library (srfi s125 hash-tables)
  (import (scheme base) (srfi s128 comparators))
  (export
    make-hash-table hash-table hash-table-unfold hash-table?
    hash-table-comparator
    hash-table-contains? hash-table-exists? hash-table-empty?
    hash-table-size hash-table-count
    hash-table-ref hash-table-ref/default
    hash-table-set! hash-table-delete! hash-table-intern!
    hash-table-update! hash-table-update!/default
    hash-table-clear! hash-table-copy hash-table-empty-copy
    hash-table-keys hash-table-values hash-table-entries
    hash-table->alist alist->hash-table
    hash-table-walk hash-table-for-each hash-table-map->list
    hash-table-fold hash-table-count-matching
    hash-table-map! hash-table-prune!
    hash-table-union! hash-table-intersection! hash-table-difference!)
  (begin

    ; curry's native hash table (src/set.h) supports exactly three
    ; equivalences — eq?/eqv?/equal? — selected by an integer mode. A
    ; comparator's ordering is irrelevant to bucketing, only its equality
    ; matters, so: the built-in eq-comparator/eqv-comparator map to the
    ; matching native mode; every other comparator (including the common
    ; case of string-comparator, symbol-comparator, a custom comparator
    ; whose equality is equal?-compatible, or default-comparator) is bucketed
    ; under native equal? mode. This is exact whenever the comparator's
    ; equality predicate agrees with equal? (true for all the comparators
    ; this library ships and for the overwhelming majority of user-defined
    ; ones); a comparator with a genuinely finer-or-coarser custom equality
    ; than equal? will bucket correctly under equal? only to the extent its
    ; equality predicate agrees with equal?'s structural notion.
    (define %builtin-make-hash-table make-hash-table)
    (define %builtin-hash-table-ref  hash-table-ref)

    (define %table-comparators (%builtin-make-hash-table 0)) ; eq?-keyed: table -> comparator

    (define (%comparator->mode cmp)
      (cond ((eq? cmp eq-comparator) 0)
            ((eq? cmp eqv-comparator) 1)
            (else 2)))

    (define (make-hash-table comparator . args)
      (let ((t (%builtin-make-hash-table (%comparator->mode comparator))))
        (hash-table-set! %table-comparators t comparator)
        t))

    (define (hash-table comparator . kvs)
      (let ((t (make-hash-table comparator)))
        (let loop ((l kvs))
          (if (pair? l)
              (begin (hash-table-set! t (car l) (cadr l)) (loop (cddr l)))))
        t))

    (define (hash-table-unfold stop? mapper successor seed comparator . args)
      (let ((t (make-hash-table comparator)))
        (let loop ((s seed))
          (if (stop? s)
              t
              (call-with-values (lambda () (mapper s))
                (lambda (k v) (hash-table-set! t k v) (loop (successor s))))))))

    (define (hash-table-comparator t)
      (hash-table-ref/default %table-comparators t equal-comparator))

    (define (hash-table-empty? t) (= (hash-table-size t) 0))
    (define hash-table-count hash-table-size)
    (define hash-table-contains? hash-table-exists?)

    ;; SRFI-125's hash-table-ref differs from curry's own builtin of the same
    ;; name: the third argument is a failure THUNK, and a fourth argument is
    ;; a success procedure of one argument called with the found value.
    (define %hash-table-miss (list 'hash-table-miss))

    (define (hash-table-ref t key . opt)
      (let ((v (%builtin-hash-table-ref t key %hash-table-miss)))
        (cond ((not (eq? v %hash-table-miss))
               (if (and (pair? opt) (pair? (cdr opt))) ((cadr opt) v) v))
              ((pair? opt) ((car opt)))
              (else (error "hash-table-ref: key not found" key)))))

    (define (hash-table-ref/default t key default)
      (%builtin-hash-table-ref t key default))

    (define (hash-table-intern! t key failure)
      (let ((v (%builtin-hash-table-ref t key %hash-table-miss)))
        (if (eq? v %hash-table-miss)
            (let ((nv (failure))) (hash-table-set! t key nv) nv)
            v)))

    (define (hash-table-update! t key updater . opt)
      (hash-table-set! t key (updater (apply hash-table-ref t key opt))))

    (define (hash-table-update!/default t key updater default)
      (hash-table-set! t key (updater (hash-table-ref/default t key default))))

    (define (hash-table-clear! t)
      (for-each (lambda (k) (hash-table-delete! t k)) (hash-table-keys t)))

    (define (hash-table-empty-copy t) (make-hash-table (hash-table-comparator t)))

    (define (hash-table-copy t . mutable?)
      (let ((new (hash-table-empty-copy t)))
        (for-each (lambda (kv) (hash-table-set! new (car kv) (cdr kv))) (hash-table->alist t))
        new))

    (define (hash-table-entries t)
      (values (hash-table-keys t) (hash-table-values t)))

    (define (alist->hash-table alist comparator . args)
      (let ((t (make-hash-table comparator)))
        (for-each (lambda (kv) (hash-table-set! t (car kv) (cdr kv))) alist)
        t))

    (define (hash-table-walk t proc)
      (for-each (lambda (kv) (proc (car kv) (cdr kv))) (hash-table->alist t)))

    (define hash-table-for-each hash-table-walk)

    (define (hash-table-map->list proc t)
      (map (lambda (kv) (proc (car kv) (cdr kv))) (hash-table->alist t)))

    (define (hash-table-fold t proc init)
      (fold-left (lambda (acc kv) (proc (car kv) (cdr kv) acc)) init (hash-table->alist t)))

    (define (hash-table-count-matching t pred)
      (hash-table-fold t (lambda (k v acc) (if (pred k v) (+ acc 1) acc)) 0))

    (define (hash-table-map! proc t)
      (for-each (lambda (kv) (hash-table-set! t (car kv) (proc (car kv) (cdr kv))))
                (hash-table->alist t))
      t)

    (define (hash-table-prune! pred t)
      (for-each (lambda (kv) (if (pred (car kv) (cdr kv)) (hash-table-delete! t (car kv))))
                (hash-table->alist t))
      t)

    (define (hash-table-union! t1 t2)
      (for-each (lambda (kv) (if (not (hash-table-contains? t1 (car kv)))
                                  (hash-table-set! t1 (car kv) (cdr kv))))
                (hash-table->alist t2))
      t1)

    (define (hash-table-intersection! t1 t2)
      (for-each (lambda (kv) (if (not (hash-table-contains? t2 (car kv)))
                                  (hash-table-delete! t1 (car kv))))
                (hash-table->alist t1))
      t1)

    (define (hash-table-difference! t1 t2)
      (for-each (lambda (kv) (if (hash-table-contains? t2 (car kv))
                                  (hash-table-delete! t1 (car kv))))
                (hash-table->alist t1))
      t1)))
