(define-library (srfi s209 enums)
  (import (scheme base) (srfi s128 comparators))
  (export
    ; Enum types and enums
    enum-type? enum? enum-type-contains? enum=? enum<? enum>? enum<=? enum>=?
    make-enum-type
    enum-type enum-name enum-ordinal enum-value
    enum-name->enum enum-ordinal->enum enum-name->ordinal enum-name->value
    enum-ordinal->name enum-ordinal->value
    enum-type-size enum-min enum-max enum-type-enums enum-type-names enum-type-values
    enum-next enum-prev
    make-enum-comparator
    ; Enum sets: construction
    enum-empty-set enum-type->enum-set enum-set list->enum-set enum-set-copy
    enum-set-projection
    ; R6RS compatibility (deprecated)
    make-enumeration enum-set-universe enum-set-constructor enum-set-member?
    enum-set->list enum-set-indexer
    ; Enum sets: predicates
    enum-set? enum-set-contains? enum-set-empty? enum-set-disjoint?
    enum-set=? enum-set<? enum-set>? enum-set<=? enum-set>=? enum-set-subset?
    enum-set-any? enum-set-every?
    ; Enum sets: accessors
    enum-set-type
    ; Enum sets: mutation (functional and linear-update pairs)
    enum-set-adjoin enum-set-adjoin! enum-set-delete enum-set-delete!
    enum-set-delete-all enum-set-delete-all!
    ; Enum sets: whole-set operations
    enum-set-size enum-set->enum-list enum-set-count enum-set-filter
    enum-set-remove enum-set-map->list enum-set-for-each enum-set-fold
    ; Enum sets: logical operations (functional and linear-update pairs)
    enum-set-complement enum-set-complement!
    enum-set-union enum-set-union!
    enum-set-intersection enum-set-intersection!
    enum-set-difference enum-set-difference!
    enum-set-xor enum-set-xor!
    ; Syntax
    define-enum define-enumeration
    ; Internal helpers referenced directly from exported macro expansions.
    ; Not part of the SRFI-209 API — exported only because curry's
    ; syntax-rules macros resolve template identifiers in the use-site
    ; environment rather than the definition-site environment.
    %register-enum-type! %define-enum-lookup %define-enumeration-lookup
    %define-enum-set-ctor %normalize-enumeration-name-values)
  (begin

    ; ── enum types and enums ─────────────────────────────────────────────
    ; An enum-type's `enums-vec` field starts out #f and is patched in via
    ; the mutator right after construction (two-phase build): each <enum>
    ; needs a back-pointer to its own type, but the type's own enums-vec
    ; needs those same <enum> objects to already exist -- so the type
    ; record is allocated first (identity fixed, contents still empty),
    ; the enums are built referencing it, and only then is enums-vec filled
    ; in. The type object itself never changes identity, only that one
    ; internal field.

    (define-record-type <enum-type>
      (%make-enum-type enums-vec name-table)
      enum-type?
      (enums-vec  %enum-type-enums-vec %enum-type-enums-vec!)
      (name-table %enum-type-name-table))

    (define-record-type <enum>
      (%make-enum type name ordinal value)
      enum?
      (type     enum-type)
      (name     enum-name)
      (ordinal  enum-ordinal)
      (value    enum-value))

    (define (enum-type-contains? et e) (and (enum? e) (eq? (enum-type e) et)))

    (define (%enum-compare-chain cmp es)
      (or (null? es) (null? (cdr es))
          (let ((a (car es)) (b (cadr es)))
            (unless (eq? (enum-type a) (enum-type b))
              (error "enum comparison: enums belong to different enum types" a b))
            (and (cmp (enum-ordinal a) (enum-ordinal b))
                 (%enum-compare-chain cmp (cdr es))))))

    (define (enum=? . es)  (%enum-compare-chain =  es))
    (define (enum<? . es)  (%enum-compare-chain <  es))
    (define (enum>? . es)  (%enum-compare-chain >  es))
    (define (enum<=? . es) (%enum-compare-chain <= es))
    (define (enum>=? . es) (%enum-compare-chain >= es))

    (define (make-enum-type lst)
      (let* ((n (length lst))
             (name-table (make-hash-table))
             (et (%make-enum-type #f name-table))
             (enums (make-vector n)))
        (let loop ((i 0) (rest lst))
          (if (null? rest)
              (begin (%enum-type-enums-vec! et enums) et)
              (let* ((nv (car rest))
                     (name (if (pair? nv) (car nv) nv))
                     (value (if (pair? nv) (cadr nv) i)))
                (when (hash-table-ref name-table name #f)
                  (error "make-enum-type: duplicate enum name" name))
                (let ((e (%make-enum et name i value)))
                  (vector-set! enums i e)
                  (hash-table-set! name-table name e)
                  (loop (+ i 1) (cdr rest))))))))

    (define (enum-name->enum et name) (hash-table-ref (%enum-type-name-table et) name #f))

    (define (enum-ordinal->enum et ord)
      (let ((v (%enum-type-enums-vec et)))
        (if (and (exact-integer? ord) (>= ord 0) (< ord (vector-length v)))
            (vector-ref v ord)
            #f)))

    (define (enum-name->ordinal et name)
      (let ((e (enum-name->enum et name)))
        (if e (enum-ordinal e) (error "enum-name->ordinal: no such enum name" name))))

    (define (enum-name->value et name)
      (let ((e (enum-name->enum et name)))
        (if e (enum-value e) (error "enum-name->value: no such enum name" name))))

    (define (enum-ordinal->name et ord)
      (let ((e (enum-ordinal->enum et ord)))
        (if e (enum-name e) (error "enum-ordinal->name: ordinal out of range" ord))))

    (define (enum-ordinal->value et ord)
      (let ((e (enum-ordinal->enum et ord)))
        (if e (enum-value e) (error "enum-ordinal->value: ordinal out of range" ord))))

    (define (enum-type-size et) (vector-length (%enum-type-enums-vec et)))

    (define (enum-min et) (vector-ref (%enum-type-enums-vec et) 0))

    (define (enum-max et)
      (let ((v (%enum-type-enums-vec et))) (vector-ref v (- (vector-length v) 1))))

    (define (enum-type-enums et) (vector->list (%enum-type-enums-vec et)))
    (define (enum-type-names et) (map enum-name (enum-type-enums et)))
    (define (enum-type-values et) (map enum-value (enum-type-enums et)))

    (define (enum-next e) (enum-ordinal->enum (enum-type e) (+ (enum-ordinal e) 1)))
    (define (enum-prev e) (enum-ordinal->enum (enum-type e) (- (enum-ordinal e) 1)))

    (define (make-enum-comparator et)
      (make-comparator
        (lambda (x) (enum-type-contains? et x))
        (lambda (a b) (= (enum-ordinal a) (enum-ordinal b)))
        (lambda (a b) (< (enum-ordinal a) (enum-ordinal b)))
        (lambda (x) (enum-ordinal x))))

    ; ── enum sets ─────────────────────────────────────────────────────────
    ; Membership is a boolean vector indexed by ordinal -- enum types are
    ; small (a few dozen values, typically), so this keeps every operation
    ; below simple and O(type size) rather than needing a real set
    ; structure. The "!" mutators write through this vector in place; the
    ; plain versions copy it first.

    (define-record-type <enum-set>
      (%make-enum-set type membership)
      enum-set?
      (type       enum-set-type)
      (membership %enum-set-membership))

    (define (%check-enum-type-match! et e who)
      (unless (eq? (enum-type e) et)
        (error (string-append who ": enum belongs to a different enum type") e)))

    (define (%check-same-set-type! s1 s2 who)
      (unless (eq? (enum-set-type s1) (enum-set-type s2))
        (error (string-append who ": enum sets belong to different enum types") s1 s2)))

    (define (enum-empty-set et) (%make-enum-set et (make-vector (enum-type-size et) #f)))
    (define (enum-type->enum-set et) (%make-enum-set et (make-vector (enum-type-size et) #t)))

    (define (%enum-set-add! s e)
      (%check-enum-type-match! (enum-set-type s) e "enum-set")
      (vector-set! (%enum-set-membership s) (enum-ordinal e) #t))

    (define (%enum-set-remove! s e)
      (%check-enum-type-match! (enum-set-type s) e "enum-set-delete")
      (vector-set! (%enum-set-membership s) (enum-ordinal e) #f))

    (define (list->enum-set et lst)
      (let ((s (enum-empty-set et)))
        (for-each (lambda (e) (%enum-set-add! s e)) lst)
        s))

    (define (enum-set et . enums) (list->enum-set et enums))

    (define (enum-set-copy s) (%make-enum-set (enum-set-type s) (vector-copy (%enum-set-membership s))))

    (define (enum-set->enum-list s)
      (let* ((et (enum-set-type s))
             (v (%enum-set-membership s))
             (ev (%enum-type-enums-vec et))
             (n (vector-length v)))
        (let loop ((i (- n 1)) (acc '()))
          (if (< i 0)
              acc
              (loop (- i 1) (if (vector-ref v i) (cons (vector-ref ev i) acc) acc))))))

    (define (enum-set-projection target set)
      (let* ((target-type (if (enum-type? target) target (enum-set-type target)))
             (result (enum-empty-set target-type)))
        (for-each
          (lambda (e)
            (let ((target-enum (enum-name->enum target-type (enum-name e))))
              (if target-enum
                  (vector-set! (%enum-set-membership result) (enum-ordinal target-enum) #t)
                  (error "enum-set-projection: name not present in target enum type" (enum-name e)))))
          (enum-set->enum-list set))
        result))

    ; ── R6RS compatibility (deprecated) ────────────────────────────────────

    (define (make-enumeration symbols)
      (enum-type->enum-set (make-enum-type (map (lambda (s) (list s s)) symbols))))

    (define (enum-set-universe s) (enum-type->enum-set (enum-set-type s)))

    (define (enum-set-constructor s)
      (let ((et (enum-set-type s)))
        (lambda (symbols)
          (list->enum-set et
            (map (lambda (sym)
                   (or (enum-name->enum et sym)
                       (error "enum-set-constructor: unknown enum name" sym)))
                 symbols)))))

    (define (enum-set-member? sym s)
      (let ((e (enum-name->enum (enum-set-type s) sym)))
        (if e
            (vector-ref (%enum-set-membership s) (enum-ordinal e))
            (error "enum-set-member?: unknown enum name" sym))))

    (define (enum-set->list s) (map enum-name (enum-set->enum-list s)))

    (define (enum-set-indexer s)
      (let ((et (enum-set-type s)))
        (lambda (sym)
          (let ((e (enum-name->enum et sym)))
            (if e (enum-ordinal e) #f)))))

    ; ── enum set predicates ────────────────────────────────────────────────

    (define (enum-set-contains? s e)
      (%check-enum-type-match! (enum-set-type s) e "enum-set-contains?")
      (vector-ref (%enum-set-membership s) (enum-ordinal e)))

    (define (%bv-count-true v)
      (let ((n (vector-length v)))
        (let loop ((i 0) (c 0))
          (if (>= i n) c (loop (+ i 1) (if (vector-ref v i) (+ c 1) c))))))

    (define (enum-set-empty? s) (= 0 (%bv-count-true (%enum-set-membership s))))

    (define (%bv-every2 pred v1 v2)
      (let ((n (vector-length v1)))
        (let loop ((i 0))
          (or (>= i n) (and (pred (vector-ref v1 i) (vector-ref v2 i)) (loop (+ i 1)))))))

    (define (enum-set-disjoint? s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-disjoint?")
      (%bv-every2 (lambda (a b) (not (and a b))) (%enum-set-membership s1) (%enum-set-membership s2)))

    (define (%bv-equal? v1 v2)
      (%bv-every2 (lambda (a b) (eq? (and a #t) (and b #t))) v1 v2))

    (define (%bv-subset? v1 v2)
      (%bv-every2 (lambda (a b) (or (not a) b)) v1 v2))

    (define (enum-set=? s1 s2)
      (%check-same-set-type! s1 s2 "enum-set=?")
      (%bv-equal? (%enum-set-membership s1) (%enum-set-membership s2)))

    (define (enum-set<=? s1 s2)
      (%check-same-set-type! s1 s2 "enum-set<=?")
      (%bv-subset? (%enum-set-membership s1) (%enum-set-membership s2)))

    (define (enum-set>=? s1 s2)
      (%check-same-set-type! s1 s2 "enum-set>=?")
      (%bv-subset? (%enum-set-membership s2) (%enum-set-membership s1)))

    (define (enum-set<? s1 s2) (and (enum-set<=? s1 s2) (not (enum-set=? s1 s2))))
    (define (enum-set>? s1 s2) (and (enum-set>=? s1 s2) (not (enum-set=? s1 s2))))

    (define (enum-set-subset? s1 s2)
      ; Unlike the same-type comparators above, this compares by NAME and
      ; is explicitly allowed to span two different enum types.
      (let ((present (make-hash-table)))
        (for-each (lambda (e) (hash-table-set! present (enum-name e) #t)) (enum-set->enum-list s2))
        (let loop ((es (enum-set->enum-list s1)))
          (or (null? es)
              (and (hash-table-ref present (enum-name (car es)) #f)
                   (loop (cdr es)))))))

    (define (enum-set-any? pred s)
      (let loop ((es (enum-set->enum-list s)))
        (and (pair? es) (or (pred (car es)) (loop (cdr es))))))

    (define (enum-set-every? pred s)
      (let loop ((es (enum-set->enum-list s)))
        (or (null? es) (and (pred (car es)) (loop (cdr es))))))

    ; ── enum set mutation ───────────────────────────────────────────────────

    (define (enum-set-adjoin s . es)
      (let ((copy (enum-set-copy s))) (for-each (lambda (e) (%enum-set-add! copy e)) es) copy))

    (define (enum-set-adjoin! s . es)
      (for-each (lambda (e) (%enum-set-add! s e)) es) s)

    (define (enum-set-delete s . es)
      (let ((copy (enum-set-copy s))) (for-each (lambda (e) (%enum-set-remove! copy e)) es) copy))

    (define (enum-set-delete! s . es)
      (for-each (lambda (e) (%enum-set-remove! s e)) es) s)

    (define (enum-set-delete-all s lst)
      (let ((copy (enum-set-copy s))) (for-each (lambda (e) (%enum-set-remove! copy e)) lst) copy))

    (define (enum-set-delete-all! s lst)
      (for-each (lambda (e) (%enum-set-remove! s e)) lst) s)

    ; ── whole-set operations ────────────────────────────────────────────────

    (define (enum-set-size s) (%bv-count-true (%enum-set-membership s)))

    (define (enum-set-count pred s)
      (let loop ((es (enum-set->enum-list s)) (c 0))
        (if (null? es) c (loop (cdr es) (if (pred (car es)) (+ c 1) c)))))

    (define (enum-set-filter pred s)
      (list->enum-set (enum-set-type s) (filter pred (enum-set->enum-list s))))

    (define (enum-set-remove pred s)
      (list->enum-set (enum-set-type s) (filter (lambda (e) (not (pred e))) (enum-set->enum-list s))))

    (define (enum-set-map->list proc s) (map proc (enum-set->enum-list s)))
    (define (enum-set-for-each proc s) (for-each proc (enum-set->enum-list s)))

    (define (enum-set-fold proc nil s)
      (let loop ((es (enum-set->enum-list s)) (acc nil))
        (if (null? es) acc (loop (cdr es) (proc (car es) acc)))))

    ; ── logical operations ──────────────────────────────────────────────────

    (define (%bv-map1 f v)
      (let* ((n (vector-length v)) (out (make-vector n)))
        (let loop ((i 0))
          (if (< i n) (begin (vector-set! out i (f (vector-ref v i))) (loop (+ i 1))) out))))

    (define (%bv-map1! f v)
      (let ((n (vector-length v)))
        (let loop ((i 0))
          (if (< i n) (begin (vector-set! v i (f (vector-ref v i))) (loop (+ i 1))) v))))

    (define (%bv-map2 f v1 v2)
      (let* ((n (vector-length v1)) (out (make-vector n)))
        (let loop ((i 0))
          (if (< i n)
              (begin (vector-set! out i (f (vector-ref v1 i) (vector-ref v2 i))) (loop (+ i 1)))
              out))))

    (define (%bv-map2! f v1 v2)
      (let ((n (vector-length v1)))
        (let loop ((i 0))
          (if (< i n)
              (begin (vector-set! v1 i (f (vector-ref v1 i) (vector-ref v2 i))) (loop (+ i 1)))
              v1))))

    (define (enum-set-complement s)
      (%make-enum-set (enum-set-type s) (%bv-map1 not (%enum-set-membership s))))

    (define (enum-set-complement! s)
      (%bv-map1! not (%enum-set-membership s)) s)

    (define (enum-set-union s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-union")
      (%make-enum-set (enum-set-type s1)
        (%bv-map2 (lambda (a b) (or a b)) (%enum-set-membership s1) (%enum-set-membership s2))))

    (define (enum-set-union! s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-union!")
      (%bv-map2! (lambda (a b) (or a b)) (%enum-set-membership s1) (%enum-set-membership s2)) s1)

    (define (enum-set-intersection s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-intersection")
      (%make-enum-set (enum-set-type s1)
        (%bv-map2 (lambda (a b) (and a b)) (%enum-set-membership s1) (%enum-set-membership s2))))

    (define (enum-set-intersection! s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-intersection!")
      (%bv-map2! (lambda (a b) (and a b)) (%enum-set-membership s1) (%enum-set-membership s2)) s1)

    (define (enum-set-difference s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-difference")
      (%make-enum-set (enum-set-type s1)
        (%bv-map2 (lambda (a b) (and a (not b))) (%enum-set-membership s1) (%enum-set-membership s2))))

    (define (enum-set-difference! s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-difference!")
      (%bv-map2! (lambda (a b) (and a (not b))) (%enum-set-membership s1) (%enum-set-membership s2)) s1)

    (define (%xor a b) (and (or a b) (not (and a b))))

    (define (enum-set-xor s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-xor")
      (%make-enum-set (enum-set-type s1)
        (%bv-map2 %xor (%enum-set-membership s1) (%enum-set-membership s2))))

    (define (enum-set-xor! s1 s2)
      (%check-same-set-type! s1 s2 "enum-set-xor!")
      (%bv-map2! %xor (%enum-set-membership s1) (%enum-set-membership s2)) s1)

    ; ── define-enum / define-enumeration ────────────────────────────────────
    ; `type-name` must become a macro usable as `(type-name symbol)` --
    ; which means it can't ALSO be a plain top-level `define` binding for
    ; the enum-type value (a name is one or the other, not both), and the
    ; type-name and ctor-name macros both need to share the very SAME
    ; enum-type object underneath (so an enum built via one and an enum-set
    ; built via the other agree on membership). curry's define-syntax
    ; hygiene doesn't rename introduced top-level identifiers per macro
    ; expansion (confirmed: two separate invocations of a helper macro that
    ; each introduce their own top-level `(define %tmp ...)` collide on the
    ; same %tmp rather than getting distinct hygienic bindings), so a
    ; shared hidden temporary name written directly in this macro's
    ; template isn't safe across more than one define-enum use in the same
    ; scope. Instead, the enum-type is stashed in a private global registry,
    ; and both generated macros look it up there at their use sites.
    ;
    ; The registry key is the pair (quoted type-name . quoted ctor-name),
    ; not just 'type-name alone -- an earlier version keyed on 'type-name
    ; alone, but that collides across two SEPARATE libraries that each
    ; independently choose the same type-name (e.g. two libraries each
    ; defining their own unrelated `(define-enum size ...)`): even though
    ; ordinary top-level name collision already means only one `size` macro
    ; binding survives being imported into one flat scope, an R7RS renaming
    ; import (`(rename (liba) (size liba-size))`) would normally let a user
    ; disambiguate that -- but the registry lookup embeds the ORIGINAL,
    ; unrenamed 'type-name text at macro-definition time, so a rename import
    ; doesn't help: both renamed macros would still silently resolve to
    ; whichever entry was registered last. Folding in 'ctor-name as well
    ; doesn't eliminate the collision risk (two libraries could coincidentally
    ; pick the same pair of names too), only substantially narrows it --
    ; curry has no gensym/datum->syntax available to a plain syntax-rules
    ; template to close this completely. Programs combining `define-enum`
    ; types from multiple independently-authored libraries should give each
    ; a distinct type-name (and, belt-and-suspenders, a distinct
    ; constructor-name) to stay clear of this.

    (define %enum-type-registry (make-hash-table))

    (define (%register-enum-type! registry-key et)
      (hash-table-set! %enum-type-registry registry-key et)
      et)

    (define (%registered-enum-type registry-key)
      (or (hash-table-ref %enum-type-registry registry-key #f)
          (error "define-enum/define-enumeration: no such enum type" registry-key)))

    (define (%define-enum-lookup registry-key name-sym)
      (or (enum-name->enum (%registered-enum-type registry-key) name-sym)
          (error "define-enum: unknown enum name" name-sym)))

    (define (%define-enumeration-lookup registry-key name-sym)
      (if (enum-name->enum (%registered-enum-type registry-key) name-sym)
          name-sym
          (error "define-enumeration: unknown enum name" name-sym)))

    (define (%define-enum-set-ctor registry-key names)
      (let ((et (%registered-enum-type registry-key)))
        (list->enum-set et
          (map (lambda (n) (or (enum-name->enum et n) (error "unknown enum name" n))) names))))

    (define (%normalize-enumeration-name-values nvs)
      (map (lambda (nv) (if (pair? nv) nv (list nv nv))) nvs))

    (define-syntax define-enum
      (syntax-rules ()
        ((_ type-name (name-value ...) ctor-name)
         (begin
           (%register-enum-type! (cons 'type-name 'ctor-name) (make-enum-type '(name-value ...)))
           (define-syntax type-name
             (syntax-rules ()
               ((_ sym) (%define-enum-lookup (cons 'type-name 'ctor-name) 'sym))))
           (define-syntax ctor-name
             (syntax-rules ()
               ((_ sym (... ...))
                (%define-enum-set-ctor (cons 'type-name 'ctor-name) '(sym (... ...))))))))))

    (define-syntax define-enumeration
      (syntax-rules ()
        ((_ type-name (name-value ...) ctor-name)
         (begin
           (%register-enum-type! (cons 'type-name 'ctor-name)
             (make-enum-type (%normalize-enumeration-name-values '(name-value ...))))
           (define-syntax type-name
             (syntax-rules ()
               ((_ sym) (%define-enumeration-lookup (cons 'type-name 'ctor-name) 'sym))))
           (define-syntax ctor-name
             (syntax-rules ()
               ((_ sym (... ...))
                (%define-enum-set-ctor (cons 'type-name 'ctor-name) '(sym (... ...))))))))))))
