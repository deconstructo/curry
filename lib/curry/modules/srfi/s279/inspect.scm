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
;;; As of this version: also char-sets (srfi 14), the universal id/size/
;;; type properties (location deliberately omitted -- see %object-id's
;;; own comment in src/builtins.c), symbol-value (a live global-env
;;; lookup, not a module registry), char-name (curry's own 9 R7RS
;;; named-character reader vocabulary), hash-table-equivalence-
;;; function/-hash-function/-weak?/-mutable?, ports (port-open?/
;;; -direction/-type/-line/-position/-file-descriptor, backed by the
;;; primitives added alongside the ports feature), numeric vectors
;;; (u8/s8/u16/s16/u32/s32/u64/s64/f64vector, now that (curry typedvec)
;;; and (curry f64vector) both exist -- see %typedvec-properties below),
;;; and rtd-constructor/-predicate/-accessors/-mutators (the actual
;;; constructor/predicate/accessor/mutator closures define-record-type's
;;; own codegen creates, not just their names -- backed by the new
;;; record-type-constructor/-predicate/-accessors/-mutators primitives,
;;; which needed extending RecordType itself and both define-record-type
;;; codegen paths, eval.c's tree-walker case and compiler.c's native
;;; compile_define_record_type, to stash each binding's closure back
;;; onto the RTD right after it's created; see their own comments in
;;; src/object.h/src/record_type.c/src/builtins.c for the full story).
;;;
;;; Deliberately deferred, not forgotten:
;;;   - library/environment properties: modules.c's registry has no
;;;     Scheme-level enumeration API (module names/exports aren't queryable
;;;     from Scheme once loaded).
;;;   - symbol-library/symbol-exported?: same module-registry gap as
;;;     library/environment properties above -- symbol-value alone
;;;     doesn't need it (a plain global lookup), these do.
;;; A `#f`-valued object-properties entry is never emitted for these —
;;; per the SRFI's own rule, an unsupported property is omitted, not
;;; filled with a dummy value.
;;;
;;; SRFI-279 itself is still a DRAFT (not finalized) as of this writing
;;; -- see docs/reference/srfi/s279.md's own opening note. The property
;;; names/shapes below track the draft as it stood when each piece was
;;; written; if the SRFI changes before finalization, this module's
;;; surface may need to change to match.

