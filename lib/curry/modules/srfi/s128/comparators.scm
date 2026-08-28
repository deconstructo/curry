(define-library (srfi s128 comparators)
  (import (scheme base) (scheme write))
  (export
    comparator? comparator-ordered? comparator-hashable?
    make-comparator
    comparator-type-test-predicate comparator-equality-predicate
    comparator-ordering-predicate comparator-hash-function
    comparator-test-type comparator-check-type comparator-hash
    =? <? >? <=? >=?
    comparator-register-default! default-comparator make-default-comparator
    boolean-comparator real-comparator number-comparator
    char-comparator char-ci-comparator
    string-comparator string-ci-comparator symbol-comparator
    pair-comparator list-comparator vector-comparator
    eq-comparator eqv-comparator equal-comparator
    make-eq-comparator make-eqv-comparator make-equal-comparator
    boolean-hash char-hash char-ci-hash string-hash string-ci-hash
    symbol-hash number-hash default-hash hash-bound hash-salt
    comparator-if<=>)
  (begin

    ;; ------------------------------------------------------------------
    ;; Comparator record
    ;;
    ;; ordering/hash are #f when the comparator doesn't support them
    ;; (comparator-ordered?/comparator-hashable? report that; the
    ;; corresponding accessor raises if called anyway).
    ;; ------------------------------------------------------------------

    (define-record-type <comparator>
      (%make-comparator type-test equality ordering hash)
      comparator?
      (type-test %comparator-type-test)
      (equality  %comparator-equality)
      (ordering  %comparator-ordering)
      (hash      %comparator-hash))

    (define (make-comparator type-test equality ordering hash)
      (%make-comparator (if (eq? type-test #t) (lambda (x) #t) type-test)
                         equality ordering hash))

    (define (comparator-ordered? cmp) (if (%comparator-ordering cmp) #t #f))
    (define (comparator-hashable? cmp) (if (%comparator-hash cmp) #t #f))

    (define (comparator-type-test-predicate cmp) (%comparator-type-test cmp))
    (define (comparator-equality-predicate cmp) (%comparator-equality cmp))

    (define (comparator-ordering-predicate cmp)
      (or (%comparator-ordering cmp)
          (error "comparator-ordering-predicate: comparator is not ordered" cmp)))

    (define (comparator-hash-function cmp)
      (or (%comparator-hash cmp)
          (error "comparator-hash-function: comparator is not hashable" cmp)))

    (define (comparator-test-type cmp obj) ((%comparator-type-test cmp) obj))

    (define (comparator-check-type cmp obj)
      (if (comparator-test-type cmp obj)
          obj
          (error "comparator-check-type: object does not satisfy comparator's type test" obj cmp)))

    (define (comparator-hash cmp obj) ((comparator-hash-function cmp) obj))

    ;; ------------------------------------------------------------------
    ;; Variadic chained comparison predicates
    ;; ------------------------------------------------------------------

    (define (%chain pred cmp objs)
      (or (null? objs) (null? (cdr objs))
          (and (pred (car objs) (cadr objs)) (%chain pred cmp (cdr objs)))))

    (define (=? cmp . objs) (%chain (comparator-equality-predicate cmp) cmp objs))
    (define (<? cmp . objs) (%chain (comparator-ordering-predicate cmp) cmp objs))
    (define (>? cmp . objs) (%chain (lambda (a b) ((comparator-ordering-predicate cmp) b a)) cmp objs))
    (define (<=? cmp . objs) (%chain (lambda (a b) (not ((comparator-ordering-predicate cmp) b a))) cmp objs))
    (define (>=? cmp . objs) (%chain (lambda (a b) (not ((comparator-ordering-predicate cmp) a b))) cmp objs))

    ;; (comparator-if<=> [comparator] a b less-branch equal-branch greater-branch)
    ;; -- computes the 3-way comparison once (equality first, then
    ;; ordering) and evaluates exactly one branch. comparator is optional
    ;; per spec, defaulting to (default-comparator); the two clauses
    ;; below dispatch on that via ordinary syntax-rules arity matching
    ;; (5 sub-forms vs. 6), which is unambiguous since the two shapes
    ;; have different lengths.
    (define-syntax comparator-if<=>
      (syntax-rules ()
        ((_ a b less eq gtr)
         (comparator-if<=> default-comparator a b less eq gtr))
        ((_ comparator a b less eq gtr)
         (cond ((=? comparator a b) eq)
               ((<? comparator a b) less)
               (else gtr)))))

    ;; ------------------------------------------------------------------
    ;; Basic-type comparators
    ;; ------------------------------------------------------------------

    ;; Self-contained write-based hash (mirrors (srfi s69 hash-tables)'s
    ;; `hash` without depending on that library): two equal? values print
    ;; identically under `write`, hence hash identically.
    (define (%default-hash obj)
      (let loop ((i 0) (acc 17)
                 (s (let ((out (open-output-string))) (write obj out) (get-output-string out))))
        (if (= i (string-length s))
            (modulo acc 1073741824)
            (loop (+ i 1) (modulo (+ (* acc 31) (char->integer (string-ref s i))) 4294967296) s))))

    (define eq-comparator (%make-comparator (lambda (x) #t) eq? #f #f))
    (define eqv-comparator (%make-comparator (lambda (x) #t) eqv? #f %default-hash))
    (define equal-comparator (%make-comparator (lambda (x) #t) equal? #f %default-hash))

    ; The spec's typed constructors alongside the ready-made comparator
    ; values above -- both forms are standard SRFI-128, just two ways to
    ; get the same object (a zero-arg procedure call vs. a plain value).
    (define (make-eq-comparator) eq-comparator)
    (define (make-eqv-comparator) eqv-comparator)
    (define (make-equal-comparator) equal-comparator)

    ; The spec's standalone hash procedures, alongside the comparators
    ; above that already use %default-hash internally for the same
    ; types -- %default-hash's write-then-hash approach already handles
    ; every one of these types correctly (write produces distinguishing
    ; text per type: #t/#f, #\a, "str", a-symbol, 42), so each of these
    ; is a direct call except the two case-insensitive ones, which fold
    ; case first so char-ci=?/string-ci=?-equal values hash the same.
    (define (boolean-hash b) (%default-hash b))
    (define (char-hash c) (%default-hash c))
    (define (char-ci-hash c) (%default-hash (char-foldcase c)))
    (define (string-hash s) (%default-hash s))
    (define (string-ci-hash s) (%default-hash (string-foldcase s)))
    (define (symbol-hash s) (%default-hash s))
    (define (number-hash n) (%default-hash n))
    (define (default-hash obj) (%default-hash obj))

    ; %default-hash's own modulus (see its definition below) -- every
    ; hash-bound-and-hash-salt pair curry ships is fixed and known ahead
    ; of time, so hash-bound is a plain constant rather than something
    ; computed from a comparator's own hash-function.
    (define (hash-bound) 1073741824)

    ; A fixed, non-secret salt value: %default-hash and every hash
    ; procedure above are pure functions of their input with no salting
    ; at all, so this exists only so a caller writing their OWN salted
    ; hash function has a stable value to fold in for consistency with
    ; the rest of a program -- curry's own built-in hash functions don't
    ; read it, which the spec permits (it requires hash-salt to exist and
    ; be stable, not that every hash function must incorporate it).
    (define (hash-salt) 0)

    (define boolean-comparator
      (%make-comparator boolean? (lambda (a b) (eq? a b))
                         (lambda (a b) (and (not a) b)) %default-hash))

    (define real-comparator (%make-comparator real? = < %default-hash))
    (define number-comparator real-comparator)

    (define char-comparator (%make-comparator char? char=? char<? %default-hash))
    (define char-ci-comparator (%make-comparator char? char-ci=? char-ci<? %default-hash))

    (define string-comparator (%make-comparator string? string=? string<? %default-hash))
    (define string-ci-comparator (%make-comparator string? string-ci=? string-ci<? %default-hash))

    (define symbol-comparator
      (%make-comparator symbol?
                        (lambda (a b) (eq? a b))
                        (lambda (a b) (string<? (symbol->string a) (symbol->string b)))
                        %default-hash))

    ;; ------------------------------------------------------------------
    ;; Compound comparators
    ;;
    ;; Simplified relative to the full SRFI-128 constructors (which also
    ;; take custom type-test/car/cdr/null? procedures so non-pair "list-like"
    ;; structures can be compared) — these three take a single element
    ;; comparator and work on genuine pairs/lists/vectors.
    ;; ------------------------------------------------------------------

    (define (pair-comparator car-cmp cdr-cmp)
      (%make-comparator
       pair?
       (lambda (a b) (and ((comparator-equality-predicate car-cmp) (car a) (car b))
                           ((comparator-equality-predicate cdr-cmp) (cdr a) (cdr b))))
       (lambda (a b)
         (let ((ca (car a)) (cb (car b)) (car=? (comparator-equality-predicate car-cmp)))
           (if (car=? ca cb)
               ((comparator-ordering-predicate cdr-cmp) (cdr a) (cdr b))
               ((comparator-ordering-predicate car-cmp) ca cb))))
       (lambda (p) (+ (* 31 (comparator-hash car-cmp (car p))) (comparator-hash cdr-cmp (cdr p))))))

    (define (list-comparator elt-cmp)
      (define elt=? (comparator-equality-predicate elt-cmp))
      (define elt<? (comparator-ordering-predicate elt-cmp))
      (define (list=? a b)
        (cond ((and (null? a) (null? b)) #t)
              ((or (null? a) (null? b)) #f)
              ((elt=? (car a) (car b)) (list=? (cdr a) (cdr b)))
              (else #f)))
      (define (list<? a b)
        (cond ((null? b) #f)
              ((null? a) #t)
              ((elt=? (car a) (car b)) (list<? (cdr a) (cdr b)))
              (else (elt<? (car a) (car b)))))
      (%make-comparator
       list? list=? list<?
       (lambda (lst) (fold-left (lambda (acc x) (+ (* 31 acc) (comparator-hash elt-cmp x))) 17 lst))))

    (define (vector-comparator elt-cmp)
      (%make-comparator
       vector?
       (lambda (a b)
         (and (= (vector-length a) (vector-length b))
              (let loop ((i 0))
                (or (= i (vector-length a))
                    (and ((comparator-equality-predicate elt-cmp) (vector-ref a i) (vector-ref b i))
                         (loop (+ i 1)))))))
       (lambda (a b)
         (let ((la (vector-length a)) (lb (vector-length b)))
           (if (not (= la lb))
               (< la lb)
               (let loop ((i 0))
                 (cond ((= i la) #f)
                       (((comparator-equality-predicate elt-cmp) (vector-ref a i) (vector-ref b i))
                        (loop (+ i 1)))
                       (else ((comparator-ordering-predicate elt-cmp) (vector-ref a i) (vector-ref b i))))))))
       (lambda (v)
         (let loop ((i 0) (acc 17))
           (if (= i (vector-length v)) acc
               (loop (+ i 1) (+ (* 31 acc) (comparator-hash elt-cmp (vector-ref v i)))))))))

    ;; ------------------------------------------------------------------
    ;; Default comparator registry
    ;;
    ;; A simplified stand-in for SRFI-128's extensible default comparator:
    ;; registered comparators are tried, in registration order, against
    ;; each operand's type-test predicate; the fixed built-ins below are
    ;; consulted first (in the total order the SRFI itself prescribes:
    ;; booleans < numbers < chars < strings < symbols < pairs < vectors).
    ;; ------------------------------------------------------------------

    (define %default-registry (list))

    (define (comparator-register-default! cmp)
      (set! %default-registry (append %default-registry (list cmp))))

    (define (%builtin-rank obj)
      (cond ((boolean? obj) 0) ((real? obj) 1) ((char? obj) 2)
            ((string? obj) 3) ((symbol? obj) 4) ((pair? obj) 5)
            ((vector? obj) 6) (else #f)))

    (define (%builtin-comparator-for obj)
      (case (%builtin-rank obj)
        ((0) boolean-comparator) ((1) real-comparator) ((2) char-comparator)
        ((3) string-comparator) ((4) symbol-comparator)
        ((5) (pair-comparator (make-default-comparator) (make-default-comparator)))
        ((6) (vector-comparator (make-default-comparator)))
        (else #f)))

    (define (%registered-comparator-for obj)
      (let loop ((regs %default-registry))
        (cond ((null? regs) #f)
              ((comparator-test-type (car regs) obj) (car regs))
              (else (loop (cdr regs))))))

    (define (%comparator-for obj)
      (or (%builtin-comparator-for obj) (%registered-comparator-for obj) equal-comparator))

    (define (make-default-comparator)
      (%make-comparator
       (lambda (x) #t)
       (lambda (a b) (equal? a b))
       (lambda (a b)
         (let ((ra (or (%builtin-rank a) 100)) (rb (or (%builtin-rank b) 100)))
           (if (not (= ra rb))
               (< ra rb)
               ((comparator-ordering-predicate (%comparator-for a)) a b))))
       %default-hash))

    (define default-comparator (make-default-comparator))))
