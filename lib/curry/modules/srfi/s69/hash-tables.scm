(define-library (srfi s69 hash-tables)
  (import (scheme base))
  (export
    make-hash-table hash-table? alist->hash-table
    hash-table-equivalence-function hash-table-hash-function
    hash-table-ref hash-table-ref/default
    hash-table-set! hash-table-delete! hash-table-exists?
    hash-table-update! hash-table-update!/default
    hash-table-size hash-table-keys hash-table-values
    hash-table-walk hash-table-fold hash-table->alist
    hash-table-copy hash-table-merge!
    hash string-hash string-ci-hash hash-by-identity)
  (begin

    ; curry's own global make-hash-table/hash-table-ref (from (scheme base))
    ; are shadowed by the definitions below — capture them under private
    ; names first so the wrappers can still reach the real primitives.
    (define %builtin-make-hash-table make-hash-table)
    (define %builtin-hash-table-ref  hash-table-ref)

    ;; ---------------------------------------------------------------------
    ;; Comparator handling
    ;;
    ;; curry's underlying hash table (src/set.h) supports exactly three
    ;; comparator modes — eq?/eqv?/equal? — selected by an integer passed to
    ;; make-hash-table, not an arbitrary predicate procedure. SRFI-69 allows
    ;; any equivalence procedure; this module honors eq?/eqv?/equal? (and
    ;; the default, equal?) exactly, and raises a clear error for any other
    ;; procedure rather than silently mis-hashing keys under the wrong
    ;; equivalence. A side table (itself an eq?-keyed curry hash table)
    ;; tracks which comparator each table produced by this module's
    ;; make-hash-table/alist->hash-table was created with, purely so
    ;; hash-table-equivalence-function/hash-table-hash-function can answer
    ;; correctly — tables created directly via the raw builtin (not through
    ;; this module) fall back to reporting the equal?/hash default.
    ;; ---------------------------------------------------------------------

    (define %comparator-registry (%builtin-make-hash-table 0))

    (define (%comparator->mode pred)
      (cond ((or (not pred) (eq? pred equal?)) 2) ; SET_CMP_EQUAL
            ((eq? pred eqv?) 1)                    ; SET_CMP_EQV
            ((eq? pred eq?)  0)                     ; SET_CMP_EQ
            (else (error "make-hash-table: only eq?, eqv?, and equal? are supported as equivalence predicates" pred))))

    (define (make-hash-table . args)
      (let* ((equal-pred (if (pair? args) (car args) equal?))
             (mode (%comparator->mode equal-pred))
             (t (%builtin-make-hash-table mode)))
        (hash-table-set! %comparator-registry t equal-pred)
        t))

    (define (alist->hash-table alist . args)
      (let ((t (apply make-hash-table args)))
        (for-each (lambda (kv) (hash-table-set! t (car kv) (cdr kv))) alist)
        t))

    (define (hash-table-equivalence-function t)
      (hash-table-ref/default %comparator-registry t equal?))

    (define (hash-table-hash-function t)
      (let ((pred (hash-table-equivalence-function t)))
        (if (eq? pred equal?) hash hash-by-identity)))

    ;; ---------------------------------------------------------------------
    ;; Single-element operations
    ;; ---------------------------------------------------------------------

    ;; SRFI-69's hash-table-ref differs from curry's own builtin of the same
    ;; name: the third argument is a THUNK called on a miss (or an error is
    ;; signaled if it's absent), not a plain default value returned as-is.
    (define %hash-table-miss (list 'hash-table-miss))

    (define (hash-table-ref t key . opt)
      (let ((v (%builtin-hash-table-ref t key %hash-table-miss)))
        (if (eq? v %hash-table-miss)
            (if (pair? opt)
                ((car opt))
                (error "hash-table-ref: key not found" key))
            v)))

    (define (hash-table-ref/default t key default)
      (%builtin-hash-table-ref t key default))

    (define (hash-table-update! t key proc . opt)
      (hash-table-set! t key (proc (apply hash-table-ref t key opt))))

    (define (hash-table-update!/default t key proc default)
      (hash-table-set! t key (proc (hash-table-ref/default t key default))))

    ;; ---------------------------------------------------------------------
    ;; Whole-table operations
    ;; ---------------------------------------------------------------------

    (define (hash-table-walk t proc)
      (for-each (lambda (kv) (proc (car kv) (cdr kv))) (hash-table->alist t)))

    (define (hash-table-fold t f init)
      (fold-left (lambda (acc kv) (f (car kv) (cdr kv) acc)) init (hash-table->alist t)))

    (define (hash-table-copy t)
      (let ((new (make-hash-table (hash-table-equivalence-function t))))
        (for-each (lambda (kv) (hash-table-set! new (car kv) (cdr kv))) (hash-table->alist t))
        new))

    (define (hash-table-merge! t1 t2)
      (for-each (lambda (kv) (hash-table-set! t1 (car kv) (cdr kv))) (hash-table->alist t2))
      t1)

    ;; ---------------------------------------------------------------------
    ;; Hash functions
    ;;
    ;; Pure-Scheme polynomial rolling hash: not intended to match any other
    ;; implementation's exact values (the SRFI doesn't require that), only
    ;; to be internally consistent and equal?-respecting — two equal? values
    ;; produce the same printed text under `write`, hence the same hash.
    ;; ---------------------------------------------------------------------

    (define %default-hash-bound 1073741824) ; 2^30

    (define (%string-hash-raw s start)
      (let loop ((i 0) (acc start))
        (if (= i (string-length s))
            acc
            (loop (+ i 1) (modulo (+ (* acc 31) (char->integer (string-ref s i))) 4294967296)))))

    (define (string-hash s . opt)
        (modulo (%string-hash-raw s 17)
                (if (pair? opt) (car opt) %default-hash-bound)))

    (define (string-ci-hash s . opt)
      (modulo (%string-hash-raw (string-downcase s) 17)
              (if (pair? opt) (car opt) %default-hash-bound)))

    (define (hash obj . opt)
      (let ((bound (if (pair? opt) (car opt) %default-hash-bound))
            (s (let ((out (open-output-string))) (write obj out) (get-output-string out))))
        (modulo (%string-hash-raw s 17) bound)))

    ;; eq?-identity hash: numbers/chars hash by value (eqv?-consistent, since
    ;; small immediates are eq? whenever eqv?), everything else by object
    ;; identity via curry's own internal addressing exposed through eq-hash
    ;; is unavailable at the Scheme level, so this falls back to the same
    ;; printed-text hash as `hash` — acceptable per the SRFI (any hash
    ;; function whose collisions are a superset of eq?'s is technically
    ;; valid, and this is provided mainly for API completeness/symmetry
    ;; with hash-table-hash-function's eq?-table case, not raw speed).
    (define (hash-by-identity obj . opt)
      (apply hash obj opt))))