(define-library (srfi s279 inspect)
  (import (scheme base) (scheme write) (scheme char)
          (srfi 1) (srfi 14) (srfi 69) (srfi 111) (srfi 113) (srfi 4))
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
    ;; Mirrors inspect-properties' own dispatch cond order exactly (see
    ;; the bottom of this file) -- kept as a separate function rather
    ;; than restructured to share one pass, since %object-properties
    ;; runs before that dispatch and duplicating a stable, rarely-
    ;; changing predicate order is simpler than threading the matched
    ;; branch backward through the call.
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
        (else #f))) ; "whenever inferrable" -- omitted, not guessed, otherwise

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
    ;;; doesn't expose to Scheme (see the header comment) -- symbol->string
    ;;; and symbol-value (a plain global-environment lookup, needing no
    ;;; registry at all) are the two properties here with no such
    ;;; dependency.

    ;; A private, freshly-allocated sentinel (never eq? to any real Scheme
    ;; value, including #f) so "genuinely unbound" is distinguishable from
    ;; "bound, and its value happens to be #f" -- a guard that just caught
    ;; and returned #f on error couldn't tell those apart.
    (define %unbound-sentinel (list 'unbound))

    (define (%symbol-value object)
      (guard (e (#t %unbound-sentinel)) (eval object (interaction-environment))))

    (define (%symbol-properties object)
      (append
        (list (list 'symbol->string (symbol->string object)))
        (let ((v (%symbol-value object)))
          (if (eq? v %unbound-sentinel) '() (list (list 'symbol-value v))))))

    ;;; ---- Character ----

    ;; Reverse lookup against curry's own reader vocabulary (src/reader.c)
    ;; -- exactly the 9 R7RS named characters curry's #\name syntax
    ;; already recognizes, not a full Unicode character-name database
    ;; (SRFI-279's own char-name example, "MALE WITH STROKE...", implies
    ;; Unicode's full NamesList.txt, which curry has no data for at all).
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

    ;;; ---- Typed numeric vector (srfi 4: u8/s8/u16/s16/u32/s32/u64/
    ;;; s64/f64vector) ----
    ;;;
    ;;; No single generic "is this any typed vector" predicate exists
    ;;; across the 9 kinds -- 8 of them share one C heap type internally
    ;;; ((curry typedvec)'s T_TYPEDVEC, distinguished only by a kind flag)
    ;;; but expose only a kind-specific predicate at the Scheme level
    ;;; (u8vector?, s8vector?, ...), and f64vector is a wholly separate
    ;;; heap type from the pre-existing (curry f64vector) module. Each
    ;;; kind's own predicate/length/->list procedures are looked up via
    ;;; this table instead. Property keys are the real per-kind procedure
    ;;; names (e.g. 'u8vector-length, not a generic 'typedvec-length),
    ;;; matching this module's existing convention of naming a property
    ;;; after the actual standard procedure that produced it.
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

    ;; rtd-constructor/-predicate/-accessors/-mutators: the four
    ;; "record-related procedures" the SRFI wants (record-type-* are new
    ;; primitives added alongside this update -- see their own comment
    ;; in src/builtins.c). constructor/predicate are omitted in the
    ;; (should-be-unreachable-in-practice) case they're #f -- every
    ;; define-record-type binding populates both; accessors is always a
    ;; full list of real procedures (every field has one by both R6RS
    ;; and R7RS's own grammar); mutators keeps a #f entry INLINE for
    ;; each field declared immutable rather than omitting the whole
    ;; property or dropping that one element, so a caller can still
    ;; line mutators up against field-names/accessors by position.
    (define (%rtd-properties rtd)
      (append
        (list (list 'rtd-name (record-type-name rtd))
              (list 'rtd-field-names (record-type-field-names rtd)))
        (%omit 'rtd-constructor (record-type-constructor rtd))
        (%omit 'rtd-predicate   (record-type-predicate rtd))
        (list (list 'rtd-accessors (record-type-accessors rtd))
              (list 'rtd-mutators  (record-type-mutators rtd)))))

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
    ;;;
    ;;; SRFI-279 describes hash-table-equivalence-function/-hash-function
    ;;; as "Names of procedures defining hash table behaviors" -- symbols,
    ;;; not the procedure objects themselves. (srfi 69)'s own
    ;;; hash-table-equivalence-function/-hash-function already return the
    ;;; real procedure (equal?/eqv?/eq? and hash/hash-by-identity
    ;;; respectively, tracked per-table via its own side registry -- see
    ;;; its own header comment); procedure-name (added alongside this
    ;;; module's procedure-properties support) turns that into the name
    ;;; this property actually wants. curry's hash tables have no weak-
    ;;; reference variant and are always mutable (no immutable-hash-table
    ;;; concept at all), so those two are fixed constants, not per-object
    ;;; queries.

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

    ;;; ---- Character set (srfi 14) ----

    ;; char-set-name is only meaningful for one of the STANDARD predefined
    ;; sets (char-set:letter and friends) -- SRFI-14 char-sets built by
    ;; the user (via char-set/list->char-set/union/etc) are structurally
    ;; anonymous, so this is a reverse lookup against that fixed list via
    ;; char-set=, not a stored name. char-set:title-case is deliberately
    ;; NOT its own entry here: curry's own (srfi s14 char-sets) has no
    ;; titlecase table at all, so it's permanently empty and structurally
    ;; identical to char-set:empty -- char-set= can't tell them apart, so
    ;; char-set:title-case (and any other genuinely empty char-set a user
    ;; happens to build) honestly reports char-set-name 'char-set:empty,
    ;; the best answer actually available, rather than being given its
    ;; own separate (and unverifiable) name.
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

    ;;; ---- Port ----
    ;;;
    ;;; port-open?/-direction/-type/-line/-position/-file-descriptor are
    ;;; backed by new textual-port?/binary-port?/port-line/-position/
    ;;; -file-descriptor primitives added alongside this module (see
    ;;; their own comments in src/builtins.c for exactly what each of
    ;;; curry's two port representations, FILE*-backed and string/
    ;;; bytevector-backed, can and can't report). port-file is omitted
    ;;; entirely: curry's Port struct never stores the path a file port
    ;;; was opened from (see src/object.h), so there's genuinely nothing
    ;;; to report, not something merely unwired. port-column is
    ;;; similarly omitted: curry tracks line but never column at all.
    ;;; port-encoding is a fixed 'UTF-8 for textual ports (curry really
    ;;; is UTF-8-only throughout, per port.h's own header comment --
    ;;; not a guess), omitted for binary ports (encoding doesn't apply).

    (define (%port-open? object)
      (or (and (input-port? object) (input-port-open? object))
          (and (output-port? object) (output-port-open? object))))

    (define (%port-direction object)
      (cond ((and (input-port? object) (output-port? object)) 'both)
            ((input-port? object) 'input)
            ((output-port? object) 'output)
            (else #f))) ; unreachable in practice -- every real port is at least one

    ;; get-output-string/get-output-bytevector are meant for STRING/
    ;; BYTEVECTOR OUTPUT ports specifically (SRFI-279's own wording:
    ;; "for string and bytevector output ports") -- but curry's own
    ;; get-output-string/-bytevector only check the PORT_STRING flag,
    ;; not direction, so calling either on an INPUT string port doesn't
    ;; raise at all, it just returns the input content (confirmed live:
    ;; (get-output-string (open-input-string "hello")) => "hello", no
    ;; error). Relying on a guard here would silently expose these
    ;; properties on input ports too, contradicting the SRFI's own
    ;; wording -- output-port? is checked explicitly instead of trusting
    ;; curry's own exception behavior to enforce the boundary.
    ;; textual-port?/binary-port? distinguish which of the two accessors
    ;; actually applies -- both flags share PORT_STRING internally, so
    ;; trying the wrong one isn't otherwise self-evident.
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

  )) ;; end begin, define-library
