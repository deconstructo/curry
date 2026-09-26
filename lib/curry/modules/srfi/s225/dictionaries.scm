;;; SRFI-225: Dictionaries.
;;;
;;; https://srfi.schemers.org/srfi-225/ -- a uniform interface over
;;; "dictionary" (key/value mapping) types that don't otherwise share an
;;; API: alists, curry's own hash tables (several flavors), and any
;;; user-defined mapping type. A "Dictionary Type Object" (DTO) bundles
;;; the small set of primitive operations a given mapping type needs to
;;; support; every generic dict-* procedure below takes a DTO explicitly
;;; as its first argument and dispatches through it -- there is no
;;; global type registry or automatic predicate-based dispatch (the SRFI
;;; itself is written this way: every one of its own procedure
;;; signatures leads with `dto`).
;;;
;;; Only 7 primitives are REQUIRED per DTO -- dictionary?, dict-comparator,
;;; dict-find-update!, dict-map, dict-pure?, dict-remove, dict-size --
;;; matching the SRFI's own dependency graph. Every other generic
;;; procedure (dict-ref, dict-set!, dict-fold, dict-for-each, ...) is
;;; DERIVED from those 7 by dto-ref (below) unless a DTO's own make-dto
;;; call supplies a more direct/optimized implementation for that
;;; proc-id. This mirrors the SRFI's own stated architecture ("all
;;; others derive from these via composition") and is why make-dto only
;;; ever raises on a MISSING required proc-id -- every optional one
;;; always has a working, if not maximally efficient, fallback.
;;;
;;; dict-find-update! is the one genuine "workhorse" primitive: reads
;;; (dict-ref, dict-contains?), pure inserts/replacements (dict-set!,
;;; dict-adjoin!, dict-replace!, dict-intern!) and deletions (dict-
;;; delete!) are ALL expressible as one dict-find-update! call whose own
;;; failure/success callbacks choose which of dict-find-update!'s own
;;; four inner continuations (insert/ignore on absence, update/delete on
;;; presence) to invoke -- see %derive-default's dict-ref-id/dict-set!-id/
;;; etc. cases below for exactly how each read/write op reduces to this
;;; one call.
;;;
;;; Two conditionally-exported DTOs from the SRFI's own text --
;;; mapping-dto/hash-mapping-dto (SRFI 146 persistent mappings) and a
;;; SRFI 167 bytevector-store DTO -- are omitted: neither SRFI is
;;; implemented in curry. What IS shipped: eqv-alist-dto/equal-alist-dto
;;; (unconditional, per the SRFI's own text), srfi-69-dto (over
;;; (srfi s69 hash-tables)), and hash-table-dto/srfi-126-dto (both over
;;; (srfi s126 hashtables) -- curry's own "R6RS-flavored naming" hash
;;; table layer is the closest thing curry has to genuine (rnrs
;;; hashtables), so both spec-named DTOs point at the same underlying
;;; wrapper; there is no functional difference between R6RS's own
;;; hashtables and SRFI 126's in curry, since s126 IS curry's R6RS
;;; hashtable story).

