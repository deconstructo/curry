;;; srfi_s209_enums_tests.scm — (srfi s209 enums)

(import (srfi s209 enums))
(import (srfi s128 comparators))

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

;;; ---- make-enum-type / basic accessors ----

(define color (make-enum-type '(red orange yellow green cyan blue violet)))
(define color-red (enum-name->enum color 'red))
(define color-orange (enum-name->enum color 'orange))
(define color-blue (enum-name->enum color 'blue))
(define color-violet (enum-name->enum color 'violet))

(check "enum-type?" (enum-type? color) #t)
(check "enum?" (enum? color-red) #t)
(check "enum-type-contains?: true" (enum-type-contains? color color-red) #t)
(check "enum-type-contains?: non-enum" (enum-type-contains? color 5) #f)

(check "enum-name" (enum-name color-red) 'red)
(check "enum-ordinal: first" (enum-ordinal color-red) 0)
(check "enum-value: defaults to ordinal" (enum-value color-blue) 5)
(check "enum-type accessor" (eq? (enum-type color-red) color) #t)

;;; ---- comparisons ----

(check "enum=?: same enum" (enum=? color-red (enum-name->enum color 'red)) #t)
(check "enum=?: different enums" (enum=? color-red color-blue) #f)
(check "enum<?: by ordinal" (enum<? color-red color-orange color-blue) #t)
(check "enum<=?: equal counts" (enum<=? color-red color-red) #t)
(check "enum>?: descending" (enum>? color-blue color-orange color-red) #t)
(check "enum-comparison: different types raises"
       (guard (e (#t 'raised))
         (let ((other (make-enum-type '(x y))))
           (enum=? color-red (enum-name->enum other 'x))))
       'raised)

;;; ---- finders ----

(check "enum-name->ordinal" (enum-name->ordinal color 'blue) 5)
(check "enum-name->value" (enum-name->value color 'green) 3)
(check "enum-ordinal->name" (enum-ordinal->name color 0) 'red)
(check "enum-ordinal->value" (enum-ordinal->value color 6) 6)
(check "enum-name->enum: missing returns #f" (enum-name->enum color 'nope) #f)
(check "enum-ordinal->enum: out of range returns #f" (enum-ordinal->enum color 99) #f)
(check "enum-name->ordinal: missing raises"
       (guard (e (#t 'raised)) (enum-name->ordinal color 'nope))
       'raised)

;;; ---- enum-type queries ----

(check "enum-type-size" (enum-type-size color) 7)
(check "enum-min" (enum-name (enum-min color)) 'red)
(check "enum-max" (enum-name (enum-max color)) 'violet)
(check "enum-type-names" (enum-type-names color) '(red orange yellow green cyan blue violet))
(check "enum-type-values: pizza-style explicit values"
       (enum-type-values (make-enum-type '((margherita "tomato") (funghi "mushrooms"))))
       (list "tomato" "mushrooms"))

;;; ---- navigation ----

(check "enum-next" (enum-name (enum-next color-red)) 'orange)
(check "enum-next: at max is #f" (enum-next (enum-max color)) #f)
(check "enum-prev" (enum-name (enum-prev color-orange)) 'red)
(check "enum-prev: at min is #f" (enum-prev color-red) #f)

;;; ---- comparator ----

(let ((cmp (make-enum-comparator color)))
  (check "make-enum-comparator: ordering" (<? cmp color-red color-blue) #t)
  (check "make-enum-comparator: equality" (=? cmp color-red color-red) #t)
  (check "make-enum-comparator: type test" (comparator-test-type cmp color-red) #t)
  (check "make-enum-comparator: hashable" (comparator-hashable? cmp) #t))

;;; ---- enum-set construction ----

(define color-set (enum-type->enum-set color))
(define empty-color-set (enum-empty-set color))
(define reddish (enum-set color color-red color-orange))

(check "enum-type->enum-set: full size" (enum-set-size color-set) 7)
(check "enum-empty-set: size 0" (enum-set-size empty-color-set) 0)
(check "enum-set: members" (enum-set-map->list enum-name reddish) '(red orange))
(check "list->enum-set" (enum-set-map->list enum-name (list->enum-set color (list color-red color-orange))) '(red orange))
(check "enum-set-copy: independent"
       (let* ((orig (enum-set color color-red)) (copy (enum-set-copy orig)))
         (enum-set-adjoin! copy color-blue)
         (list (enum-set-size orig) (enum-set-size copy)))
       (list 1 2))
(check "enum-set: wrong-type enum raises"
       (guard (e (#t 'raised))
         (let ((other (make-enum-type '(x y))))
           (enum-set color (enum-name->enum other 'x))))
       'raised)

;;; ---- enum-set-projection ----

(define us-traffic-light (make-enum-type '(red yellow green)))

(check "enum-set-projection: matching names carry over"
       (enum-set-map->list enum-name
         (enum-set-projection us-traffic-light (enum-set color color-red (enum-name->enum color 'green))))
       '(red green))
(check "enum-set-projection: accepts an enum-set for the target too"
       (enum-set-map->list enum-name
         (enum-set-projection (enum-type->enum-set us-traffic-light) (enum-set color color-red)))
       '(red))
(check "enum-set-projection: name absent in target raises"
       (guard (e (#t 'raised)) (enum-set-projection us-traffic-light (enum-set color color-violet)))
       'raised)

;;; ---- R6RS compatibility ----

(define abc-set (make-enumeration '(a b c)))

(check "make-enumeration: full set" (enum-set->list abc-set) '(a b c))
(check "enum-set-universe" (enum-set->list (enum-set-universe (enum-set-delete abc-set (enum-name->enum (enum-set-type abc-set) 'a)))) '(a b c))
(check "enum-set-constructor" (enum-set->list ((enum-set-constructor abc-set) '(c a))) '(a c))
(check "enum-set-member?: present" (enum-set-member? 'b abc-set) #t)
(check "enum-set-member?: absent name in set but present in type" (enum-set-member? 'a (enum-set-delete abc-set (enum-name->enum (enum-set-type abc-set) 'a))) #f)
(check "enum-set-member?: unknown name raises"
       (guard (e (#t 'raised)) (enum-set-member? 'z abc-set))
       'raised)
(check "enum-set-indexer: known name" ((enum-set-indexer abc-set) 'b) 1)
(check "enum-set-indexer: unknown name" ((enum-set-indexer abc-set) 'z) #f)

;;; ---- enum-set predicates ----

(define ~reddish (enum-set-complement reddish))

(check "enum-set-contains?" (enum-set-contains? reddish color-red) #t)
(check "enum-set-contains?: absent" (enum-set-contains? reddish color-blue) #f)
(check "enum-set-empty?: true" (enum-set-empty? empty-color-set) #t)
(check "enum-set-empty?: false" (enum-set-empty? reddish) #f)
(check "enum-set-disjoint?: true" (enum-set-disjoint? reddish ~reddish) #t)
(check "enum-set-disjoint?: false" (enum-set-disjoint? reddish reddish) #f)
(check "enum-set=?" (enum-set=? reddish (enum-set color color-orange color-red)) #t)
(check "enum-set<?: proper subset" (enum-set<? reddish color-set) #t)
(check "enum-set<?: equal is not proper" (enum-set<? reddish reddish) #f)
(check "enum-set<=?: equal counts" (enum-set<=? reddish reddish) #t)
(check "enum-set>?: proper superset" (enum-set>? color-set reddish) #t)
(check "enum-set>=?: equal counts" (enum-set>=? reddish reddish) #t)
(check "enum-set-subset?: same type" (enum-set-subset? reddish color-set) #t)
(check "enum-set-subset?: across different types, by name"
       (enum-set-subset? (enum-set us-traffic-light (enum-name->enum us-traffic-light 'red))
                          (enum-set color color-red))
       #t)
(check "enum-set-any?" (enum-set-any? (lambda (e) (eq? (enum-name e) 'blue)) color-set) #t)
(check "enum-set-any?: none match" (enum-set-any? (lambda (e) (eq? (enum-name e) 'nope)) color-set) #f)
(check "enum-set-every?: all satisfy" (enum-set-every? (lambda (e) (>= (enum-ordinal e) 0)) color-set) #t)
(check "enum-set-every?: not all satisfy" (enum-set-every? (lambda (e) (eq? (enum-name e) 'red)) color-set) #f)

;;; ---- accessors ----

(check "enum-set-type" (eq? (enum-set-type reddish) color) #t)

;;; ---- mutation: functional vs linear-update ----

(check "enum-set-adjoin: functional, leaves original untouched"
       (let ((orig (enum-set color color-red)))
         (enum-set-adjoin orig color-blue)
         (enum-set-size orig))
       1)
(check "enum-set-adjoin!: mutates in place"
       (let ((s (enum-set-copy (enum-set color color-red))))
         (enum-set-adjoin! s color-blue)
         (enum-set-map->list enum-name s))
       '(red blue))
(check "enum-set-delete: functional"
       (let ((orig (enum-set color color-red color-blue)))
         (enum-set-delete orig color-blue)
         (enum-set-size orig))
       2)
(check "enum-set-delete!: mutates in place"
       (let ((s (enum-set-copy (enum-set color color-red color-blue))))
         (enum-set-delete! s color-blue)
         (enum-set-map->list enum-name s))
       '(red))
(check "enum-set-delete-all"
       (enum-set-map->list enum-name (enum-set-delete-all color-set (list color-red color-blue)))
       '(orange yellow green cyan violet))
(check "enum-set-delete-all!"
       (let ((s (enum-set-copy color-set)))
         (enum-set-delete-all! s (list color-red color-blue))
         (enum-set-map->list enum-name s))
       '(orange yellow green cyan violet))

;;; ---- whole-set operations ----

(check "enum-set-size" (enum-set-size (enum-set color color-red color-blue)) 2)
(check "enum-set->enum-list: ascending ordinal order"
       (map enum-name (enum-set->enum-list (enum-set color color-blue color-red)))
       '(red blue))
(check "enum-set-count" (enum-set-count (lambda (e) (> (enum-ordinal e) 3)) color-set) 3)
(check "enum-set-filter" (enum-set-map->list enum-name (enum-set-filter (lambda (e) (> (enum-ordinal e) 3)) color-set)) '(cyan blue violet))
(check "enum-set-remove" (enum-set-map->list enum-name (enum-set-remove (lambda (e) (> (enum-ordinal e) 3)) color-set)) '(red orange yellow green))
(check "enum-set-map->list" (enum-set-map->list enum-name reddish) '(red orange))
(check "enum-set-for-each: side effects, ascending order"
       (let ((acc '()))
         (enum-set-for-each (lambda (e) (set! acc (cons (enum-name e) acc))) reddish)
         (reverse acc))
       '(red orange))
(check "enum-set-fold: equivalent to reverse of enum-set->enum-list"
       (map enum-name (enum-set-fold cons '() reddish))
       (map enum-name (reverse (enum-set->enum-list reddish))))

;;; ---- logical operations ----

(check "enum-set-complement" (enum-set-map->list enum-name (enum-set-complement empty-color-set)) (enum-type-names color))
(check "enum-set-complement!: mutates in place"
       (let ((s (enum-set-copy empty-color-set)))
         (enum-set-complement! s)
         (enum-set-size s))
       7)
(check "enum-set-union" (enum-set=? color-set (enum-set-union reddish ~reddish)) #t)
(check "enum-set-union!: mutates and returns first arg"
       (let ((s (enum-set-copy (enum-set color color-orange))))
         (eq? s (enum-set-union! s (enum-set color color-blue))))
       #t)
(check "enum-set-intersection" (enum-set-empty? (enum-set-intersection reddish ~reddish)) #t)
(check "enum-set-intersection!" (enum-set-empty? (enum-set-intersection! (enum-set-copy reddish) (enum-set-copy ~reddish))) #t)
(check "enum-set-difference" (enum-set=? ~reddish (enum-set-difference color-set reddish)) #t)
(check "enum-set-difference!" (enum-set=? ~reddish (enum-set-difference! (enum-set-copy color-set) reddish)) #t)
(check "enum-set-xor: disjoint sets is their union" (enum-set=? color-set (enum-set-xor reddish ~reddish)) #t)
(check "enum-set-xor: a set with itself is empty" (enum-set-empty? (enum-set-xor! (enum-set-copy reddish) (enum-set-copy reddish))) #t)
(check "enum-set-union/-intersection/-difference: different types raise"
       (guard (e (#t 'raised))
         (enum-set-union reddish (enum-type->enum-set (make-enum-type '(x y)))))
       'raised)

;;; ---- define-enum / define-enumeration syntax ----

(define-enum size (small medium large) size-set)

(check "define-enum: type-name macro returns the enum" (enum-name (size medium)) 'medium)
(check "define-enum: unknown name raises" (guard (e (#t 'raised)) (size nonexistent)) 'raised)
(check "define-enum: constructor-syntax builds an enum-set" (enum-set-map->list enum-name (size-set small large)) '(small large))
(check "define-enum: zero-argument constructor is empty" (enum-set-empty? (size-set)) #t)

(define-enumeration suit (clubs diamonds hearts spades) suit-set)

(check "define-enumeration: type-name macro returns the SYMBOL, not the enum" (suit hearts) 'hearts)
(check "define-enumeration: value defaults to name, not ordinal"
       (enum-value (enum-name->enum (enum-set-type (suit-set clubs)) 'hearts))
       'hearts)
(check "define-enumeration: constructor-syntax still builds an enum-set of enums"
       (enum-set-map->list enum-name (suit-set hearts spades))
       '(hearts spades))

; Two independently defined types in the same scope must not collide --
; regression check for the hidden-registry design (curry's define-syntax
; hygiene does not rename introduced top-level identifiers per expansion,
; so a naive shared temp-variable implementation would have both types
; silently aliasing to whichever was defined last).
(check "define-enum: two types in the same scope stay independent"
       (list (enum-name (size small)) (suit clubs))
       (list 'small 'clubs))

;;; ---- Summary ----

(newline)
(display "srfi-s209 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
