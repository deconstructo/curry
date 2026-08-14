;;; Tests for (srfi s279 inspect) / (srfi 279) — curry-native SRFI-279
;;; (In(tro)spection Protocol) implementation, plus the record-introspection
;;; primitives (record?, record-type?, record-rtd, record-type-name,
;;; record-type-field-names) added alongside it in builtins.c.
;;;
;;; inspect-properties' exact key set is documented per-type in the SRFI,
;;; but this module deliberately implements a curry-scoped subset (see the
;;; header comment in lib/curry/modules/srfi/s279/inspect.scm), so these
;;; tests check "does this property exist with this value" via assoc
;;; rather than exact-equal against a hardcoded full alist — hash-table/
;;; set/bag iteration order isn't guaranteed, and pinning the exact key
;;; list would make the suite brittle against future scope additions.

(import (srfi 279) (srfi 69) (srfi 111) (srfi 113) (srfi 128)
        (scheme write) (scheme char))

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

;; (has label props key expected) — assoc-based property check.
(define (has label props key expected)
  (let ((entry (assoc key props)))
    (check label (and entry (cadr entry)) expected)))

;; (lacks label props key) — property must be absent, not present-as-#f
;; (SRFI-279's own rule: omit unsupported properties, never fill a dummy).
(define (lacks label props key)
  (check label (if (assoc key props) 'present 'absent) 'absent))

;;; ════════════════════════════════════════════════════════════
;;; § 1  object-properties — write/display, present for every type
;;; ════════════════════════════════════════════════════════════

(has "object-properties: write" (inspect-properties 42) 'write "42")
(has "object-properties: display" (inspect-properties "abc") 'display "abc")

;;; ════════════════════════════════════════════════════════════
;;; § 2  number-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties 42)))
  (has "number: real-part" p 'real-part 42)
  (has "number: imag-part" p 'imag-part 0)
  (has "number: numerator" p 'numerator 42)
  (has "number: denominator" p 'denominator 1)
  (has "number: real-sign positive" p 'real-sign 1)
  (has "number: integer->char" p 'integer->char (integer->char 42))
  (has "number: display-2" p 'display-2 "101010")
  (has "number: display-8" p 'display-8 "52")
  (has "number: display-16" p 'display-16 "2a"))

(has "number: real-sign negative" (inspect-properties -17) 'real-sign -1)
(has "number: real-sign zero" (inspect-properties 0) 'real-sign 0)

(let ((p (inspect-properties 3/4)))
  (has "rational: numerator" p 'numerator 3)
  (has "rational: denominator" p 'denominator 4)
  (lacks "rational: no display-2 (not an integer)" p 'display-2))

(let ((p (inspect-properties 3.14)))
  (has "flonum: real-part" p 'real-part 3.14)
  (lacks "flonum: no numerator (not rational? in this build's sense check)" p 'display-2))

(let ((p (inspect-properties (make-rectangular 3 4))))
  (has "complex: real-part" p 'real-part 3)
  (has "complex: imag-part" p 'imag-part 4))

(lacks "number: no integer->char for negative" (inspect-properties -1) 'integer->char)

;;; ════════════════════════════════════════════════════════════
;;; § 3  boolean-properties
;;; ════════════════════════════════════════════════════════════

(has "boolean: #t -> 1" (inspect-properties #t) 'boolean->integer 1)
(has "boolean: #f -> 0" (inspect-properties #f) 'boolean->integer 0)

;;; ════════════════════════════════════════════════════════════
;;; § 4  pair-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties (cons 1 2))))
  (has "pair: car" p 'car 1)
  (has "pair: cdr" p 'cdr 2)
  (lacks "dotted pair: no length" p 'length))

(let ((p (inspect-properties (list 1 2 3))))
  (has "list: last" p 'last 3)
  (has "list: last-pair" p 'last-pair (list 3))
  (has "list: length" p 'length 3)
  (has "list: list->vector" p 'list->vector (vector 1 2 3))
  (has "list: indexed 0" p 0 1)
  (has "list: indexed 1" p 1 2)
  (has "list: indexed 2" p 2 3)
  (lacks "list of numbers: no list->string" p 'list->string))

(has "char-list: list->string" (inspect-properties (list #\a #\b #\c)) 'list->string "abc")

;;; Empty list: not a pair (nil), so only object-properties apply.
(check "nil: no type-specific properties beyond write/display"
  (length (inspect-properties '())) 2)

;;; ════════════════════════════════════════════════════════════
;;; § 5  symbol-properties
;;; ════════════════════════════════════════════════════════════

(has "symbol: symbol->string" (inspect-properties 'hello) 'symbol->string "hello")

;;; ════════════════════════════════════════════════════════════
;;; § 6  character-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties #\a)))
  (has "char: char->integer" p 'char->integer 97)
  (has "char: char-alphabetic?" p 'char-alphabetic? #t)
  (has "char: char-lower-case?" p 'char-lower-case? #t))

(has "char: digit-value present for a digit" (inspect-properties #\7) 'digit-value 7)
(lacks "char: no digit-value for a letter" (inspect-properties #\a) 'digit-value)

;;; ════════════════════════════════════════════════════════════
;;; § 7  string-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties "abc")))
  (has "string: string->symbol" p 'string->symbol (string->symbol "abc"))
  (has "string: string->list" p 'string->list (list #\a #\b #\c))
  (has "string: string->vector" p 'string->vector (vector #\a #\b #\c))
  (has "string: string-length" p 'string-length 3)
  (has "string: indexed 0" p 0 #\a))

(has "string->number: valid numeric string" (inspect-properties "42") 'string->number 42)
(lacks "string->number: absent for non-numeric string" (inspect-properties "abc") 'string->number)
(lacks "string->number: absent for empty string (guards a core string->number bug)"
  (inspect-properties "") 'string->number)

;;; ════════════════════════════════════════════════════════════
;;; § 8  vector-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties (vector 1 2 3))))
  (has "vector: vector-length" p 'vector-length 3)
  (has "vector: vector->list" p 'vector->list (list 1 2 3))
  (has "vector: indexed 1" p 1 2)
  (lacks "vector of numbers: no vector->string" p 'vector->string))

(has "char-vector: vector->string" (inspect-properties (vector #\x #\y)) 'vector->string "xy")

;;; ════════════════════════════════════════════════════════════
;;; § 9  bytevector-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties (bytevector 1 2 3))))
  (has "bytevector: indexed 0" p 0 1)
  (has "bytevector: indexed 2" p 2 3))

;;; ════════════════════════════════════════════════════════════
;;; § 10  error-properties
;;; ════════════════════════════════════════════════════════════

(let* ((e (guard (exn (#t exn)) (error "boom" 1 2)))
       (p (inspect-properties e)))
  (has "error: error-object-message" p 'error-object-message "boom")
  (has "error: error-object-irritants" p 'error-object-irritants (list 1 2)))

;;; ════════════════════════════════════════════════════════════
;;; § 11  hash-table-properties (srfi 69)
;;; ════════════════════════════════════════════════════════════

(let* ((ht (make-hash-table))
       (dummy (hash-table-set! ht 'a 1))
       (p (inspect-properties ht)))
  (has "hash-table: hash-table-size" p 'hash-table-size 1)
  (has "hash-table: key inlined" p 'a 1))

;;; ════════════════════════════════════════════════════════════
;;; § 12  box-properties (srfi 111)
;;; ════════════════════════════════════════════════════════════

(has "box: unbox" (inspect-properties (box 5)) 'unbox 5)

;;; ════════════════════════════════════════════════════════════
;;; § 13  set / bag properties (srfi 113) — and the bag-vs-hash-table
;;; dispatch-order fix (bags are implemented as hash tables under the
;;; hood in (srfi s113 sets-and-bags), so bag? must be checked before
;;; the generic hash-table? branch or a bag would get hash-table's
;;; property set instead of its own).
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties (set equal-comparator 1 2 3))))
  (has "set: set-size" p 'set-size 3)
  (has "set: element 2 self-valued" p 2 2)
  (lacks "set: not dispatched as a hash-table" p 'hash-table-size))

(let ((p (inspect-properties (bag equal-comparator 1 1 2))))
  (has "bag: bag-size (counts duplicates)" p 'bag-size 3)
  (has "bag: bag-unique-size" p 'bag-unique-size 2)
  (has "bag: element 1 has count 2" p 1 2)
  (lacks "bag: not dispatched as a generic hash-table" p 'hash-table-size))

;;; ════════════════════════════════════════════════════════════
;;; § 14  record-properties, and the underlying record? / record-rtd /
;;; record-type-name / record-type-field-names primitives directly
;;; ════════════════════════════════════════════════════════════

(define-record-type <point> (make-point x y) point? (x point-x) (y point-y))
(define pt (make-point 3 4))

(check "record?: #t for a record instance" (record? pt) #t)
(check "record?: #f for a non-record" (record? 42) #f)
(check "record-type?: #t for an rtd" (record-type? (record-rtd pt)) #t)
(check "record-type?: #f for a non-rtd" (record-type? pt) #f)
(check "record-type-name" (record-type-name (record-rtd pt)) '<point>)
(check "record-type-field-names" (record-type-field-names (record-rtd pt)) '(x y))

(let ((p (inspect-properties pt)))
  (has "record: field x" p 'x 3)
  (has "record: field y" p 'y 4)
  (check "record: record-rtd is the same rtd" (assoc 'record-rtd p)
    (list 'record-rtd (record-rtd pt))))

(let ((p (inspect-properties (record-rtd pt))))
  (has "record-type: rtd-name" p 'rtd-name '<point>)
  (has "record-type: rtd-field-names" p 'rtd-field-names '(x y)))

;;; ════════════════════════════════════════════════════════════
;;; § 14.5  procedure-properties, and the underlying procedure-name /
;;; -arity / -arglist / -file / -line / -lambda / -closure primitives.
;;; Covers all three of curry's procedure representations: bytecode-VM
;;; closures (ordinary top-level defines), tree-walker closures (an
;;; internal define, which curry evaluates via tree-eval rather than
;;; compiling), and C primitives.
;;; ════════════════════════════════════════════════════════════

(define (add-two x y) (+ x y))
(define (make-adder n) (lambda (x) (+ x n)))
(define add5 (make-adder 5))
(define (varargs a . rest) (cons a rest))
(define (all-rest . xs) xs)

(check "procedure-name: named top-level define"    (procedure-name add-two) 'add-two)
(check "procedure-name: anonymous closure is #f"    (procedure-name add5) #f)
(check "procedure-name: primitive"                  (procedure-name car) 'car)
(check "procedure-name: non-procedure raises"
  (guard (e (#t 'raised)) (procedure-name 42)) 'raised)

(check "procedure-arity: fixed 2-arg"      (procedure-arity add-two) '(2 . 2))
(check "procedure-arity: fixed 1-arg (closure over n)" (procedure-arity add5) '(1 . 1))
(check "procedure-arity: one required + rest" (procedure-arity varargs) '(1 . #f))
(check "procedure-arity: all-rest (zero required)" (procedure-arity all-rest) '(0 . #f))
(check "procedure-arity: primitive, fixed"  (procedure-arity car) '(1 . 1))
(check "procedure-arity: primitive, variadic" (procedure-arity +) '(0 . #f))

(check "procedure-arglist: proper list"     (procedure-arglist add-two) '(x y))
(check "procedure-arglist: dotted (rest)"   (procedure-arglist varargs) '(a . rest))
(check "procedure-arglist: primitive is #f" (procedure-arglist car) #f)

(check "procedure-lambda: reconstructs the source form"
  (procedure-lambda add-two) '(lambda (x y) (+ x y)))
(check "procedure-lambda: primitive is #f" (procedure-lambda car) #f)

(check "procedure-closure: captures the free variable"
  (procedure-closure add5) '((n . 5)))
(check "procedure-closure: no captures for a top-level define"
  (procedure-closure add-two) '())
(check "procedure-closure: primitive is '()" (procedure-closure car) '())

(check "procedure-file: primitive is #f" (procedure-file car) #f)
(check "procedure-line: primitive is #f" (procedure-line car) #f)

;; A genuine tree-walker closure (T_CLOSURE, not T_BCCLOSURE) via
;; tree-eval, curry's own primitive for forcing evaluation through the
;; tree-walking interpreter rather than the bytecode VM -- an ordinary
;; internal (define ...) is NOT this path (it still compiles to a
;; BcClosure, confirmed via procedure-file returning a real filename
;; for one rather than #f).
(tree-eval '(define (tw-fn p q) (* p q)))
(check "procedure-arity works for a tree-walker closure too"
  (procedure-arity tw-fn) '(2 . 2))
(check "procedure-arglist works for a tree-walker closure too"
  (procedure-arglist tw-fn) '(p q))
(check "tree-walker closures have no source-location tracking (procedure-file)"
  (procedure-file tw-fn) #f)

(let ((p (inspect-properties add-two)))
  (has   "procedure: procedure-name"     p 'procedure-name 'add-two)
  (has   "procedure: procedure-arity"    p 'procedure-arity '(2 . 2))
  (has   "procedure: procedure-arglists" p 'procedure-arglists '((x y)))
  (has   "procedure: procedure-lambda"   p 'procedure-lambda '(lambda (x y) (+ x y)))
  (lacks "procedure: no procedure-closure for a top-level define" p 'procedure-closure))

(let ((p (inspect-properties add5)))
  (has   "procedure(closure): procedure-closure has the capture" p 'procedure-closure '((n . 5)))
  (lacks "procedure(closure): procedure-name is absent, not #f"  p 'procedure-name))

(let ((p (inspect-properties car)))
  (has   "procedure(primitive): procedure-name" p 'procedure-name 'car)
  (has   "procedure(primitive): procedure-arity" p 'procedure-arity '(1 . 1))
  (lacks "procedure(primitive): no procedure-arglists" p 'procedure-arglists)
  (lacks "procedure(primitive): no procedure-lambda"   p 'procedure-lambda)
  (lacks "procedure(primitive): no procedure-file"     p 'procedure-file)
  (lacks "procedure(primitive): no procedure-line"     p 'procedure-line)
  (lacks "procedure(primitive): no procedure-closure"  p 'procedure-closure))

;; Regression: procedure-closure on a closure whose env is a MODULE's own
;; root frame (define-library's env_new_root(), not GLOBAL_ENV itself)
;; used to leak that entire frame -- every import, every sibling private
;; define, even command-line-args -- because the "don't dump a shared
;; root scope" guard checked pointer-identity against GLOBAL_ENV
;; specifically instead of "is this a root frame at all" (found by
;; independent security review; verified to leak ~1933 bindings before
;; the fix -- every builtin, its Akkadian/cuneiform aliases, and the
;; real process argv, all reachable from an ordinary exported procedure
;; of ANY shipped module). Any root frame (parent == #f at the C level)
;; is now treated the same as GLOBAL_ENV: '(), not a real capture.
(define-library (test s279-modscope)
  (import (scheme base) (scheme write))
  (export get-inner)
  (begin
    (define (inner) (+ 1 2))
    (define (get-inner) inner)))
(import (test s279-modscope))
(check "procedure-closure on a module-scope closure does not leak the module frame"
  (procedure-closure (get-inner)) '())

;;; ════════════════════════════════════════════════════════════
;;; § 15  inspect-describe — human-readable output, doesn't raise
;;; ════════════════════════════════════════════════════════════

(let ((out (call-with-port (open-output-string)
             (lambda (p) (inspect-describe 42 p) (get-output-string p)))))
  (check "inspect-describe writes a representation of the object"
    out "42\n"))

(let ((out (call-with-port (open-output-string)
             (lambda (p) (inspect-describe (list 1 2 3) p) (get-output-string p)))))
  (check "inspect-describe on a list"
    out "(1 2 3)\n"))

;;; ════════════════════════════════════════════════════════════
;;; § 16  Unicode -- curry strings/chars are full Unicode codepoints
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties "𒀭")))
  (has "unicode string: string-length counts codepoints, not bytes" p 'string-length 1)
  (has "unicode string: indexed codepoint" p 0 (string-ref "𒀭" 0)))

(has "unicode char: char->integer" (inspect-properties #\𒀭) 'char->integer 73773)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
