;;; R6RS compatibility tests for Curry Scheme

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; ---- (rnrs) import ----

(import (rnrs))

(check "rnrs: +" (+ 1 2) 3)
(check "rnrs: map" (map (lambda (x) (* x x)) '(1 2 3)) '(1 4 9))
(check "rnrs: filter" (filter odd? '(1 2 3 4 5)) '(1 3 5))

;;; ---- (rnrs base) ----

(import (rnrs base))
(check "rnrs base: append" (append '(1 2) '(3 4)) '(1 2 3 4))

;;; ---- (except (rnrs) ...) ----

(import (except (rnrs) remove))
(check "except-rnrs: +" (+ 10 20) 30)

;;; ---- R6RS library form ----

(library (test-lib-a)
  (export square cube)
  (import (rnrs))
  (define (square x) (* x x))
  (define (cube x)   (* x x x)))

(import (test-lib-a))
(check "library: square" (square 5) 25)
(check "library: cube"   (cube 3)   27)

;;; ---- library importing another library ----

(library (test-lib-b)
  (export square+cube)
  (import (rnrs) (test-lib-a))
  (define (square+cube x) (+ (square x) (cube x))))

(import (test-lib-b))
(check "library chain: square+cube 2" (square+cube 2) 12)

;;; ---- (only ...) and (except ...) import filters ----

(library (test-lib-c)
  (export foo bar baz)
  (import (rnrs))
  (define (foo) 'foo)
  (define (bar) 'bar)
  (define (baz) 'baz))

(import (only (test-lib-c) foo bar))
(check "only: foo" (foo) 'foo)
(check "only: bar" (bar) 'bar)

;;; ---- R6RS define-record-type with (fields ...) ----

(library (test-records)
  (export make-point point? point-x point-y point-x-set! point-y-set!
          make-color color? color-r color-g color-b)
  (import (rnrs))

  (define-record-type point
    (fields (mutable x)
            (mutable y)))

  (define-record-type color
    (fields (immutable r)
            (immutable g)
            (immutable b))))

(import (test-records))

(define p (make-point 3 4))
(check "r6rs record: constructor" (point? p) #t)
(check "r6rs record: accessor x"  (point-x p) 3)
(check "r6rs record: accessor y"  (point-y p) 4)
(point-x-set! p 10)
(check "r6rs record: mutator x"   (point-x p) 10)
(point-y-set! p 20)
(check "r6rs record: mutator y"   (point-y p) 20)
(check "r6rs record: predicate #f" (point? 'not-a-point) #f)

(define c (make-color 255 128 0))
(check "r6rs record immutable: r" (color-r c) 255)
(check "r6rs record immutable: g" (color-g c) 128)
(check "r6rs record immutable: b" (color-b c) 0)

;;; record-type-constructor/-predicate/-accessors/-mutators via the
;;; R6RS (fields ...) codegen path (record_type.c's is_r6rs branch),
;;; a different name-derivation shape from R7RS's explicit
;;; (ctor-name field...)/pred/(field acc [mut])... forms above.
(define point-rtd (record-rtd p))
(check "r6rs rtd: constructor field values"
  (let ((q ((record-type-constructor point-rtd) 7 8))) (list (point-x q) (point-y q)))
  '(7 8))
(check "r6rs rtd: predicate" ((record-type-predicate point-rtd) p) #t)
(check "r6rs rtd: both point fields are mutable"
  (map (lambda (m) (procedure? m)) (record-type-mutators point-rtd))
  '(#t #t))

(define color-rtd (record-rtd c))
(check "r6rs rtd: all-immutable record has no mutators"
  (record-type-mutators color-rtd) '(#f #f #f))
(check "r6rs rtd: accessors still work for an all-immutable record"
  (map (lambda (acc) (acc c)) (record-type-accessors color-rtd))
  '(255 128 0))

;;; ---- (rnrs) sub-library aliases ----

(import (rnrs lists))
(check "rnrs lists: filter" (filter even? '(1 2 3 4 5)) '(2 4))

(import (rnrs io simple))
; Just checking it loads without error — output primitives are in the global env
(check "rnrs io simple: display" (procedure? display) #t)

;;; ---- random-real (SRFI-27 primitives) ----

; random-real returns a float in [0,1)
(let ((r (random-real)))
  (check "random-real: >= 0" (>= r 0.0) #t)
  (check "random-real: < 1"  (<  r 1.0) #t))

; random-integer
(let ((n (random-integer 10)))
  (check "random-integer: >= 0" (>= n 0)  #t)
  (check "random-integer: < 10" (<  n 10) #t))

; default-random-source and randomize!
(check "default-random-source: bound" (symbol? default-random-source) #t)
(random-source-randomize! default-random-source)
(let ((r (random-real)))
  (check "random-real after seed: in range" (and (>= r 0.0) (< r 1.0)) #t))

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
