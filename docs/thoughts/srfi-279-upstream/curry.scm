;; SPDX-FileCopyrightText: 2026 Scáth
;; SPDX-License-Identifier: MIT

;;; Permission is hereby granted, free of charge, to any person
;;; obtaining a copy of this software and associated documentation
;;; files (the "Software"), to deal in the Software without
;;; restriction, including without limitation the rights to use,
;;; copy, modify, merge, publish, distribute, sublicense, and/or
;;; sell copies of the Software, and to permit persons to whom the
;;; Software is furnished to do so, subject to the following
;;; conditions:
;;;
;;; The above copyright notice and this permission notice shall be
;;; included in all copies or substantial portions of the Software.
;;;
;;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;;; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;;; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;;; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;;; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;;; OTHER DEALINGS IN THE SOFTWARE.

;;; curry (https://github.com/deconstructo/curry) implementation of
;;; SRFI 279, included into (srfi 279) via 279.sld's own cond-expand
;;; clause for the 'curry feature identifier. Written directly against
;;; curry's own primitives rather than layered on another Scheme's
;;; introspection facilities.
;;;
;;; Property groups covered: object (id/write/display/size/type),
;;; number, boolean, pair, symbol, character, string, vector, typed
;;; numeric vector (u8/s8/u16/s16/u32/s32/u64/s64/f64), bytevector,
;;; record and record-type, error object, hash table (srfi 69), box
;;; (srfi 111), set/bag (srfi 113), character set (srfi 14), procedure,
;;; and port.
;;;
;;; Deliberately omitted: library/environment properties and
;;; symbol-library/symbol-exported? -- curry's module registry has no
;;; Scheme-level enumeration API to back them with real data, and this
;;; implementation follows the SRFI's own rule of omitting an
;;; unsupported property rather than filling it with a placeholder.

(define (%to-string-with object proc)
  (call-with-port (open-output-string)
    (lambda (p) (proc object p) (get-output-string p))))

(define (%type-name object)
  (cond
    ((number? object)      'number)
    ((boolean? object)     'boolean)
    ((box? object)         'box)
    ((bag? object)         'bag)
    ((hash-table? object)  'hash-table)
    ((set? object)         'set)
    ((char-set? object)    'char-set)
    ((%typedvec? object)   'typedvec)
    ((pair? object)        'pair)
    ((symbol? object)      'symbol)
    ((char? object)        'char)
    ((string? object)      'string)
    ((vector? object)      'vector)
    ((bytevector? object)  'bytevector)
    ((error-object? object) 'error)
    ((record-type? object) 'record-type)
    ((record? object)      'record)
    ((procedure? object)   'procedure)
    ((port? object)        'port)
    ((null? object)        'null)
    ((eof-object? object)  'eof)
    (else #f)))

(define (%object-properties object)
  (append
    (list (list 'id      (%object-id object))
          (list 'write   (%to-string-with object write))
          (list 'display (%to-string-with object display)))
    (let ((sz (%object-size object))) (if sz (list (list 'size sz)) '()))
    (let ((ty (%type-name object)))   (if ty (list (list 'type ty)) '()))))

;;; ---- Number ----

(define (%number-properties object)
  (append
    (list (list 'real-part (real-part object))
          (list 'imag-part (imag-part object)))
    (if (rational? object)
        (list (list 'numerator (numerator object))
              (list 'denominator (denominator object))
              (list 'real-sign (cond ((negative? object) -1)
                                      ((zero? object) 0)
                                      (else 1)))
              (list 'real-base 2))
        '())
    (if (and (integer? object) (>= object 0) (<= object #x10FFFF))
        (list (list 'integer->char (integer->char (exact object))))
        '())
    (if (and (integer? object) (exact? object))
        (list (list 'display-2  (number->string object 2))
              (list 'display-8  (number->string object 8))
              (list 'display-16 (number->string object 16)))
        '())))

;;; ---- Boolean ----

(define (%boolean-properties object)
  (list (list 'boolean->integer (if object 1 0))))

;;; ---- Pair ----
;;;
;;; `list?` is R7RS-guaranteed to be #f for both dotted and circular
;;; pairs (it does its own cycle detection), so it's the single check
;;; that safely gates every list-consuming property below -- a
;;; circular or dotted pair still gets car/cdr, just not the rest.

(define (%pair-properties object)
  (append
    (list (list 'car (car object)) (list 'cdr (cdr object)))
    (if (list? object)
        (append
          (list (list 'last (car (last-pair object)))
                (list 'last-pair (last-pair object))
                (list 'length (length object)))
          (if (every char? object)
              (list (list 'list->string (list->string object)))
              '())
          (list (list 'list->vector (list->vector object)))
          (%indexed-properties object))
        '())))

;;; ---- Symbol ----
;;;
;;; symbol-library/symbol-exported?/symbol-properties from the SRFI's
;;; own list need a library/property registry curry's module system
;;; doesn't expose to Scheme -- symbol->string and symbol-value (a
;;; plain global-environment lookup, needing no registry at all) are
;;; the two properties here with no such dependency.

;; A private, freshly-allocated sentinel (never eq? to any real Scheme
;; value, including #f) so "genuinely unbound" is distinguishable from
;; "bound, and its value happens to be #f".
(define %unbound-sentinel (list 'unbound))

(define (%symbol-value object)
  (guard (e (#t %unbound-sentinel)) (eval object (interaction-environment))))

(define (%symbol-properties object)
  (append
    (list (list 'symbol->string (symbol->string object)))
    (let ((v (%symbol-value object)))
      (if (eq? v %unbound-sentinel) '() (list (list 'symbol-value v))))))

;;; ---- Character ----

;; Exactly the 9 R7RS named characters curry's #\name reader syntax
;; recognizes, not a full Unicode character-name database.
(define %named-chars
  (list (cons #\space "space") (cons #\newline "newline")
        (cons #\tab "tab") (cons #\return "return")
        (cons (integer->char 0) "null") (cons (integer->char 27) "escape")
        (cons (integer->char 127) "delete") (cons (integer->char 7) "alarm")
        (cons (integer->char 8) "backspace")))

(define (%char-name object) (cond ((assv object %named-chars) => cdr) (else #f)))

(define (%character-properties object)
  (append
    (list (list 'char->integer (char->integer object)))
    (if (char-numeric? object)
        (let ((d (digit-value object)))
          (if d (list (list 'digit-value d)) '()))
        '())
    (let ((n (%char-name object))) (if n (list (list 'char-name n)) '()))
    (list (list 'char-alphabetic? (char-alphabetic? object))
          (list 'char-numeric?    (char-numeric? object))
          (list 'char-whitespace? (char-whitespace? object))
          (list 'char-upper-case? (char-upper-case? object))
          (list 'char-lower-case? (char-lower-case? object)))))

;;; ---- String ----

(define (%string-properties object)
  (let ((lst (string->list object)))
    (append
      (list (list 'string->symbol (string->symbol object))
            (list 'string->list   lst)
            (list 'string->vector (list->vector lst))
            (list 'string->utf8   (string->utf8 object))
            (list 'string-length  (string-length object)))
      (let ((n (string->number object)))
        (if n (list (list 'string->number n)) '()))
      (%indexed-properties lst))))

;;; ---- Vector ----

(define (%vector-properties object)
  (let ((lst (vector->list object)))
    (append
      (list (list 'vector-length (vector-length object))
            (list 'vector->list lst))
      (if (every char? lst)
          (list (list 'vector->string (list->string lst)))
          '())
      (%indexed-properties lst))))

;;; ---- Typed numeric vector (srfi 4: u8/s8/u16/s16/u32/s32/u64/s64,
;;; plus f64vector, a separate module in curry) ----
;;;
;;; No single predicate identifies any typed-vector kind across all 9 --
;;; each kind's own predicate/length/->list procedures are looked up
;;; via this table instead. Property keys are the real per-kind
;;; procedure names (e.g. 'u8vector-length, not a generic
;;; 'typedvec-length).

(define %typedvec-table
  (list
    (list u8vector?  'u8vector-length  u8vector-length  'u8vector->list  u8vector->list)
    (list s8vector?  's8vector-length  s8vector-length  's8vector->list  s8vector->list)
    (list u16vector? 'u16vector-length u16vector-length 'u16vector->list u16vector->list)
    (list s16vector? 's16vector-length s16vector-length 's16vector->list s16vector->list)
    (list u32vector? 'u32vector-length u32vector-length 'u32vector->list u32vector->list)
    (list s32vector? 's32vector-length s32vector-length 's32vector->list s32vector->list)
    (list u64vector? 'u64vector-length u64vector-length 'u64vector->list u64vector->list)
    (list s64vector? 's64vector-length s64vector-length 's64vector->list s64vector->list)
    (list f64vector? 'f64vector-length f64vector-length 'f64vector->list f64vector->list)))

(define (%typedvec-entry object)
  (let loop ((table %typedvec-table))
    (cond ((null? table) #f)
          (((caar table) object) (car table))
          (else (loop (cdr table))))))

(define (%typedvec? object) (if (%typedvec-entry object) #t #f))

(define (%typedvec-properties object)
  (let* ((e (%typedvec-entry object))
         (len-key (list-ref e 1)) (len-fn (list-ref e 2))
         (list-key (list-ref e 3)) (list-fn (list-ref e 4))
         (elements (list-fn object)))
    (append
      (list (list len-key (len-fn object))
            (list list-key elements))
      (%indexed-properties elements))))

;;; ---- Bytevector ----

;; curry's utf8->string validates its input and raises on malformed
;; UTF-8 -- guard it so a bytevector that isn't valid UTF-8 just omits
;; this entry instead of making the whole inspect-properties call raise
;; for an unrelated bytevector.
(define (%bytevector-properties object)
  (append
    (guard (e (#t '()))
      (list (list 'utf8->string (utf8->string object))))
    (%indexed-properties (bytevector->list object))))

(define (bytevector->list bv)
  (let loop ((i (- (bytevector-length bv) 1)) (acc '()))
    (if (< i 0) acc (loop (- i 1) (cons (bytevector-u8-ref bv i) acc)))))

;;; ---- Record ----

(define (%record-properties object)
  (let* ((rtd (record-rtd object))
         (names (record-type-field-names rtd)))
    (append
      (list (list 'record-rtd rtd))
      (%rtd-field-properties names (lambda (i) (%record-field-ref object i))))))

(define (%record-field-ref object i) (%record-ref object i))

;; constructor/predicate are omitted in the (should-be-unreachable)
;; case they're #f; accessors is always a full list of real procedures
;; (every field has one, by both R6RS and R7RS's own grammar); mutators
;; keeps a #f entry INLINE for each field declared immutable rather
;; than omitting the whole property, so a caller can still line
;; mutators up against field-names/accessors by position.
(define (%rtd-properties rtd)
  (append
    (list (list 'rtd-name (record-type-name rtd))
          (list 'rtd-field-names (record-type-field-names rtd)))
    (%omit 'rtd-constructor (record-type-constructor rtd))
    (%omit 'rtd-predicate   (record-type-predicate rtd))
    (list (list 'rtd-accessors (record-type-accessors rtd))
          (list 'rtd-mutators  (record-type-mutators rtd)))))

(define (%rtd-field-properties names ref)
  (let loop ((names names) (i 0) (acc '()))
    (if (null? names)
        (reverse acc)
        (loop (cdr names) (+ i 1) (cons (list (car names) (ref i)) acc)))))

;;; ---- Error / exception ----

(define (%error-properties object)
  (list (list 'error-object-message (error-object-message object))
        (list 'error-object-irritants (error-object-irritants object))))

;;; ---- Hash table (srfi 69) ----
;;;
;;; hash-table-equivalence-function/-hash-function want symbols (names
;;; of the procedures defining hash-table behavior), not procedure
;;; objects -- procedure-name converts. curry's hash tables have no
;;; weak-reference variant and are always mutable, so those two are
;;; fixed constants, not per-object queries.

(define (%hash-table-properties object)
  (append
    (list (list 'hash-table-size (hash-table-size object)))
    (let ((n (procedure-name (hash-table-equivalence-function object))))
      (if n (list (list 'hash-table-equivalence-function n)) '()))
    (let ((n (procedure-name (hash-table-hash-function object))))
      (if n (list (list 'hash-table-hash-function n)) '()))
    (list (list 'hash-table-weak? #f)
          (list 'hash-table-mutable? #t))
    (map (lambda (kv) (list (car kv) (cdr kv))) (hash-table->alist object))))

;;; ---- Box (srfi 111) ----

(define (%box-properties object)
  (list (list 'unbox (unbox object))))

;;; ---- Set / bag (srfi 113) ----

(define (%set-properties object)
  (append
    (list (list 'set-size (set-size object)))
    (map (lambda (e) (list e e)) (set->list object))))

;; bag->alist already gives (element . count), more informative than a
;; set would be for something that's fundamentally a multiset.
(define (%bag-properties object)
  (append
    (list (list 'bag-size (bag-size object))
          (list 'bag-unique-size (bag-unique-size object)))
    (map (lambda (kv) (list (car kv) (cdr kv))) (bag->alist object))))

;;; ---- Character set (srfi 14) ----

;; char-set-name is only meaningful for one of the standard predefined
;; sets -- a reverse lookup against that fixed list via char-set=, not
;; a stored name (user-built char-sets are structurally anonymous).
(define %named-char-sets
  (list (cons 'char-set:lower-case char-set:lower-case)
        (cons 'char-set:upper-case char-set:upper-case)
        (cons 'char-set:letter char-set:letter)
        (cons 'char-set:digit char-set:digit)
        (cons 'char-set:letter+digit char-set:letter+digit)
        (cons 'char-set:graphic char-set:graphic)
        (cons 'char-set:printing char-set:printing)
        (cons 'char-set:whitespace char-set:whitespace)
        (cons 'char-set:iso-control char-set:iso-control)
        (cons 'char-set:punctuation char-set:punctuation)
        (cons 'char-set:symbol char-set:symbol)
        (cons 'char-set:hex-digit char-set:hex-digit)
        (cons 'char-set:blank char-set:blank)
        (cons 'char-set:ascii char-set:ascii)
        (cons 'char-set:full char-set:full)
        (cons 'char-set:empty char-set:empty)))

(define (%char-set-name object)
  (let loop ((known %named-char-sets))
    (cond ((null? known) #f)
          ((char-set= object (cdar known)) (caar known))
          (else (loop (cdr known))))))

(define (%char-set-properties object)
  (append
    (list (list 'char-set-size (char-set-size object))
          (list 'char-set->list (char-set->list object))
          (list 'char-set->string (char-set->string object)))
    (let ((n (%char-set-name object)))
      (if n (list (list 'char-set-name n)) '()))))

;;; ---- Procedure ----
;;;
;;; Each accessor already returns #f (or, for procedure-closure, '())
;;; when curry has nothing to report for that procedure representation
;;; (primitives have no source file/line/arglist/closure; the
;;; tree-walker has no source location at all) -- %omit turns that into
;;; "key absent" rather than "key present with a dummy #f".

(define (%omit key value) (if (or (eq? value #f) (null? value)) '() (list (list key value))))

(define (%procedure-properties object)
  (append
    (%omit 'procedure-name     (procedure-name object))
    (list  (list 'procedure-arity (procedure-arity object)))
    (%omit 'procedure-arglists (let ((a (procedure-arglist object))) (if a (list a) #f)))
    (%omit 'procedure-file     (procedure-file object))
    (%omit 'procedure-line     (procedure-line object))
    (%omit 'procedure-lambda   (procedure-lambda object))
    (%omit 'procedure-closure  (procedure-closure object))))

;;; ---- Port ----
;;;
;;; port-file is omitted entirely: curry's port representation never
;;; stores the path a file port was opened from. port-column is
;;; similarly omitted: curry tracks line but never column. port-encoding
;;; is a fixed 'UTF-8 for textual ports (curry is UTF-8-only throughout),
;;; omitted for binary ports.

(define (%port-open? object)
  (or (and (input-port? object) (input-port-open? object))
      (and (output-port? object) (output-port-open? object))))

(define (%port-direction object)
  (cond ((and (input-port? object) (output-port? object)) 'both)
        ((input-port? object) 'input)
        ((output-port? object) 'output)
        (else #f)))

;; get-output-string/get-output-bytevector are meant for string/
;; bytevector OUTPUT ports specifically -- output-port? is checked
;; explicitly, since curry's own accessors don't enforce that boundary
;; themselves. textual-port?/binary-port? distinguish which of the two
;; accessors actually applies.
(define (%port-buffer object)
  (and (output-port? object)
       (if (textual-port? object)
           (guard (e (#t #f)) (get-output-string object))
           (guard (e (#t #f)) (get-output-bytevector object)))))

(define (%port-properties object)
  (append
    (list (list 'port-open? (%port-open? object))
          (list 'port-direction (%port-direction object))
          (list 'port-type (if (binary-port? object) 'binary 'textual))
          (list 'port-line (port-line object)))
    (let ((pos (port-position object))) (if pos (list (list 'port-position pos)) '()))
    (let ((fd (port-file-descriptor object))) (if fd (list (list 'port-file-descriptor fd)) '()))
    (if (textual-port? object) (list (list 'port-encoding 'UTF-8)) '())
    (let ((buf (%port-buffer object))) (if buf (list (list 'port-buffer buf)) '()))
    (if (and (output-port? object) (textual-port? object))
        (guard (e (#t '())) (list (list 'get-output-string (get-output-string object))))
        '())
    (if (and (output-port? object) (binary-port? object))
        (guard (e (#t '())) (list (list 'get-output-bytevector (get-output-bytevector object))))
        '())))

;;; ---- 0..N -> type indexed-element inlining, shared by pair/string/
;;; vector/bytevector/typed-vector properties above. ----

(define (%indexed-properties lst) (%indexed-pairs lst))

;;; ---- Dispatch ----
;;;
;;; Order matters: hash-table/box/set/bag/char-set/typed-vector must
;;; come before the generic record check so their own tailored property
;;; sets win instead of the generic field-dump, and bag? must be
;;; checked before hash-table? since bags are hash tables under the
;;; hood.

(define (inspect-properties object)
  (append
    (%object-properties object)
    (cond
      ((number? object)  (%number-properties object))
      ((boolean? object) (%boolean-properties object))
      ((box? object)     (%box-properties object))
      ((bag? object)     (%bag-properties object))
      ((hash-table? object) (%hash-table-properties object))
      ((set? object)     (%set-properties object))
      ((char-set? object) (%char-set-properties object))
      ((%typedvec? object) (%typedvec-properties object))
      ((pair? object)    (%pair-properties object))
      ((symbol? object)  (%symbol-properties object))
      ((char? object)    (%character-properties object))
      ((string? object)  (%string-properties object))
      ((vector? object)  (%vector-properties object))
      ((bytevector? object) (%bytevector-properties object))
      ((error-object? object) (%error-properties object))
      ((record-type? object) (%rtd-properties object))
      ((record? object)  (%record-properties object))
      ((procedure? object) (%procedure-properties object))
      ((port? object)      (%port-properties object))
      (else '()))))

(define (inspect-describe object . port-arg)
  (let ((port (if (null? port-arg) (current-output-port) (car port-arg))))
    (write object port)
    (newline port)))
