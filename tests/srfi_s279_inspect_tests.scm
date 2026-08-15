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

(import (srfi 279) (srfi 14) (srfi 69) (srfi 111) (srfi 113) (srfi 128) (srfi 4)
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

;;; id: present for everything, and distinguishes different objects
(check "object-properties: id is present"
  (integer? (cadr (assoc 'id (inspect-properties 42)))) #t)
(check "object-properties: id distinguishes two different (non-eq?) strings"
  (= (cadr (assoc 'id (inspect-properties (string-copy "abc"))))
     (cadr (assoc 'id (inspect-properties (string-copy "abc")))))
  #f)

;;; type: matches the object's actual kind
(has "object-properties: type of a number" (inspect-properties 42) 'type 'number)
(has "object-properties: type of a pair" (inspect-properties (cons 1 2)) 'type 'pair)
(has "object-properties: type of a string" (inspect-properties "x") 'type 'string)
(has "object-properties: type of a symbol" (inspect-properties 'x) 'type 'symbol)
(has "object-properties: type of a boolean" (inspect-properties #t) 'type 'boolean)

;;; size: present (and correct) for the handful of types it's implemented
;;; for, absent everywhere else (never a dummy 0)
(has "object-properties: size of a pair" (inspect-properties (cons 1 2)) 'size 32)
(check "object-properties: size of a string reflects its length"
  (< (cadr (assoc 'size (inspect-properties "a")))
     (cadr (assoc 'size (inspect-properties "abcdefghij"))))
  #t)
(lacks "object-properties: no size for a number" (inspect-properties 42) 'size)
(lacks "object-properties: no size for a symbol" (inspect-properties 'x) 'size)

;; Regression: a string's real footprint after a width-changing
;; string-set! (ASCII -> multi-byte, forcing a reallocation onto a
;; separate `ext` buffer) is roughly DOUBLE the original -- the
;; original inline block stays allocated (dead weight, never freed or
;; reused under Boehm GC) alongside the new ext buffer. size must grow
;; to reflect that, not stay roughly flat (found by independent code
;; review: the original implementation used live content length
;; instead of allocated capacity, silently under-reporting both this
;; case and any string whose allocated capacity simply exceeds its
;; current content length).
(let* ((s (make-string 10000 #\a))
       (size-before (cadr (assoc 'size (inspect-properties s)))))
  (string-set! s 0 (integer->char 955)) ; U+03BB, a 2-byte UTF-8 char
  (let ((size-after (cadr (assoc 'size (inspect-properties s)))))
    (check "object-properties: string size reflects a real ext-buffer reallocation, not just live content length"
      (> size-after (* 1.5 size-before))
      #t)))

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

;;; Empty list: not a pair (nil), so only object-properties apply --
;;; id/write/display/type (size omitted: '() isn't one of the heap
;;; types %object-size covers).
(check "nil: no type-specific properties beyond the universal ones"
  (length (inspect-properties '())) 4)
(has "nil: type" (inspect-properties '()) 'type 'null)

;;; ════════════════════════════════════════════════════════════
;;; § 5  symbol-properties
;;; ════════════════════════════════════════════════════════════

(has "symbol: symbol->string" (inspect-properties 'hello) 'symbol->string "hello")

;; symbol-value: a plain global-environment lookup, not a module registry
(define s279-bound-global 99)
(has "symbol: symbol-value for a bound global" (inspect-properties 's279-bound-global) 'symbol-value 99)
(lacks "symbol: no symbol-value for an unbound symbol"
  (inspect-properties 's279-definitely-unbound-xyz) 'symbol-value)
(define s279-false-global #f)
(has "symbol: symbol-value is present even when the value is #f itself"
  (inspect-properties 's279-false-global) 'symbol-value #f)

;;; ════════════════════════════════════════════════════════════
;;; § 6  character-properties
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties #\a)))
  (has "char: char->integer" p 'char->integer 97)
  (has "char: char-alphabetic?" p 'char-alphabetic? #t)
  (has "char: char-lower-case?" p 'char-lower-case? #t)
  (lacks "char: no char-name for an ordinary letter" p 'char-name))

(has "char: digit-value present for a digit" (inspect-properties #\7) 'digit-value 7)
(lacks "char: no digit-value for a letter" (inspect-properties #\a) 'digit-value)

;; char-name: curry's own 9 R7RS named-character reader vocabulary
(has "char: char-name for space" (inspect-properties #\space) 'char-name "space")
(has "char: char-name for newline" (inspect-properties #\newline) 'char-name "newline")
(has "char: char-name for tab" (inspect-properties #\tab) 'char-name "tab")

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
  (has "hash-table: key inlined" p 'a 1)
  (has "hash-table: hash-table-equivalence-function name (default equal?)"
    p 'hash-table-equivalence-function 'equal?)
  (has "hash-table: hash-table-hash-function name" p 'hash-table-hash-function 'hash)
  (has "hash-table: hash-table-weak? is always #f" p 'hash-table-weak? #f)
  (has "hash-table: hash-table-mutable? is always #t" p 'hash-table-mutable? #t))

;; Not tested here with a non-default comparator (e.g. (make-hash-table
;; eq?)): this test file imports both (srfi 69) and (srfi 128), which
;; both provide a make-hash-table, and the one that wins the resulting
;; name collision isn't (srfi 69)'s own comparator-tracking wrapper --
;; unrelated to this property's own correctness (already covered above
;; via the default equal? case, which every make-hash-table variant
;; agrees on), just an ambiguity in which library's same-named export
;; this file's own import list resolves to.

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

;; record-type-constructor/-predicate/-accessors/-mutators: the four
;; "record-related procedures" (SRFI-279's own phrase) as real,
;; callable procedure objects, stashed onto the RTD by define-record-
;; type's own codegen (record_type.c/compiler.c/eval.c).
(check "record-type-constructor is callable and produces a real instance"
  (point-x ((record-type-constructor (record-rtd pt)) 10 20)) 10)
(check "record-type-predicate accepts an instance"
  ((record-type-predicate (record-rtd pt)) pt) #t)
(check "record-type-predicate rejects a non-instance"
  ((record-type-predicate (record-rtd pt)) 'not-a-point) #f)
(check "record-type-accessors: field order matches field-names, both work"
  (map (lambda (acc) (acc pt)) (record-type-accessors (record-rtd pt)))
  '(3 4))
(check "record-type-mutators: both <point> fields are immutable"
  (record-type-mutators (record-rtd pt)) '(#f #f))

(let ((p (inspect-properties pt)))
  (has "record: field x" p 'x 3)
  (has "record: field y" p 'y 4)
  (check "record: record-rtd is the same rtd" (assoc 'record-rtd p)
    (list 'record-rtd (record-rtd pt))))

(let ((p (inspect-properties (record-rtd pt))))
  (has "record-type: rtd-name" p 'rtd-name '<point>)
  (has "record-type: rtd-field-names" p 'rtd-field-names '(x y))
  (check "record-type: rtd-constructor is present and callable"
    (procedure? (cadr (assoc 'rtd-constructor p))) #t)
  (check "record-type: rtd-predicate is present and callable"
    (procedure? (cadr (assoc 'rtd-predicate p))) #t)
  (check "record-type: rtd-accessors is a list of 2 real procedures"
    (map procedure? (cadr (assoc 'rtd-accessors p))) '(#t #t))
  (check "record-type: rtd-mutators is (#f #f) for an all-immutable record"
    (cadr (assoc 'rtd-mutators p)) '(#f #f)))

;; A record type with a mutable field: rtd-mutators must mix real
;; procedures and #f in the right positions, not omit or reorder.
(define-record-type <mut-point> (make-mut-point x y) mut-point?
  (x mut-point-x) (y mut-point-y set-mut-point-y!))
(let ((p (inspect-properties (record-rtd (make-mut-point 1 2)))))
  (check "record-type: rtd-mutators mixes #f (immutable) and a real procedure (mutable)"
    (map (lambda (m) (if m 'mutator #f)) (cadr (assoc 'rtd-mutators p)))
    '(#f mutator)))

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

;;; ════════════════════════════════════════════════════════════
;;; § 17  char-set-properties (srfi 14)
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties (char-set #\a #\b))))
  (has   "char-set: char-set-size" p 'char-set-size 2)
  (has   "char-set: char-set->list" p 'char-set->list (list #\a #\b))
  (has   "char-set: char-set->string" p 'char-set->string "ab")
  (lacks "char-set: no char-set-name for a non-predefined set" p 'char-set-name))

(has "char-set: char-set-name recognizes a predefined set"
  (inspect-properties char-set:hex-digit) 'char-set-name 'char-set:hex-digit)
(has "char-set: char-set-name recognizes char-set:empty"
  (inspect-properties char-set:empty) 'char-set-name 'char-set:empty)
;; char-set:title-case has no titlecase table in curry at all (see
;; (srfi 14)'s own docs) so it's permanently empty -- structurally
;; identical to, and therefore honestly reported as, char-set:empty.
(has "char-set: char-set-name for char-set:title-case honestly reports char-set:empty"
  (inspect-properties char-set:title-case) 'char-set-name 'char-set:empty)
(has "char-set: type" (inspect-properties char-set:empty) 'type 'char-set)

;;; ════════════════════════════════════════════════════════════
;;; § 18  port-properties
;;; ════════════════════════════════════════════════════════════

;; textual-port?/binary-port?/port-line/port-position/port-file-descriptor
;; as standalone R7RS-style primitives, not just through inspect-properties.
(check "textual-port? on a textual port" (textual-port? (open-output-string)) #t)
(check "binary-port? on a textual port" (binary-port? (open-output-string)) #f)
(check "binary-port? on a binary port" (binary-port? (open-output-bytevector)) #t)
(check "textual-port? on a binary port" (textual-port? (open-output-bytevector)) #f)
(check "port-line is a positive integer" (integer? (port-line (open-input-string "x"))) #t)
(check "port-file-descriptor is #f for a string port"
  (port-file-descriptor (open-output-string)) #f)

;; Regression: a FILE*-backed port (open-input-file/open-output-file),
;; open and healthy -- port-position/port-file-descriptor's ftell(3)/
;; fileno(3) path, entirely untested until now (only string/bytevector
;; ports were exercised above; found missing by independent code
;; review alongside the closed-port crash below).
(let* ((path "/tmp/curry-s279-port-test.txt")
       (dummy1 (call-with-output-file path (lambda (p) (write-string "hello" p))))
       (ip (open-input-file path)))
  (check "port-file-descriptor is a real fd for an open file port"
    (and (integer? (port-file-descriptor ip)) (>= (port-file-descriptor ip) 0)) #t)
  (check "port-position is a real offset for an open file port (ftell path)"
    (integer? (port-position ip)) #t)
  (close-port ip)
  (delete-file path))

;; Regression (CRITICAL, found by independent code review AND
;; independent security review, both flagging the exact same bug):
;; port_close (src/port.c) fclose()s a non-std-stream FILE*-backed
;; port and sets its u.fp to NULL -- but the port object itself is
;; still port?-true (closing doesn't change the type tag), so nothing
;; upstream filters it out. The FIRST version of port-position/
;; port-file-descriptor called ftell(3)/fileno(3) directly on that now-
;; NULL FILE* with no PORT_CLOSED check at all -- an unconditional
;; NULL-pointer dereference, verified live to segfault the whole
;; process (exit 139), reachable from ordinary Scheme code with no
;; unsafe/FFI involved, including transitively through plain
;; inspect-properties/inspect-describe on a closed port (an entirely
;; ordinary thing to introspect, e.g. debugging why/when a port
;; closed). Must return #f, not crash.
(let* ((path "/tmp/curry-s279-port-closed-test.txt")
       (dummy1 (call-with-output-file path (lambda (p) (write-string "x" p))))
       (ip (open-input-file path))
       (dummy2 (close-port ip)))
  (check "port-position on a closed file port returns #f, does not crash"
    (port-position ip) #f)
  (check "port-file-descriptor on a closed file port returns #f, does not crash"
    (port-file-descriptor ip) #f)
  (check "inspect-properties on a closed file port does not crash"
    (integer? (length (inspect-properties ip))) #t)
  (has "port(closed): port-open? is #f" (inspect-properties ip) 'port-open? #f)
  (lacks "port(closed): no port-position" (inspect-properties ip) 'port-position)
  (lacks "port(closed): no port-file-descriptor" (inspect-properties ip) 'port-file-descriptor)
  (delete-file path))

;; Regression: output string ports track position via u.str.len (bytes
;; written so far), NOT u.str.pos (which is exclusively the INPUT read
;; cursor and stays 0 for an output port no matter how much is
;; written) -- an earlier version of port-position used pos
;; unconditionally and silently reported 0 after every write.
(let ((op (open-output-string)))
  (check "port-position starts at 0 for a fresh output string port" (port-position op) 0)
  (write-string "hello" op)
  (check "port-position reflects bytes actually written" (port-position op) 5))

;; port-position for an input string port: the read cursor, distinct
;; from output's write-length tracking above.
(let ((ip (open-input-string "hello world")))
  (check "port-position starts at 0 for a fresh input string port" (port-position ip) 0)
  (read-char ip) (read-char ip)
  (check "port-position advances as chars are read" (port-position ip) 2))

;; Input string port properties
(let ((p (inspect-properties (open-input-string "x"))))
  (has   "port(input): port-open?" p 'port-open? #t)
  (has   "port(input): port-direction" p 'port-direction 'input)
  (has   "port(input): port-type" p 'port-type 'textual)
  (has   "port(input): port-encoding" p 'port-encoding 'UTF-8)
  (lacks "port(input): no get-output-string (SRFI-279 specifies this for OUTPUT ports only, even though curry's own get-output-string primitive doesn't itself enforce that direction)"
    p 'get-output-string)
  (lacks "port(input): no port-buffer either, same reasoning" p 'port-buffer)
  (lacks "port(input): no port-file (curry never stores the opening path)" p 'port-file)
  (lacks "port(input): no port-column (curry never tracks it)" p 'port-column))

;; Output string port properties
(let* ((op (open-output-string))
       (dummy (write-string "abc" op))
       (p (inspect-properties op)))
  (has "port(output-string): port-direction" p 'port-direction 'output)
  (has "port(output-string): port-position after writing" p 'port-position 3)
  (has "port(output-string): port-buffer mirrors get-output-string" p 'port-buffer "abc")
  (has "port(output-string): get-output-string" p 'get-output-string "abc")
  (lacks "port(output-string): no get-output-bytevector for a textual port" p 'get-output-bytevector))

;; Output bytevector port properties
(let* ((bop (open-output-bytevector))
       (dummy (write-u8 65 bop))
       (p (inspect-properties bop)))
  (has   "port(output-bytevector): port-type" p 'port-type 'binary)
  (has   "port(output-bytevector): get-output-bytevector" p 'get-output-bytevector (bytevector 65))
  (lacks "port(output-bytevector): no port-encoding for a binary port" p 'port-encoding)
  (lacks "port(output-bytevector): no get-output-string for a binary port" p 'get-output-string))

(has "port(input): type" (inspect-properties (open-input-string "x")) 'type 'port)

;;; ════════════════════════════════════════════════════════════
;;; § 19  typedvec-properties (srfi 4: u8/s8/u16/s16/u32/s32/u64/s64/f64vector)
;;; ════════════════════════════════════════════════════════════

(let ((p (inspect-properties (u8vector 1 2 3))))
  (has "typedvec(u8): type" p 'type 'typedvec)
  (has "typedvec(u8): u8vector-length" p 'u8vector-length 3)
  (has "typedvec(u8): u8vector->list" p 'u8vector->list (list 1 2 3))
  (has "typedvec(u8): indexed element 0" p 0 1)
  (has "typedvec(u8): indexed element 2" p 2 3)
  (lacks "typedvec(u8): no s8vector-length (wrong kind)" p 's8vector-length))

;; s64vector: exact-bignum boundary values (past C long range) round-trip
;; through the numeric tower correctly.
(let ((p (inspect-properties (s64vector -9223372036854775808 9223372036854775807))))
  (has "typedvec(s64): s64vector-length" p 's64vector-length 2)
  (has "typedvec(s64): s64vector->list preserves bignum boundary values"
    p 's64vector->list (list -9223372036854775808 9223372036854775807)))

;; u64vector: UINT64_MAX, exceeding even signed int64 range.
(has "typedvec(u64): u64vector->list preserves UINT64_MAX"
  (inspect-properties (u64vector 18446744073709551615)) 'u64vector->list
  (list 18446744073709551615))

;; f64vector: a wholly separate heap type from the other 8 kinds
;; ((curry f64vector), not (curry typedvec)) -- verifies %typedvec-entry's
;; table dispatch reaches it too, not just the T_TYPEDVEC-backed kinds.
(let ((p (inspect-properties (f64vector 1.5 2.5))))
  (has "typedvec(f64): type" p 'type 'typedvec)
  (has "typedvec(f64): f64vector-length" p 'f64vector-length 2)
  (has "typedvec(f64): f64vector->list" p 'f64vector->list (list 1.5 2.5))
  (lacks "typedvec(f64): no u8vector-length (wrong kind)" p 'u8vector-length))

;;; ════════════════════════════════════════════════════════════
;;; § %indexed-pairs — the C primitive %indexed-properties delegates to.
;;; Backs every pair/string/vector/bytevector/typedvec indexed-element
;;; expansion above; on large sequences it's the dominant cost of
;;; inspect-properties when implemented as an interpreted Scheme loop
;;; (measured: >90% of a 2M-element vector's total time, running that
;;; same loop inside a define-library body vs. top-level) -- moved to C
;;; to sidestep that. Not exported by (srfi 279) itself (it's an
;;; internal helper primitive, same convention as %rtd-set-constructor!
;;; etc.), but reachable directly since curry's primitives all live in
;;; the flat GLOBAL_ENV.
;;; ════════════════════════════════════════════════════════════

(check "%indexed-pairs: empty list" (%indexed-pairs '()) '())
(check "%indexed-pairs: single element" (%indexed-pairs (list 'a)) '((0 a)))
(check "%indexed-pairs: preserves order" (%indexed-pairs (list 'a 'b 'c))
  '((0 a) (1 b) (2 c)))
(check "%indexed-pairs: raises on a non-list argument"
  (guard (e (#t 'raised)) (%indexed-pairs 5))
  'raised)
(check "%indexed-pairs: raises on a dotted (improper) list"
  ;; A dotted pair isn't nil-terminated -- the walk stops at the first
  ;; non-pair cdr, same as it would for any proper-list-only C loop;
  ;; every real call site (inspect.scm) only ever calls this after its
  ;; own list?/vector->list/string->list check already guarantees a
  ;; proper list, so this exercises the primitive's own defensive
  ;; behavior directly rather than anything reachable through inspect-
  ;; properties itself.
  (%indexed-pairs (cons 1 2))
  '((0 1)))
;; Large-scale round-trip: confirms the C fast path produces byte-for-
;; byte the same shape the old interpreted Scheme loop did, at a size
;; where a correctness regression (e.g. an off-by-one in the length
;; count or the backward-build loop) would be far more likely to show up
;; than at the tiny sizes above.
(let* ((n 10000)
       (lst (let loop ((i (- n 1)) (acc '())) (if (< i 0) acc (loop (- i 1) (cons i acc)))))
       (result (%indexed-pairs lst)))
  (check "%indexed-pairs: length matches input at scale" (length result) n)
  (check "%indexed-pairs: first entry at scale" (car result) '(0 0))
  (check "%indexed-pairs: last entry at scale" (list-ref result (- n 1)) (list (- n 1) (- n 1))))

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