(define-library (srfi s225 dictionaries)
  (import (scheme base) (srfi s128 comparators) (srfi s158 generators-and-accumulators)
          (prefix (srfi s69 hash-tables) %s69-)
          (prefix (srfi s126 hashtables) %ht-))
  (export
    ;; Core DTO machinery
    dto? make-dto dto-ref
    ;; Predicates
    dictionary? dict-empty? dict-contains? dict=? dict-pure?
    ;; Accessors
    dict-ref dict-ref/default dict-comparator
    ;; Update procedures
    dict-set! dict-adjoin! dict-delete! dict-delete-all! dict-replace!
    dict-intern! dict-update! dict-update/default! dict-pop!
    dict-find-update!
    ;; Mapping/filtering
    dict-map dict-filter dict-remove
    ;; Whole-dictionary operations
    dict-size dict-count dict-any dict-every
    dict-keys dict-values dict-entries dict-fold dict-map->list dict->alist
    ;; Iteration
    dict-for-each dict->generator dict-set!-accumulator dict-adjoin!-accumulator
    ;; Errors
    dictionary-error dictionary-error? dictionary-message dictionary-irritants
    ;; Alist DTO constructor
    make-alist-dto eqv-alist-dto equal-alist-dto
    ;; Hash-table-backed DTOs
    srfi-69-dto hash-table-dto srfi-126-dto
    ;; Required proc-ids
    dictionary?-id dict-find-update!-id dict-comparator-id dict-map-id
    dict-pure?-id dict-remove-id dict-size-id
    ;; Optional proc-ids
    dict->alist-id dict-adjoin!-id dict-adjoin!-accumulator-id
    dict-any-id dict-every-id dict-contains?-id dict-count-id
    dict-delete!-id dict-delete-all!-id dict-empty?-id
    dict-entries-id dict-filter-id dict-fold-id dict-for-each-id
    dict-intern!-id dict-keys-id dict-map->list-id dict-pop!-id
    dict-ref-id dict-ref/default-id dict-replace!-id
    dict-set!-id dict-set!-accumulator-id
    dict-update!-id dict-update/default!-id dict-values-id dict=?-id
    dict->generator-id
    ;; Internal helper the exported %derive-default reaches -- kept
    ;; unexported deliberately (not part of the SRFI's own surface, and
    ;; no exported macro's expansion reaches it, so it needs no re-export
    ;; per this codebase's usual cross-library-macro rule).
    )
  (begin

    ;; ── Proc-ids ─────────────────────────────────────────────────────────────
    ;; Plain, self-naming symbols -- unique by content (interned), directly
    ;; usable as make-dto's/dto-ref's own arguments and as hash-table keys.

    (define dictionary?-id             'dictionary?-id)
    (define dict-find-update!-id       'dict-find-update!-id)
    (define dict-comparator-id         'dict-comparator-id)
    (define dict-map-id                'dict-map-id)
    (define dict-pure?-id              'dict-pure?-id)
    (define dict-remove-id             'dict-remove-id)
    (define dict-size-id               'dict-size-id)

    (define dict->alist-id             'dict->alist-id)
    (define dict-adjoin!-id            'dict-adjoin!-id)
    (define dict-adjoin!-accumulator-id 'dict-adjoin!-accumulator-id)
    (define dict-any-id                'dict-any-id)
    (define dict-every-id              'dict-every-id)
    (define dict-contains?-id          'dict-contains?-id)
    (define dict-count-id              'dict-count-id)
    (define dict-delete!-id            'dict-delete!-id)
    (define dict-delete-all!-id        'dict-delete-all!-id)
    (define dict-empty?-id             'dict-empty?-id)
    (define dict-entries-id            'dict-entries-id)
    (define dict-filter-id             'dict-filter-id)
    (define dict-fold-id               'dict-fold-id)
    (define dict-for-each-id           'dict-for-each-id)
    (define dict-intern!-id            'dict-intern!-id)
    (define dict-keys-id               'dict-keys-id)
    (define dict-map->list-id          'dict-map->list-id)
    (define dict-pop!-id               'dict-pop!-id)
    (define dict-ref-id                'dict-ref-id)
    (define dict-ref/default-id        'dict-ref/default-id)
    (define dict-replace!-id           'dict-replace!-id)
    (define dict-set!-id               'dict-set!-id)
    (define dict-set!-accumulator-id   'dict-set!-accumulator-id)
    (define dict-update!-id            'dict-update!-id)
    (define dict-update/default!-id    'dict-update/default!-id)
    (define dict-values-id             'dict-values-id)
    (define dict=?-id                  'dict=?-id)
    (define dict->generator-id         'dict->generator-id)

    (define %required-ids
      (list dictionary?-id dict-find-update!-id dict-comparator-id dict-map-id
            dict-pure?-id dict-remove-id dict-size-id))

    ;; ── Dictionary errors ────────────────────────────────────────────────────

    (define-record-type <dictionary-error>
      (%make-dictionary-error message irritants)
      dictionary-error?
      (message   dictionary-message)
      (irritants dictionary-irritants))

    (define (dictionary-error message . irritants)
      (raise (%make-dictionary-error message irritants)))

    ;; ── DTOs ─────────────────────────────────────────────────────────────────
    ;; A DTO is a hash table from proc-id symbol -> procedure (or, for a
    ;; derived-and-cached optional proc-id, the derived closure -- dto-ref
    ;; caches a derivation back into this same table the first time it's
    ;; asked for, honoring the SRFI's own stated dto-ref rationale
    ;; ("enabling efficient repeated calls") instead of re-deriving a
    ;; fresh closure on every single generic-procedure call site.

    (define-record-type <dto>
      (%make-dto table)
      dto?
      (table %dto-table))

    (define (make-dto . plist)
      (let ((table (make-hash-table)))
        (let loop ((p plist))
          (cond
            ((null? p) #f)
            ((null? (cdr p))
             (dictionary-error "make-dto: odd number of arguments (proc-id with no procedure)" plist))
            (else (hash-table-set! table (car p) (cadr p)) (loop (cddr p)))))
        (for-each
          (lambda (id)
            (if (not (hash-table-exists? table id))
                (dictionary-error "make-dto: missing required proc-id" id)))
          %required-ids)
        (%make-dto table)))

    (define (dto-ref dto proc-id)
      (let ((table (%dto-table dto)))
        (if (hash-table-exists? table proc-id)
            (hash-table-ref table proc-id)
            (let ((derived (%derive-default dto proc-id)))
              (hash-table-set! table proc-id derived)
              derived))))

    ;; Every derivation below is expressed purely in terms of OTHER
    ;; dto-ref calls (never a raw table lookup), so a DTO that overrides
    ;; one optional proc-id (say, a native dict-for-each for a real
    ;; ordered structure) automatically improves every OTHER derived
    ;; proc-id built on top of it too (dict-fold, dict-keys, dict-any,
    ;; ...) with no extra wiring.
    (define (%derive-default dto proc-id)
      (cond
        ((eq? proc-id dict-for-each-id)
         (lambda (proc dict . range)
           (let* ((cmp (dict-comparator dto dict))
                  (lt  (and cmp (comparator-ordered? cmp) (comparator-ordering-predicate cmp)))
                  (start (if (pair? range) (car range) #f))
                  (end   (if (and (pair? range) (pair? (cdr range))) (cadr range) #f)))
             ((dto-ref dto dict-map-id)
               (lambda (k v)
                 (if (or (not lt)
                         (and (or (not start) (not (lt k start)))
                              (or (not end) (lt k end))))
                     (proc k v))
                 v)
               dict)
             (if #f #f))))
        ((eq? proc-id dict-fold-id)
         (lambda (proc knil dict)
           (let ((acc knil))
             ((dto-ref dto dict-for-each-id) (lambda (k v) (set! acc (proc k v acc))) dict)
             acc)))
        ((eq? proc-id dict-keys-id)
         (lambda (dict)
           (reverse ((dto-ref dto dict-fold-id) (lambda (k v acc) (cons k acc)) '() dict))))
        ((eq? proc-id dict-values-id)
         (lambda (dict)
           (reverse ((dto-ref dto dict-fold-id) (lambda (k v acc) (cons v acc)) '() dict))))
        ((eq? proc-id dict-entries-id)
         (lambda (dict)
           (let ((ks '()) (vs '()))
             ((dto-ref dto dict-for-each-id)
               (lambda (k v) (set! ks (cons k ks)) (set! vs (cons v vs)))
               dict)
             (values (reverse ks) (reverse vs)))))
        ((eq? proc-id dict->alist-id)
         (lambda (dict)
           (reverse ((dto-ref dto dict-fold-id) (lambda (k v acc) (cons (cons k v) acc)) '() dict))))
        ((eq? proc-id dict-map->list-id)
         (lambda (proc dict)
           (reverse ((dto-ref dto dict-fold-id) (lambda (k v acc) (cons (proc k v) acc)) '() dict))))
        ((eq? proc-id dict-count-id)
         (lambda (pred dict)
           ((dto-ref dto dict-fold-id) (lambda (k v acc) (if (pred k v) (+ acc 1) acc)) 0 dict)))
        ((eq? proc-id dict-any-id)
         (lambda (pred dict)
           (call-with-current-continuation
             (lambda (return)
               ((dto-ref dto dict-for-each-id)
                 (lambda (k v) (let ((r (pred k v))) (if r (return r))))
                 dict)
               #f))))
        ((eq? proc-id dict-every-id)
         (lambda (pred dict)
           (call-with-current-continuation
             (lambda (return)
               (let ((last #t))
                 ((dto-ref dto dict-for-each-id)
                   (lambda (k v) (let ((r (pred k v))) (if r (set! last r) (return #f))))
                   dict)
                 last)))))
        ((eq? proc-id dict-empty?-id)
         (lambda (dict) (zero? (dict-size dto dict))))
        ((eq? proc-id dict-filter-id)
         (lambda (pred dict) ((dto-ref dto dict-remove-id) (lambda (k v) (not (pred k v))) dict)))
        ((eq? proc-id dict-ref-id)
         (lambda (dict key . opt)
           (let ((failure (if (pair? opt) (car opt)
                               (lambda () (dictionary-error "dict-ref: key not found" key))))
                 (success (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) (lambda (v) v))))
             ((dto-ref dto dict-find-update!-id) dict key
               (lambda (insert ignore) (failure))
               (lambda (mk v update delete) (success v))))))
        ((eq? proc-id dict-ref/default-id)
         (lambda (dict key default) (dict-ref dto dict key (lambda () default))))
        ((eq? proc-id dict-contains?-id)
         (lambda (dict key)
           (let ((sentinel (list 'missing)))
             (not (eq? sentinel (dict-ref dto dict key (lambda () sentinel)))))))
        ((eq? proc-id dict-set!-id)
         (lambda (dict . objs)
           (let loop ((d dict) (os objs))
             (if (null? os) d
                 (let ((k (car os)) (v (cadr os)))
                   (loop ((dto-ref dto dict-find-update!-id) d k
                           (lambda (insert ignore) (insert v))
                           (lambda (mk mv update delete) (update k v)))
                         (cddr os)))))))
        ((eq? proc-id dict-adjoin!-id)
         (lambda (dict . objs)
           (let loop ((d dict) (os objs))
             (if (null? os) d
                 (let ((k (car os)) (v (cadr os)))
                   (loop ((dto-ref dto dict-find-update!-id) d k
                           (lambda (insert ignore) (insert v))
                           (lambda (mk mv update delete) (update mk mv)))
                         (cddr os)))))))
        ((eq? proc-id dict-delete!-id)
         (lambda (dict . keys)
           (let loop ((d dict) (ks keys))
             (if (null? ks) d
                 (loop ((dto-ref dto dict-find-update!-id) d (car ks)
                         (lambda (insert ignore) (ignore))
                         (lambda (mk mv update delete) (delete)))
                       (cdr ks))))))
        ((eq? proc-id dict-delete-all!-id)
         (lambda (dict keylist) (apply (dto-ref dto dict-delete!-id) dict keylist)))
        ((eq? proc-id dict-replace!-id)
         (lambda (dict key value)
           ((dto-ref dto dict-find-update!-id) dict key
             (lambda (insert ignore) (ignore))
             (lambda (mk mv update delete) (update key value)))))
        ((eq? proc-id dict-intern!-id)
         (lambda (dict key failure)
           ((dto-ref dto dict-find-update!-id) dict key
             (lambda (insert ignore) (let ((v (failure))) (values (insert v) v)))
             (lambda (mk mv update delete) (values (update mk mv) mv)))))
        ((eq? proc-id dict-update!-id)
         (lambda (dict key updater . opt)
           (let ((failure (if (pair? opt) (car opt)
                               (lambda () (dictionary-error "dict-update!: key not found" key))))
                 (success (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) (lambda (v) v))))
             ((dto-ref dto dict-set!-id) dict key
               (updater (dict-ref dto dict key failure success))))))
        ((eq? proc-id dict-update/default!-id)
         (lambda (dict key updater default)
           ((dto-ref dto dict-set!-id) dict key
             (updater (dict-ref/default dto dict key default)))))
        ((eq? proc-id dict-pop!-id)
         (lambda (dict)
           (if (dict-empty? dto dict)
               (dictionary-error "dict-pop!: dictionary is empty")
               (call-with-current-continuation
                 (lambda (return)
                   ((dto-ref dto dict-for-each-id)
                     (lambda (k v)
                       (return (values ((dto-ref dto dict-delete!-id) dict k) k v)))
                     dict)
                   (dictionary-error "dict-pop!: dictionary is empty"))))))
        ((eq? proc-id dict=?-id)
         (lambda (val=? dict1 dict2)
           (let ((sentinel (list 'missing)))
             (and (= (dict-size dto dict1) (dict-size dto dict2))
                  (dict-every dto
                    (lambda (k v1)
                      (let ((v2 (dict-ref dto dict2 k (lambda () sentinel))))
                        (and (not (eq? v2 sentinel)) (val=? v1 v2))))
                    dict1)))))
        ((eq? proc-id dict->generator-id)
         (lambda (dict . range)
           (let ((items '()))
             (apply (dto-ref dto dict-for-each-id)
                    (lambda (k v) (set! items (cons (cons k v) items)))
                    dict range)
             (list->generator (reverse items)))))
        ((eq? proc-id dict-set!-accumulator-id)
         (lambda (dict)
           (let ((d dict))
             (lambda (kv)
               (if (eof-object? kv) d
                   (begin (set! d (dict-set! dto d (car kv) (cdr kv))) d))))))
        ((eq? proc-id dict-adjoin!-accumulator-id)
         (lambda (dict)
           (let ((d dict))
             (lambda (kv)
               (if (eof-object? kv) d
                   (begin (set! d (dict-adjoin! dto d (car kv) (cdr kv))) d))))))
        (else
         (dictionary-error "dto-ref: no default derivation available for proc-id" proc-id))))

    ;; ── Generic procedures ───────────────────────────────────────────────────
    ;; Thin, uniformly-shaped wrappers: look the proc-id up (native or
    ;; derived, dto-ref doesn't care which) and apply it. Required
    ;; proc-ids call the DTO's own procedure directly without going
    ;; through dto-ref's derivation path at all -- there is never
    ;; anything to derive for these seven.

    (define (dictionary? dto obj) ((hash-table-ref (%dto-table dto) dictionary?-id) obj))
    (define (dict-comparator dto dict) ((hash-table-ref (%dto-table dto) dict-comparator-id) dict))
    (define (dict-find-update! dto dict key failure success)
      ((hash-table-ref (%dto-table dto) dict-find-update!-id) dict key failure success))
    (define (dict-map dto proc dict) ((hash-table-ref (%dto-table dto) dict-map-id) proc dict))
    (define (dict-pure? dto dict) ((hash-table-ref (%dto-table dto) dict-pure?-id) dict))
    (define (dict-remove dto pred dict) ((hash-table-ref (%dto-table dto) dict-remove-id) pred dict))
    (define (dict-size dto dict) ((hash-table-ref (%dto-table dto) dict-size-id) dict))

    (define (dict-empty? dto dict) ((dto-ref dto dict-empty?-id) dict))
    (define (dict-contains? dto dict key) ((dto-ref dto dict-contains?-id) dict key))
    (define (dict=? dto val=? dict1 dict2) ((dto-ref dto dict=?-id) val=? dict1 dict2))

    (define (dict-ref dto dict key . opt) (apply (dto-ref dto dict-ref-id) dict key opt))
    (define (dict-ref/default dto dict key default) ((dto-ref dto dict-ref/default-id) dict key default))

    (define (dict-set! dto dict . objs) (apply (dto-ref dto dict-set!-id) dict objs))
    (define (dict-adjoin! dto dict . objs) (apply (dto-ref dto dict-adjoin!-id) dict objs))
    (define (dict-delete! dto dict . keys) (apply (dto-ref dto dict-delete!-id) dict keys))
    (define (dict-delete-all! dto dict keylist) ((dto-ref dto dict-delete-all!-id) dict keylist))
    (define (dict-replace! dto dict key value) ((dto-ref dto dict-replace!-id) dict key value))
    (define (dict-intern! dto dict key failure) ((dto-ref dto dict-intern!-id) dict key failure))
    (define (dict-update! dto dict key updater . opt)
      (apply (dto-ref dto dict-update!-id) dict key updater opt))
    (define (dict-update/default! dto dict key updater default)
      ((dto-ref dto dict-update/default!-id) dict key updater default))
    (define (dict-pop! dto dict) ((dto-ref dto dict-pop!-id) dict))

    (define (dict-filter dto pred dict) ((dto-ref dto dict-filter-id) pred dict))

    (define (dict-count dto pred dict) ((dto-ref dto dict-count-id) pred dict))
    (define (dict-any dto pred dict) ((dto-ref dto dict-any-id) pred dict))
    (define (dict-every dto pred dict) ((dto-ref dto dict-every-id) pred dict))
    (define (dict-keys dto dict) ((dto-ref dto dict-keys-id) dict))
    (define (dict-values dto dict) ((dto-ref dto dict-values-id) dict))
    (define (dict-entries dto dict) ((dto-ref dto dict-entries-id) dict))
    (define (dict-fold dto proc knil dict) ((dto-ref dto dict-fold-id) proc knil dict))
    (define (dict-map->list dto proc dict) ((dto-ref dto dict-map->list-id) proc dict))
    (define (dict->alist dto dict) ((dto-ref dto dict->alist-id) dict))

    (define (dict-for-each dto proc dict . range) (apply (dto-ref dto dict-for-each-id) proc dict range))
    (define (dict->generator dto dict . range) (apply (dto-ref dto dict->generator-id) dict range))
    (define (dict-set!-accumulator dto dict) ((dto-ref dto dict-set!-accumulator-id) dict))
    (define (dict-adjoin!-accumulator dto dict) ((dto-ref dto dict-adjoin!-accumulator-id) dict))

    ;; ── Alist DTOs ───────────────────────────────────────────────────────────
    ;; Pure: every update returns a NEW list; the input alist argument is
    ;; never itself mutated. "Associations with new keys are added to the
    ;; beginning" (SRFI text) -- see the insert continuation below.

    (define (%alist-replace equal? key nk nv alist)
      (map (lambda (kv) (if (equal? (car kv) key) (cons nk nv) kv)) alist))
    (define (%alist-remove equal? key alist)
      (let loop ((lst alist) (acc '()))
        (cond
          ((null? lst) (reverse acc))
          ((equal? (caar lst) key) (append (reverse acc) (cdr lst)))
          (else (loop (cdr lst) (cons (car lst) acc))))))

    (define (make-alist-dto equal?)
      (make-dto
        dictionary?-id (lambda (obj)
                         (and (or (null? obj) (pair? obj))
                              (let loop ((l obj))
                                (cond ((null? l) #t)
                                      ((and (pair? l) (pair? (car l))) (loop (cdr l)))
                                      (else #f)))))
        dict-comparator-id (lambda (dict) (make-comparator (lambda (x) #t) equal? #f #f))
        dict-find-update!-id
          (lambda (alist key failure success)
            (let loop ((lst alist))
              (cond
                ((null? lst)
                 (failure (lambda (v) (cons (cons key v) alist))
                          (lambda () alist)))
                ((equal? (caar lst) key)
                 (success (caar lst) (cdar lst)
                   (lambda (nk nv) (%alist-replace equal? key nk nv alist))
                   (lambda () (%alist-remove equal? key alist))))
                (else (loop (cdr lst))))))
        dict-map-id (lambda (proc alist) (map (lambda (kv) (cons (car kv) (proc (car kv) (cdr kv)))) alist))
        dict-pure?-id (lambda (dict) #t)
        dict-remove-id (lambda (pred alist)
                         (let loop ((lst alist) (acc '()))
                           (cond ((null? lst) (reverse acc))
                                 ((pred (caar lst) (cdar lst)) (loop (cdr lst) acc))
                                 (else (loop (cdr lst) (cons (car lst) acc))))))
        dict-size-id (lambda (alist) (length alist))))

    (define eqv-alist-dto (make-alist-dto eqv?))
    (define equal-alist-dto (make-alist-dto equal?))

    ;; ── SRFI 69 hash-table DTO (impure) ──────────────────────────────────────

    (define srfi-69-dto
      (make-dto
        dictionary?-id %s69-hash-table?
        dict-comparator-id
          (lambda (dict) (make-comparator (lambda (x) #t)
                                           (%s69-hash-table-equivalence-function dict)
                                           #f (%s69-hash-table-hash-function dict)))
        dict-find-update!-id
          (lambda (dict key failure success)
            (if (%s69-hash-table-exists? dict key)
                (success key (%s69-hash-table-ref/default dict key #f)
                  (lambda (nk nv) (%s69-hash-table-set! dict nk nv) dict)
                  (lambda () (%s69-hash-table-delete! dict key) dict))
                (failure (lambda (v) (%s69-hash-table-set! dict key v) dict)
                         (lambda () dict))))
        dict-map-id
          (lambda (proc dict)
            (let ((new (%s69-make-hash-table (%s69-hash-table-equivalence-function dict))))
              (%s69-hash-table-walk dict (lambda (k v) (%s69-hash-table-set! new k (proc k v))))
              new))
        dict-pure?-id (lambda (dict) #f)
        dict-remove-id
          (lambda (pred dict)
            (let ((new (%s69-make-hash-table (%s69-hash-table-equivalence-function dict))))
              (%s69-hash-table-walk dict (lambda (k v) (if (not (pred k v)) (%s69-hash-table-set! new k v))))
              new))
        dict-size-id %s69-hash-table-size
        dict-for-each-id (lambda (proc dict . range) (%s69-hash-table-walk dict proc))
        dict->alist-id %s69-hash-table->alist
        dict-keys-id %s69-hash-table-keys
        dict-values-id %s69-hash-table-values))

    ;; ── SRFI 126 / R6RS-flavored hashtable DTO (impure) ──────────────────────
    ;; See this file's own header comment for why hash-table-dto and
    ;; srfi-126-dto are the same underlying wrapper in curry.

    (define %hashtable-dto
      (make-dto
        dictionary?-id %ht-hashtable?
        dict-comparator-id
          (lambda (dict) (make-comparator (lambda (x) #t)
                                           (%ht-hashtable-equivalence-function dict)
                                           #f (or (%ht-hashtable-hash-function dict) (lambda (x) 0))))
        dict-find-update!-id
          (lambda (dict key failure success)
            (if (%ht-hashtable-contains? dict key)
                (success key (%ht-hashtable-ref dict key #f)
                  (lambda (nk nv) (%ht-hashtable-set! dict nk nv) dict)
                  (lambda () (%ht-hashtable-delete! dict key) dict))
                (failure (lambda (v) (%ht-hashtable-set! dict key v) dict)
                         (lambda () dict))))
        dict-map-id
          (lambda (proc dict)
            (let ((new (%ht-hashtable-copy dict)))
              (%ht-hashtable-walk dict (lambda (k v) (%ht-hashtable-set! new k (proc k v))))
              new))
        dict-pure?-id (lambda (dict) #f)
        dict-remove-id
          (lambda (pred dict)
            (let ((new (%ht-hashtable-copy dict)))
              (%ht-hashtable-walk dict (lambda (k v) (if (pred k v) (%ht-hashtable-delete! new k))))
              new))
        dict-size-id %ht-hashtable-size
        dict-for-each-id (lambda (proc dict . range) (%ht-hashtable-walk dict proc))
        dict->alist-id %ht-hashtable->alist
        dict-keys-id %ht-hashtable-keys
        dict-values-id %ht-hashtable-values))

    (define hash-table-dto %hashtable-dto)
    (define srfi-126-dto %hashtable-dto)))
