;;; oop_tests.scm — (curry oop) module: Slim CLOS Layer 1 (classes, generic
;;; functions, multiple dispatch), per docs/thoughts/oop.md.

(import (scheme base))
(import (curry oop))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (check-true label got)
  (check label (if got #t #f) #t))

;;; ---- Single-inheritance dispatch ----

(define (square x) (* x x))

(define-class <point> ()
  (x #:init 0 #:accessor point-x)
  (y #:init 0 #:accessor point-y))

(define p1 (make <point> #:x 0 #:y 0))
(define p2 (make <point> #:x 3 #:y 4))

(check "accessor read" (point-x p2) 3)

(define-generic distance (a b))
(define-method distance ((a <point>) (b <point>))
  (sqrt (+ (square (- (point-x b) (point-x a)))
           (square (- (point-y b) (point-y a))))))

(check "single-inheritance dispatch" (distance p1 p2) 5)

;;; ---- Multiple inheritance + C3 linearization ----
;;; Classic textbook example (Python/Dylan/Raku docs, and object-system.md's
;;; own worked example): D(B, C), B(A), C(A) -> MRO (D B C A object).

(define-class <a> ())
(define-class <b> (<a>))
(define-class <c> (<a>))
(define-class <d> (<b> <c>))

(check "C3 linearization MRO"
       (map class-name (class-precedence-list <d>))
       '(<d> <b> <c> <a> <object>))

;;; ---- call-next-method ----

(define-generic describe (x))
(define-method describe ((x <object>)) "object")
(define-method describe ((x <a>)) (string-append "a:" (call-next-method)))
(define-method describe ((x <d>)) (string-append "d:" (call-next-method)))

(check "call-next-method chains through the full MRO"
       (describe (make <d>))
       "d:a:object")

;;; ---- Dispatch on built-in types ----

(define-generic my-kind (x))
(define-method my-kind ((x <number>)) 'number)
(define-method my-kind ((x <string>)) 'string)

(check "dispatch on built-in <number>" (my-kind 5) 'number)
(check "dispatch on built-in <string>" (my-kind "hi") 'string)
(check "class-of exact integer"  (class-name (class-of 5))    '<integer>)
(check "class-of inexact real"   (class-name (class-of 3.14)) '<inexact-real>)
(check "class-of symbol"         (class-name (class-of 'sym)) '<symbol>)
(check "class-of string"         (class-name (class-of "s"))  '<string>)

;;; ---- Extension of a built-in generic (+) for a user-defined type ----
;;; The doc's own explicit acceptance test: `+` must still work normally on
;;; numbers after being extended for a user-defined type.

(define-class <poly> ()
  (coeffs #:init '() #:accessor poly-coeffs))

(define-method + ((a <poly>) (b <poly>))
  (make <poly> #:coeffs (map + (poly-coeffs a) (poly-coeffs b))))

(define poly-sum (+ (make <poly> #:coeffs '(1 2 3))
                     (make <poly> #:coeffs '(10 20 30))))

(check "extended + on user-defined type" (poly-coeffs poly-sum) '(11 22 33))
(check "extended + still works on plain numbers" (+ 1 2 3) 6)

;;; ---- Immutable slot enforcement ----

(define-class <imm> () (v #:init 42))
(define iv (make <imm>))
(check "immutable slot readable" (slot-ref iv 'v) 42)
(check-true "slot-set! on immutable slot raises"
            (guard (e (#t #t)) (slot-set! iv 'v 100) #f))

(define-class <mut> () (v #:init 0 #:mutable))
(define mv (make <mut>))
(slot-set! mv 'v 99)
(check "mutable slot after slot-set!" (slot-ref mv 'v) 99)

;;; ---- Additional coverage: error paths, multi-argument specificity ----

(define-generic foo-only-string (x))
(define-method foo-only-string ((x <string>)) 'matched)
(check-true "no applicable method raises"
            (guard (e (#t #t)) (foo-only-string 5) #f))

(check-true "slot-set! on nonexistent slot raises"
            (guard (e (#t #t)) (slot-set! mv 'nope 1) #f))

(define-class <animal> ())
(define-class <dog> (<animal>))

(define-generic interact (a b))
(define-method interact ((a <animal>) (b <animal>)) 'generic)
(define-method interact ((a <dog>) (b <animal>)) 'dog-specific)

(check "multi-arg dispatch: more specific pair wins"
       (interact (make <dog>) (make <animal>))
       'dog-specific)
(check "multi-arg dispatch: falls back to less specific pair"
       (interact (make <animal>) (make <animal>))
       'generic)

;;; ---- is-a? / subclass? / class-slots ----

(check "is-a? respects inheritance" (is-a? (make <dog>) <animal>) #t)
(check "is-a? false for unrelated class" (is-a? (make <animal>) <dog>) #f)
(check "subclass? respects inheritance" (subclass? <dog> <animal>) #t)
(check "subclass? false for unrelated class" (subclass? <animal> <dog>) #f)
(check "class-slots lists slot names" (class-slots <imm>) '(v))

;;; ---- Slot inheritance across superclasses ----

(define-class <point-3d> (<point>)
  (z #:init 0 #:accessor point-z))

(define p3 (make <point-3d> #:x 1 #:y 2 #:z 3))
(check "inherited slot accessor" (point-x p3) 1)
(check "own slot accessor" (point-z p3) 3)
(check "class-slots includes inherited slots"
       (class-slots <point-3d>) '(z x y))

;;; ---- Regression coverage for review-caught bugs ----

;; Redefining a method must replace it, not append a duplicate — otherwise
;; the ordinary REPL/script-reload workflow leaves the OLD definition as
;; the one dispatch actually picks.
(define-generic redef-test (x))
(define-method redef-test ((x <number>)) 'first)
(define-method redef-test ((x <number>)) 'second)
(check "redefining a method replaces the old one" (redef-test 5) 'second)

;; #:init must be re-evaluated per instance, not shared eq? across every
;; instance of the class (a mutable default like a hash table is the case
;; that actually matters; the empty list is a poor test since '() is a
;; canonical shared value in Scheme regardless of this).
(define-class <boxh> () (h #:init (make-hash-table) #:mutable))
(define bh1 (make <boxh>))
(define bh2 (make <boxh>))
(hash-table-set! (slot-ref bh1 'h) 'k 'v1)
(check "#:init defaults are fresh per instance, not eq?-shared"
       (hash-table-ref (slot-ref bh2 'h) 'k 'missing)
       'missing)

;; Storing the internal unbound-slot sentinel directly must be rejected —
;; otherwise slot-ref would wrongly report the slot as still unbound.
(define-class <sentinel-test> () (v #:init 0 #:mutable))
(define st (make <sentinel-test>))
(check-true "slot-set! rejects the raw unbound-slot sentinel"
            (guard (e (#t #t)) (slot-set! st 'v %unbound-slot) #f))

;; define-method on a name already bound to a non-procedure must raise
;; instead of silently clobbering it with a generic function.
(define already-a-number 42)
(check-true "define-method on a non-procedure binding raises"
            (guard (e (#t #t))
              (%ensure-generic! 'already-a-number already-a-number)
              #f))

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
