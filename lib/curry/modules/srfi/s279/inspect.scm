;;; SRFI-279: In(tro)spection Protocol — curry-native implementation.
;;;
;;; https://srfi.schemers.org/srfi-279/ (draft #1, 2026-08-11) specifies
;;; exactly two procedures: inspect-properties (structured alist of an
;;; object's introspectable data) and inspect-describe (a human-readable
;;; one-shot summary). This is a from-scratch implementation written
;;; directly against curry's own primitives, not a port of the SRFI's
;;; reference `generic.scm` -- that file leans on SRFI 14 (char-sets),
;;; SRFI 26 (cut), SRFI 160 (numeric vectors), and SRFI 253 (checked
;;; lambdas), none of which curry has, so porting it would mean building
;;; four other SRFIs first just to get this one working.
;;;
;;; Scope: this covers the property groups curry can back with real
;;; introspection today -- object, number, boolean, pair, string, vector,
;;; bytevector, a useful subset of character and symbol, record (backed
;;; by the new record?/record-rtd/record-type-name/record-type-field-names
;;; primitives added alongside this module -- curry previously had no way
;;; to ask "is this any record" without already knowing its RTD), error
;;; objects, hash tables (srfi 69), boxes (srfi 111), sets/bags (srfi 113),
;;; and procedures (backed by the procedure-name/-arity/-arglist/-file/
;;; -line/-lambda/-closure primitives added alongside this module -- see
;;; their own header comment in src/builtins.c for exactly what each of
;;; curry's three procedure representations, tree-walker closures,
;;; bytecode-VM closures, and C primitives, can and can't report).
;;; Deliberately deferred, not forgotten:
;;;   - char-set properties: curry now HAS SRFI-14 (as of a later commit
;;;     than this module's original version), but char-sets aren't wired
;;;     into inspect-properties yet -- an easy, self-contained follow-up.
;;;   - numeric-vector (s8vector etc.) properties: curry has no SRFI 4/160.
;;;   - library/environment properties: modules.c's registry has no
;;;     Scheme-level enumeration API (module names/exports aren't queryable
;;;     from Scheme once loaded).
;;;   - ports: no port-open?/-direction/-type/etc case at all yet.
;;; A `#f`-valued object-properties entry is never emitted for these —
;;; per the SRFI's own rule, an unsupported property is omitted, not
;;; filled with a dummy value.

(define-library (srfi s279 inspect)
  (import (scheme base) (scheme write) (scheme char)
          (srfi 1) (srfi 69) (srfi 111) (srfi 113))
  (export inspect-properties inspect-describe)
  (begin

    ;; curry has no string->vector/vector->string (an R7RS gap in its
    ;; own right, out of scope here) -- small local fallbacks via the
    ;; list conversions curry does have, good enough for property display.
    (define (%string->vector s) (list->vector (string->list s)))
    (define (%vector->string v) (list->string (vector->list v)))

    (define (%to-string-with object proc)
      (call-with-port (open-output-string)
        (lambda (p) (proc object p) (get-output-string p))))

    ;; KNOWN LIMITATION (curry core, not this module): curry's write/
    ;; display have no cycle detection (no #n=/#n# datum-labeling) --
    ;; confirmed independently, `(write x)` on a circular pair alone
    ;; hangs forever. Since this SRFI's spec calls for the 'write/'display
    ;; entries to be exactly the standard procedures' own output,
    ;; inspect-properties on a circular object will hang here rather than
    ;; silently working around a core gap that's out of this module's
    ;; scope to fix. Everything else in inspect-properties (car/cdr, the
    ;; rest of pair-properties) stays cycle-safe via `list?`'s own R7RS-
    ;; mandated cycle detection -- only this write/display step is at risk.
    (define (%object-properties object)
      (list (list 'write   (%to-string-with object write))
            (list 'display (%to-string-with object display))))

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
    ;;; doesn't expose to Scheme (see the header comment) -- symbol->string
    ;;; is the one property here with no such dependency.

    (define (%symbol-properties object)
      (list (list 'symbol->string (symbol->string object))))

    ;;; ---- Character ----

    (define (%character-properties object)
      (append
        (list (list 'char->integer (char->integer object)))
        (if (char-numeric? object)
            (let ((d (digit-value object)))
              (if d (list (list 'digit-value d)) '()))
            '())
        (list (list 'char-alphabetic? (char-alphabetic? object))
              (list 'char-numeric?    (char-numeric? object))
              (list 'char-whitespace? (char-whitespace? object))
              (list 'char-upper-case? (char-upper-case? object))
              (list 'char-lower-case? (char-lower-case? object)))))

    ;;; ---- String ----

    (define (%string-properties object)
      (append
        (list (list 'string->symbol (string->symbol object))
              (list 'string->list   (string->list object))
              (list 'string->vector (%string->vector object))
              (list 'string->utf8   (string->utf8 object))
              (list 'string-length  (string-length object)))
        ;; (string->number "") incorrectly returns 0 rather than #f in
        ;; this build (a core numeric-parser bug, out of scope to fix
        ;; here) -- guard the empty string explicitly so that bug doesn't
        ;; leak a bogus string->number entry into every empty string's
        ;; properties.
        (if (> (string-length object) 0)
            (let ((n (string->number object)))
              (if n (list (list 'string->number n)) '()))
            '())
        (%indexed-properties (string->list object))))

    ;;; ---- Vector ----

    (define (%vector-properties object)
      (append
        (list (list 'vector-length (vector-length object))
              (list 'vector->list (vector->list object)))
        (if (every char? (vector->list object))
            (list (list 'vector->string (%vector->string object)))
            '())
        (%indexed-properties (vector->list object))))

    ;;; ---- Bytevector ----

    ;; No `guard` here: curry's utf8->string does a raw copy with no
    ;; validation (never raises on invalid UTF-8, per its own comment in
    ;; builtins.c), so it always succeeds -- for a bytevector that isn't
    ;; valid UTF-8, this entry is present but its content is garbled
    ;; rather than a real decode, which callers should be aware of.
    (define (%bytevector-properties object)
      (append
        (list (list 'utf8->string (utf8->string object)))
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

    ;; %record-ref isn't exported by (scheme base) -- it's an internal
    ;; primitive define-record-type's own codegen uses (record_type.c) --
    ;; but it's an ordinary discoverable global like the rest of that
    ;; family (see builtins.c's own comment on why), so it's usable here
    ;; the same way.
    (define (%record-field-ref object i) (%record-ref object i))

    (define (%rtd-properties rtd)
      (list (list 'rtd-name (record-type-name rtd))
            (list 'rtd-field-names (record-type-field-names rtd))))

    ;; field… → object: indexed by field name, in field-definition order.
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

    (define (%hash-table-properties object)
      (append
        (list (list 'hash-table-size (hash-table-size object)))
        (map (lambda (kv) (list (car kv) (cdr kv))) (hash-table->alist object))))

    ;;; ---- Box (srfi 111) ----

    (define (%box-properties object)
      (list (list 'unbox (unbox object))))

    ;;; ---- Set / bag (srfi 113) ----

    (define (%set-properties object)
      (append
        (list (list 'set-size (set-size object)))
        (map (lambda (e) (list e e)) (set->list object))))

    ;; The SRFI's own suggested shape is a single bag->set entry,
    ;; delegating everything else to set-properties -- but curry's (srfi
    ;; s113 sets-and-bags) has no bag->set conversion to delegate to, so
    ;; this reports bag-specific sizes and element→count pairs directly
    ;; instead (bag->alist already gives (element . count), which is more
    ;; informative than a set would be for something that's fundamentally
    ;; a multiset).
    (define (%bag-properties object)
      (append
        (list (list 'bag-size (bag-size object))
              (list 'bag-unique-size (bag-unique-size object)))
        (map (lambda (kv) (list (car kv) (cdr kv))) (bag->alist object))))

    ;;; ---- Procedure ----
    ;;;
    ;;; Each accessor already returns #f (or, for procedure-closure, '())
    ;;; when curry has nothing to report for that procedure representation
    ;;; (e.g. primitives have no source file/line/arglist/closure; the
    ;;; tree-walker has no source location at all) -- %omit below turns
    ;;; that into "key absent" rather than "key present with a dummy #f",
    ;;; matching the SRFI's own rule for this exact situation.
    ;;;
    ;;; Known ambiguity (flagged by independent code review, accepted
    ;;; rather than fixed): procedure-closure's '() means both "this
    ;;; procedure genuinely captured nothing" (e.g. a top-level define)
    ;;; and "this representation can't report captures at all" (every
    ;;; primitive). Both collapse to an absent procedure-closure key
    ;;; here, so a caller can't tell "verified empty" from "unknown" --
    ;;; the same shape of collapse the SRFI's own omission rule already
    ;;; accepts elsewhere (e.g. a #f digit-value is indistinguishable
    ;;; from "not a digit" in %character-properties above).

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

    ;;; ---- 0..N → type indexed-element inlining, shared by pair/string/
    ;;; vector properties above. ----

    (define (%indexed-properties lst)
      (let loop ((lst lst) (i 0) (acc '()))
        (if (null? lst)
            (reverse acc)
            (loop (cdr lst) (+ i 1) (cons (list i (car lst)) acc)))))

    ;;; ---- Dispatch ----
    ;;;
    ;;; Order matters: record must come before pair (a record is never a
    ;;; pair here, but keeping the check-order documented avoids surprise
    ;;; if that ever changes), and hash-table/box/set/bag — all srfi
    ;;; record types under the hood — must come before the generic record
    ;;; check so their own tailored property sets win instead of the
    ;;; generic field-dump.

    (define (inspect-properties object)
      (append
        (%object-properties object)
        (cond
          ((number? object)  (%number-properties object))
          ((boolean? object) (%boolean-properties object))
          ((box? object)     (%box-properties object))
          ;; bag? must be checked before hash-table? -- (srfi s113
          ;; sets-and-bags)'s bags are hash tables under the hood
          ;; (bag? is literally (and (hash-table? obj) ...)), so a bag
          ;; would otherwise get the generic hash-table property set
          ;; instead of its own.
          ((bag? object)     (%bag-properties object))
          ((hash-table? object) (%hash-table-properties object))
          ((set? object)     (%set-properties object))
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
          (else '()))))

    (define (inspect-describe object . port-arg)
      (let ((port (if (null? port-arg) (current-output-port) (car port-arg))))
        (write object port)
        (newline port)))

  )) ;; end begin, define-library
