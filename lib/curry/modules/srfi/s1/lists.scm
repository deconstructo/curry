(define-library (srfi s1 lists)
  (import (scheme base))
  (export
    ; SRFI-1 procedures already in curry's global env
    cons car cdr caaar cadar caar cdar
    list list* make-list length append reverse
    list-tail list-ref last-pair list-copy
    map for-each filter fold-left

    ; Constructors
    xcons cons* list-tabulate circular-list

    ; Predicates
    proper-list? circular-list? dotted-list? null-list? not-pair? list=

    ; Selectors
    first second third fourth fifth sixth seventh eighth ninth tenth
    take drop take-right drop-right take! drop-right!
    split-at split-at! last

    ; Fold, unfold, map
    fold fold-right reduce reduce-right
    unfold unfold-right
    map! pair-for-each append-map append-map! filter-map flat-map

    ; Filtering / partitioning
    any every remove delete delete! partition count

    ; Searching
    find find-tail take-while drop-while span break list-index
    member assoc

    ; Deleting duplicates
    delete-duplicates delete-duplicates!

    ; Append / concatenate / reverse
    concatenate concatenate! append! append-reverse append-reverse! reverse!

    ; iota (existing)
    iota

    ; Zip / unzip
    zip unzip1 unzip2 unzip3 unzip4 unzip5

    ; Association lists
    alist-cons alist-copy del-assq del-assv del-assoc del-assq! del-assv! del-assoc!

    ; Lists as sets
    lset<= lset= lset-adjoin
    lset-union lset-intersection lset-difference lset-xor
    lset-union! lset-intersection! lset-difference! lset-xor!)
  (begin

    ;;; ---- iota (pre-existing) ----

    (define (iota count . rest)
      (let ((start (if (null? rest) 0 (car rest)))
            (step  (if (or (null? rest) (null? (cdr rest))) 1 (cadr rest))))
        (let loop ((i 0) (acc '()))
          (if (= i count)
              (reverse acc)
              (loop (+ i 1) (cons (+ start (* i step)) acc))))))

    ;;; ---- Constructors ----

    (define (xcons d a) (cons a d))
    (define (cons* . args)
      (if (null? (cdr args))
          (car args)
          (cons (car args) (apply cons* (cdr args)))))
    (define (list-tabulate n f)
      (let loop ((i (- n 1)) (acc '()))
        (if (< i 0) acc (loop (- i 1) (cons (f i) acc)))))
    ;; Genuinely circular -- callers must not (write)/(display)/traverse
    ;; this with anything that doesn't detect cycles (curry's write/display
    ;; have no cycle detection at all -- see (srfi 279)'s own documented
    ;; limitation on this exact point).
    (define (circular-list . args)
      (if (null? args)
          (error "circular-list: at least one element required")
          (let ((c (apply list args)))
            (let loop ((p c)) (if (null? (cdr p)) (set-cdr! p c) (loop (cdr p))))
            c)))

    ;;; ---- Predicates ----

    (define (proper-list? x)
      (cond ((null? x) #t)
            ((pair? x) (list? x))   ; list? is R7RS-mandated to be cycle-safe
            (else #f)))

    (define (circular-list? x)
      (and (pair? x)
           (let loop ((slow x) (fast (cdr x)))
             (cond ((not (pair? fast)) #f)
                   ((eq? slow fast) #t)
                   ((not (pair? (cdr fast))) #f)
                   (else (loop (cdr slow) (cddr fast)))))))

    ;; A list is exactly one of {proper, circular, dotted} (or the atom
    ;; '() itself, which is proper but not a pair). Reusing proper-list?
    ;; (delegates to R7RS's cycle-safe list?) and circular-list? (its
    ;; own tortoise/hare walk) instead of a third independent manual
    ;; walk here is deliberate: an earlier version of this predicate did
    ;; its own naive (cdr p) walk with no cycle detection at all, hanging
    ;; forever on a circular list -- found live by independent security
    ;; review (dotted-list? on (circular-list 1 2 3) never returned).
    (define (dotted-list? x)
      (and (not (null? x))
           (not (proper-list? x))
           (not (circular-list? x))))

    (define (null-list? x) (null? x))
    (define (not-pair? x) (not (pair? x)))

    (define (list= elt=? . lists)
      (or (null? lists) (null? (cdr lists))
          (let loop ((a (car lists)) (rest (cdr lists)))
            (or (null? rest)
                (let ((b (car rest)))
                  (and (= (length a) (length b))
                       (let eq-loop ((pa a) (pb b))
                         (or (null? pa)
                             (and (elt=? (car pa) (car pb))
                                  (eq-loop (cdr pa) (cdr pb)))))
                       (loop b (cdr rest))))))))

    ;;; ---- Selectors ----

    (define first  car)
    (define second cadr)
    (define third  caddr)
    (define (fourth  lst) (car (cdr (cdr (cdr lst)))))
    (define (fifth   lst) (car (cdr (cdr (cdr (cdr lst))))))
    (define (sixth   lst) (car (cdr (cdr (cdr (cdr (cdr lst)))))))
    (define (seventh lst) (car (cdr (cdr (cdr (cdr (cdr (cdr lst))))))))
    (define (eighth  lst) (car (cdr (cdr (cdr (cdr (cdr (cdr (cdr lst)))))))))
    (define (ninth   lst) (car (cdr (cdr (cdr (cdr (cdr (cdr (cdr (cdr lst))))))))))
    (define (tenth   lst) (car (cdr (cdr (cdr (cdr (cdr (cdr (cdr (cdr (cdr lst)))))))))))

    (define (take lst n)
      (if (or (= n 0) (null? lst))
          '()
          (cons (car lst) (take (cdr lst) (- n 1)))))

    (define (drop lst n)
      (if (or (= n 0) (null? lst))
          lst
          (drop (cdr lst) (- n 1))))

    (define (take-right lst n)
      (let loop ((lead (drop lst n)) (lag lst))
        (if (pair? lead) (loop (cdr lead) (cdr lag)) lag)))

    (define (drop-right lst n)
      (take lst (- (length lst) n)))

    ;; SRFI-1 permits (but doesn't require) the !-suffixed variants to
    ;; imperatively splice their argument -- curry's implements them as
    ;; plain aliases to the non-destructive version throughout this file,
    ;; which is spec-conformant and avoids introducing shared-structure
    ;; mutation hazards for no real benefit under a GC'd, immutable-by-
    ;; convention list-processing style.
    (define (take! lst n) (take lst n))
    (define (drop-right! lst n) (drop-right lst n))

    (define (split-at lst n) (values (take lst n) (drop lst n)))
    (define (split-at! lst n) (split-at lst n))

    (define (last lst) (car (last-pair lst)))

    (define (last-pair lst)
      (if (null? (cdr lst)) lst (last-pair (cdr lst))))

    ;;; ---- Fold / unfold / map ----

    ;; SRFI-1's fold calls kons with the CURRENT ELEMENT(S) FIRST and the
    ;; accumulator LAST -- (kons e1 e2 ... acc) -- traversing left to
    ;; right. This is NOT the same argument order as fold-left (accumulator
    ;; first), and getting this backwards silently gives wrong answers for
    ;; any non-commutative kons: (fold cons '() '(1 2 3)) must be (3 2 1),
    ;; not the fold-left-shaped (((() . 1) . 2) . 3).
    (define (fold kons knil lst1 . rest)
      (if (null? rest)
          (let loop ((l lst1) (acc knil))
            (if (null? l) acc (loop (cdr l) (kons (car l) acc))))
          (let loop ((ls (cons lst1 rest)) (acc knil))
            (if (any null? ls)
                acc
                (loop (map cdr ls) (apply kons (append (map car ls) (list acc))))))))

    ;; fold-right already matches this convention correctly in curry's
    ;; global env (verified: (fold-right cons '() '(1 2 3)) => (1 2 3)),
    ;; so it's just re-exported above, not redefined here.

    ;; (reduce f ridentity list) = ridentity if list is empty, else
    ;; (fold f (car list) (cdr list)) -- uses the first element as the
    ;; seed rather than a caller-supplied identity, so f is never called
    ;; with ridentity at all unless the list is empty.
    (define (reduce f ridentity lst)
      (if (null? lst) ridentity (fold f (car lst) (cdr lst))))

    (define (reduce-right f ridentity lst)
      (if (null? lst)
          ridentity
          (let ((rev (reverse lst)))
            (fold f (car rev) (cdr rev)))))

    ;; (unfold p f g seed [tail-gen]) builds a list left-to-right: stops
    ;; when (p seed) is true, otherwise conses (f seed) and recurses on
    ;; (g seed). tail-gen (default: seed is dropped, proper list) computes
    ;; the final cdr from the seed that satisfied p.
    (define (unfold p f g seed . tail-gen)
      (let ((tg (if (null? tail-gen) (lambda (x) '()) (car tail-gen))))
        (let loop ((seed seed))
          (if (p seed)
              (tg seed)
              (cons (f seed) (loop (g seed)))))))

    (define (unfold-right p f g seed . tail)
      (let loop ((seed seed) (acc (if (null? tail) '() (car tail))))
        (if (p seed) acc (loop (g seed) (cons (f seed) acc)))))

    (define (map! f lst1 . rest) (apply map f lst1 rest))

    (define (pair-for-each f lst)
      (let loop ((p lst)) (when (pair? p) (f p) (loop (cdr p)))))

    (define (append-map f lst . rest)
      (apply append (apply map f lst rest)))

    (define (append-map! f lst . rest) (apply append-map f lst rest))

    (define (filter-map f lst . rest)
      (if (null? rest)
          (let loop ((l lst) (acc '()))
            (if (null? l)
                (reverse acc)
                (let ((r (f (car l))))
                  (loop (cdr l) (if r (cons r acc) acc)))))
          (let loop ((ls (cons lst rest)) (acc '()))
            (if (any null? ls)
                (reverse acc)
                (let ((r (apply f (map car ls))))
                  (loop (map cdr ls) (if r (cons r acc) acc)))))))

    (define (flat-map f lst) (append-map f lst))

    ;;; ---- Filtering / partitioning / any / every ----

    (define (any pred lst . rest)
      (if (null? rest)
          (cond ((null? lst) #f)
                ((pred (car lst)) => (lambda (r) r))
                (else (any pred (cdr lst))))
          (let loop ((ls (cons lst rest)))
            (cond ((any null? ls) #f)
                  ((apply pred (map car ls)) => (lambda (r) r))
                  (else (loop (map cdr ls)))))))

    (define (every pred lst . rest)
      (if (null? rest)
          (cond ((null? lst) #t)
                ((null? (cdr lst)) (pred (car lst)))
                ((pred (car lst)) (every pred (cdr lst)))
                (else #f))
          (let loop ((ls (cons lst rest)) (last #t))
            (if (any null? ls)
                last
                (let ((r (apply pred (map car ls))))
                  (and r (loop (map cdr ls) r)))))))

    (define (remove pred lst) (filter (lambda (x) (not (pred x))) lst))

    (define (delete x lst . rest)
      (let ((=? (if (null? rest) equal? (car rest))))
        (filter (lambda (y) (not (=? x y))) lst)))

    (define (delete! x lst . rest) (apply delete x lst rest))

    (define (take-while pred lst)
      (cond ((null? lst) '())
            ((pred (car lst)) (cons (car lst) (take-while pred (cdr lst))))
            (else '())))

    (define (drop-while pred lst)
      (cond ((null? lst) '())
            ((pred (car lst)) (drop-while pred (cdr lst)))
            (else lst)))

    (define (count pred lst . rest)
      (if (null? rest)
          (fold (lambda (x acc) (if (pred x) (+ acc 1) acc)) 0 lst)
          (let loop ((ls (cons lst rest)) (acc 0))
            (if (any null? ls)
                acc
                (loop (map cdr ls) (if (apply pred (map car ls)) (+ acc 1) acc))))))

    (define (partition pred lst)
      (let loop ((l lst) (yes '()) (no '()))
        (cond ((null? l) (values (reverse yes) (reverse no)))
              ((pred (car l)) (loop (cdr l) (cons (car l) yes) no))
              (else           (loop (cdr l) yes (cons (car l) no))))))

    ;;; ---- Searching ----

    (define (find pred lst)
      (cond ((null? lst) #f)
            ((pred (car lst)) (car lst))
            (else (find pred (cdr lst)))))

    (define (find-tail pred lst)
      (cond ((null? lst) #f)
            ((pred (car lst)) lst)
            (else (find-tail pred (cdr lst)))))

    (define (span pred lst) (values (take-while pred lst) (drop-while pred lst)))
    (define (break pred lst) (span (lambda (x) (not (pred x))) lst))

    (define (list-index pred lst . rest)
      (if (null? rest)
          (let loop ((l lst) (i 0))
            (cond ((null? l) #f) ((pred (car l)) i) (else (loop (cdr l) (+ i 1)))))
          (let loop ((ls (cons lst rest)) (i 0))
            (cond ((any null? ls) #f)
                  ((apply pred (map car ls)) i)
                  (else (loop (map cdr ls) (+ i 1)))))))

    ;; member/assoc with an optional 3rd-argument equality predicate --
    ;; curry's own global member/assoc (builtins.c) don't accept one at
    ;; all (raise "too many arguments" on a 3rd arg), so both R7RS's own
    ;; optional-comparator form and SRFI-1's identical extension are
    ;; unavailable without going through this library.
    (define (member x lst . rest)
      (let ((=? (if (null? rest) equal? (car rest))))
        (let loop ((l lst))
          (cond ((null? l) #f)
                ((=? x (car l)) l)
                (else (loop (cdr l)))))))

    (define (assoc x lst . rest)
      (let ((=? (if (null? rest) equal? (car rest))))
        (let loop ((l lst))
          (cond ((null? l) #f)
                ((=? x (caar l)) (car l))
                (else (loop (cdr l)))))))

    ;;; ---- Deleting duplicates ----

    (define (delete-duplicates lst . rest)
      (let ((=? (if (null? rest) equal? (car rest))))
        (let loop ((l lst) (acc '()))
          (cond ((null? l) (reverse acc))
                ((any (lambda (y) (=? (car l) y)) acc) (loop (cdr l) acc))
                (else (loop (cdr l) (cons (car l) acc)))))))

    (define (delete-duplicates! lst . rest) (apply delete-duplicates lst rest))

    ;;; ---- Append / concatenate / reverse ----

    (define (concatenate lists) (apply append lists))
    (define (concatenate! lists) (concatenate lists))
    (define (append! . lists) (apply append lists))
    (define (reverse! lst) (reverse lst))

    (define (append-reverse rev-head tail)
      (fold cons tail rev-head))
    (define (append-reverse! rev-head tail) (append-reverse rev-head tail))

    ;;; ---- Zip / unzip ----

    (define (zip . lists) (apply map list lists))
    (define (unzip1 lst) (map car lst))
    (define (unzip2 lst) (values (map car lst) (map cadr lst)))
    (define (unzip3 lst) (values (map car lst) (map cadr lst) (map caddr lst)))
    (define (unzip4 lst)
      (values (map first lst) (map second lst) (map third lst) (map fourth lst)))
    (define (unzip5 lst)
      (values (map first lst) (map second lst) (map third lst)
              (map fourth lst) (map fifth lst)))

    ;;; ---- Association lists ----

    (define (alist-cons key val alist) (cons (cons key val) alist))
    (define (alist-copy alist) (map (lambda (p) (cons (car p) (cdr p))) alist))

    (define (del-assq key alist) (remove (lambda (p) (eq?    (car p) key)) alist))
    (define (del-assv key alist) (remove (lambda (p) (eqv?   (car p) key)) alist))
    (define (del-assoc key alist) (remove (lambda (p) (equal? (car p) key)) alist))
    (define (del-assq! key alist) (del-assq key alist))
    (define (del-assv! key alist) (del-assv key alist))
    (define (del-assoc! key alist) (del-assoc key alist))

    ;;; ---- Lists as sets ----
    ;;; Every procedure here takes the element-equality predicate as its
    ;;; own first argument, per SRFI-1 (there is no fixed default -- unlike
    ;;; member/assoc/delete/delete-duplicates above, all of which default
    ;;; to equal? when no predicate is supplied).

    (define (lset-adjoin =? lst . elts)
      (fold (lambda (e acc) (if (any (lambda (x) (=? x e)) acc) acc (cons e acc)))
            lst elts))

    (define (lset<= =? . lists)
      (or (null? lists) (null? (cdr lists))
          (let loop ((a (car lists)) (rest (cdr lists)))
            (or (null? rest)
                (and (every (lambda (x) (any (lambda (y) (=? x y)) (car rest))) a)
                     (loop (car rest) (cdr rest)))))))

    (define (lset= =? . lists)
      (or (null? lists) (null? (cdr lists))
          (every (lambda (l) (and (apply lset<= =? (list l (car lists)))
                                   (apply lset<= =? (list (car lists) l))))
                 (cdr lists))))

    (define (lset-union =? . lists)
      (fold (lambda (lst acc) (apply lset-adjoin =? acc lst)) '() lists))

    (define (lset-intersection =? lst1 . lists)
      (filter (lambda (x) (every (lambda (l) (any (lambda (y) (=? x y)) l)) lists)) lst1))

    (define (lset-difference =? lst1 . lists)
      (filter (lambda (x) (not (any (lambda (l) (any (lambda (y) (=? x y)) l)) lists))) lst1))

    (define (lset-xor =? . lists)
      (fold (lambda (lst acc)
              (append (lset-difference =? acc lst) (lset-difference =? lst acc)))
            '() lists))

    (define (lset-union! =? . lists) (apply lset-union =? lists))
    (define (lset-intersection! =? lst1 . lists) (apply lset-intersection =? lst1 lists))
    (define (lset-difference! =? lst1 . lists) (apply lset-difference =? lst1 lists))
    (define (lset-xor! =? . lists) (apply lset-xor =? lists))))
