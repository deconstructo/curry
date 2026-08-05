;;; (curry toml) — TOML 1.0 reader and writer, pure Scheme.
;;;
;;; Mirrors (curry yaml)'s shape deliberately: tables become association
;;; lists (string keys, not forced/coerced the way JSON keys sometimes are),
;;; arrays become lists, and — since TOML datetimes are unquoted literals
;;; distinct from strings (a bare `1979-05-27` is not the same value as the
;;; quoted string `"1979-05-27"`) — datetimes get the same "distinguished
;;; sentinel" treatment (curry yaml)'s `yaml-null` uses for YAML's null:
;;; a `toml-datetime` record (tested with `toml-datetime?`), rather than
;;; collapsing them into plain strings and losing the distinction on write.
;;;
;;; Supported: key/value pairs (bare, quoted, and dotted keys), standard
;;; tables `[table]` and dotted-path tables `[a.b.c]`, arrays of tables
;;; `[[table]]`, inline tables `{ k = v, ... }`, arrays (including
;;; multi-line and heterogeneous, per TOML 1.0), all four string forms
;;; (basic `"..."`, literal `'...'`, multi-line basic `"""..."""` with
;;; line-continuation backslashes, multi-line literal `'''...'''`),
;;; integers (decimal/hex/octal/binary, underscores, sign), floats
;;; (exponents, `inf`/`nan` with sign, underscores), booleans, and all
;;; four RFC-3339-ish datetime forms (offset date-time, local date-time,
;;; local date, local time).
;;;
;;; Deliberately not supported (see docs/reference/module-toml.md):
;;;
;;; - Duplicate-key/redefinition errors. The TOML spec requires rejecting a
;;;   key defined twice (including via table headers reopening a key
;;;   already set as a non-table value); this module simply overwrites,
;;;   the same pragmatic simplification (curry yaml) makes for YAML merge
;;;   keys re-defining a key from more than one source.
;;; - Strict inline-table immutability-after-the-fact and strict
;;;   no-trailing-comma-in-inline-tables enforcement — both are accepted
;;;   leniently rather than rejected.
;;; - Full Unicode-escape validation in basic strings beyond `\uXXXX`/
;;;   `\UXXXXXXXX` decoding into the corresponding character.

(define-library (curry toml)
  (import (scheme base))
  (export
    toml-datetime toml-datetime? toml-datetime->string
    toml-parse toml-read toml-load-file
    toml-stringify toml-write toml-dump-file)
  (begin

;;; =========================================================================
;;; The datetime sentinel
;;; =========================================================================

;; Wraps the raw literal text (e.g. "1979-05-27T07:32:00Z") rather than
;; decomposing it into fields — TOML datetimes are written back out exactly
;; as parsed, and this module doesn't do date arithmetic, so there's
;; nothing to gain from a fielded representation and much to lose (four
;; distinct RFC-3339-ish shapes would need four distinct field sets).
(define-record-type <toml-datetime>
  (toml-datetime text)
  toml-datetime?
  (text toml-datetime->string))

;;; =========================================================================
;;; Small local helpers (kept independent of (srfi s1 lists))
;;; =========================================================================

(define (%string-trim-right s)
  (let loop ((i (string-length s)))
    (if (and (> i 0) (memv (string-ref s (- i 1)) (list #\space #\tab)))
        (loop (- i 1))
        (substring s 0 i))))

(define (%string-index s ch start)
  (let ((n (string-length s)))
    (let loop ((i start))
      (cond ((>= i n) #f)
            ((char=? (string-ref s i) ch) i)
            (else (loop (+ i 1)))))))

(define (%last-pair lst) (if (null? (cdr lst)) lst (%last-pair (cdr lst))))

;;; =========================================================================
;;; Cursor: a mutable (string pos len) triple, same imperative-recursive-
;;; descent convention (curry yaml) uses and for the same reason — TOML is
;;; line/position-sensitive text that a hand-written parser wants to walk
;;; with a mutable position, not thread functionally through every helper.
;;; =========================================================================

(define (%mk-cursor s) (vector s 0 (string-length s)))
(define (%c-str c) (vector-ref c 0))
(define (%c-pos c) (vector-ref c 1))
(define (%c-len c) (vector-ref c 2))
(define (%c-set! c p) (vector-set! c 1 p))
(define (%c-eof? c) (>= (%c-pos c) (%c-len c)))
(define (%c-ch c) (if (%c-eof? c) #f (string-ref (%c-str c) (%c-pos c))))
(define (%c-ch-at c k)
  (let ((p (+ (%c-pos c) k)))
    (if (or (< p 0) (>= p (%c-len c))) #f (string-ref (%c-str c) p))))
(define (%c-adv! c) (%c-set! c (+ 1 (%c-pos c))))
(define (%c-adv-n! c n) (%c-set! c (+ n (%c-pos c))))

(define (%c-looking-at? c lit)
  (let ((n (string-length lit)))
    (let loop ((i 0))
      (cond ((= i n) #t)
            ((not (eqv? (%c-ch-at c i) (string-ref lit i))) #f)
            (else (loop (+ i 1)))))))

(define (%toml-error msg . irritants) (apply error (string-append "toml: " msg) irritants))

;; Whitespace WITHIN a line only (space/tab) — TOML lines are significant,
;; unlike YAML's flow contexts, so this never crosses a newline.
(define (%skip-line-ws! c)
  (let loop ()
    (when (and (not (%c-eof? c)) (memv (%c-ch c) (list #\space #\tab))) (%c-adv! c) (loop))))

(define (%c-skip-line! c)
  (let loop ()
    (cond ((%c-eof? c) #t)
          ((char=? (%c-ch c) #\newline) (%c-adv! c))
          (else (%c-adv! c) (loop)))))

;; Advance past blank lines, whitespace, comment-only lines, and CRLF/CR
;; line endings (normalized away, never surfacing in parsed content),
;; leaving the cursor at the first content character of a line, at a "["
;; table header, or at EOF.
(define (%skip-to-content! c)
  (let loop ()
    (%skip-line-ws! c)
    (cond
      ((%c-eof? c) #f)
      ((char=? (%c-ch c) #\newline) (%c-adv! c) (loop))
      ((and (char=? (%c-ch c) #\return) (eqv? (%c-ch-at c 1) #\newline)) (%c-adv-n! c 2) (loop))
      ((char=? (%c-ch c) #\#) (%c-skip-line! c) (loop))
      (else #t))))

;; Whitespace/comments inside a flow context (array, inline table) — here
;; newlines ARE just whitespace (TOML 1.0 allows multi-line arrays, though
;; not multi-line inline tables; this module is lenient about the latter).
(define (%skip-flow-ws! c)
  (let loop ()
    (cond
      ((%c-eof? c) #f)
      ((memv (%c-ch c) (list #\space #\tab #\newline)) (%c-adv! c) (loop))
      ((and (char=? (%c-ch c) #\return) (eqv? (%c-ch-at c 1) #\newline)) (%c-adv-n! c 2) (loop))
      ((char=? (%c-ch c) #\#) (%c-skip-line! c) (loop))
      (else #t))))

;;; =========================================================================
;;; Keys
;;; =========================================================================

(define (%bare-key-char? ch)
  (and ch (or (char-alphabetic? ch) (char-numeric? ch) (char=? ch #\-) (char=? ch #\_))))

(define (%parse-bare-key! c)
  (let ((start (%c-pos c)))
    (let loop () (when (%bare-key-char? (%c-ch c)) (%c-adv! c) (loop)))
    (if (= start (%c-pos c))
        (%toml-error "expected a key" (%c-pos c))
        (substring (%c-str c) start (%c-pos c)))))

;; A single (undotted) key segment: bare, basic-quoted, or literal-quoted.
(define (%parse-simple-key! c)
  (cond
    ((char=? (%c-ch c) #\") (%parse-basic-string! c #f))
    ((char=? (%c-ch c) #\') (%parse-literal-string! c #f))
    (else (%parse-bare-key! c))))

;; Returns a list of key segments — a single segment for an undotted key.
(define (%parse-key-path! c)
  (let loop ((segs (list (%parse-simple-key! c))))
    (%skip-line-ws! c)
    (if (eqv? (%c-ch c) #\.)
        (begin (%c-adv! c) (%skip-line-ws! c) (loop (cons (%parse-simple-key! c) segs)))
        (reverse segs))))

;;; =========================================================================
;;; Strings
;;; =========================================================================

(define (%hex-digit->int ch)
  (cond ((and (char>=? ch #\0) (char<=? ch #\9)) (- (char->integer ch) (char->integer #\0)))
        ((and (char>=? ch #\a) (char<=? ch #\f)) (+ 10 (- (char->integer ch) (char->integer #\a))))
        ((and (char>=? ch #\A) (char<=? ch #\F)) (+ 10 (- (char->integer ch) (char->integer #\A))))
        (else (%toml-error "invalid hex digit" ch))))

(define (%read-hex-escape! c n)
  (let loop ((i 0) (acc 0))
    (if (= i n)
        acc
        (let ((ch (%c-ch c)))
          (%c-adv! c)
          (loop (+ i 1) (+ (* acc 16) (%hex-digit->int ch)))))))

;; Basic (double-quoted) string. `multiline?` selects `"""..."""` framing
;; (a leading newline immediately after the opening delimiter is trimmed;
;; a trailing backslash-newline is a line-continuation that swallows all
;; following leading whitespace, TOML's "line ending backslash" escape).
(define (%parse-basic-string! c multiline?)
  (%c-adv-n! c (if multiline? 3 1))
  (when (and multiline? (eqv? (%c-ch c) #\newline)) (%c-adv! c))
  (let ((out (open-output-string)))
    (let loop ()
      (cond
        ((%c-eof? c) (%toml-error "unterminated string"))
        ((and multiline? (%c-looking-at? c "\"\"\"")) (%c-adv-n! c 3))
        ((and (not multiline?) (char=? (%c-ch c) #\")) (%c-adv! c))
        ((char=? (%c-ch c) #\\)
         (%c-adv! c)
         (cond
           ((and multiline? (memv (%c-ch c) (list #\newline #\space #\tab #\return)))
            ;; line-ending backslash: skip all whitespace/newlines up to
            ;; and including the next non-whitespace content
            (let skip () (when (memv (%c-ch c) (list #\newline #\space #\tab #\return)) (%c-adv! c) (skip))))
           (else
            (let ((e (%c-ch c)))
              (%c-adv! c)
              (case e
                ((#\n) (write-char #\newline out))
                ((#\t) (write-char #\tab out))
                ((#\r) (write-char #\return out))
                ((#\") (write-char #\" out))
                ((#\\) (write-char #\\ out))
                ((#\b) (write-char #\backspace out))
                ((#\f) (write-char (integer->char 12) out))
                ((#\u) (write-char (integer->char (%read-hex-escape! c 4)) out))
                ((#\U) (write-char (integer->char (%read-hex-escape! c 8)) out))
                (else (%toml-error "unknown string escape" e))))))
         (loop))
        (else (write-char (%c-ch c) out) (%c-adv! c) (loop))))
    (get-output-string out)))

;; Literal (single-quoted) string: no escapes at all. `'''...'''` for the
;; multi-line form (a leading newline right after the delimiter is trimmed).
(define (%parse-literal-string! c multiline?)
  (%c-adv-n! c (if multiline? 3 1))
  (when (and multiline? (eqv? (%c-ch c) #\newline)) (%c-adv! c))
  (let ((start (%c-pos c)))
    (let loop ()
      (cond
        ((%c-eof? c) (%toml-error "unterminated literal string"))
        ((and multiline? (%c-looking-at? c "'''"))
         (let ((s (substring (%c-str c) start (%c-pos c)))) (%c-adv-n! c 3) s))
        ((and (not multiline?) (char=? (%c-ch c) #\'))
         (let ((s (substring (%c-str c) start (%c-pos c)))) (%c-adv! c) s))
        (else (%c-adv! c) (loop))))))

(define (%parse-string-value! c)
  (cond
    ((%c-looking-at? c "\"\"\"") (%parse-basic-string! c #t))
    ((char=? (%c-ch c) #\") (%parse-basic-string! c #f))
    ((%c-looking-at? c "'''") (%parse-literal-string! c #t))
    (else (%parse-literal-string! c #f))))

;;; =========================================================================
;;; Numbers, booleans, datetimes
;;; =========================================================================

(define (%value-terminator? ch)
  (or (not ch) (memv ch (list #\, #\] #\} #\newline #\# #\space #\tab #\return))))

(define (%scan-bareword! c)
  (let ((start (%c-pos c)))
    (let loop () (unless (%value-terminator? (%c-ch c)) (%c-adv! c) (loop)))
    (substring (%c-str c) start (%c-pos c))))

(define (%strip-underscores s)
  (let ((out (open-output-string)))
    (string-for-each (lambda (ch) (unless (char=? ch #\_) (write-char ch out))) s)
    (get-output-string out)))

(define (%digit-str? s from to)
  (let loop ((i from))
    (or (= i to)
        (and (< i (string-length s)) (char-numeric? (string-ref s i)) (loop (+ i 1))))))

;; A TOML datetime always starts "DDDD-DD-DD" (a date) or "DD:DD:DD" (a
;; bare local time) — check that shape cheaply before committing to it,
;; falling back to numeric parsing otherwise (a bare integer/float never
;; has a '-' or ':' in those positions).
(define (%looks-like-date-or-time? s)
  (let ((n (string-length s)))
    (or (and (>= n 10) (char=? (string-ref s 4) #\-) (char=? (string-ref s 7) #\-) (%digit-str? s 0 4))
        (and (>= n 8) (char=? (string-ref s 2) #\:) (char=? (string-ref s 5) #\:) (%digit-str? s 0 2)))))

(define (%parse-number-token s)
  (cond
    ((or (string=? s "inf") (string=? s "+inf")) +inf.0)
    ((string=? s "-inf") -inf.0)
    ((or (string=? s "nan") (string=? s "+nan") (string=? s "-nan")) +nan.0)
    ((and (>= (string-length s) 2) (char=? (string-ref s 0) #\0) (char=? (string-ref s 1) #\x))
     (or (string->number (%strip-underscores (substring s 2 (string-length s))) 16) (%toml-error "bad hex integer" s)))
    ((and (>= (string-length s) 2) (char=? (string-ref s 0) #\0) (char=? (string-ref s 1) #\o))
     (or (string->number (%strip-underscores (substring s 2 (string-length s))) 8) (%toml-error "bad octal integer" s)))
    ((and (>= (string-length s) 2) (char=? (string-ref s 0) #\0) (char=? (string-ref s 1) #\b))
     (or (string->number (%strip-underscores (substring s 2 (string-length s))) 2) (%toml-error "bad binary integer" s)))
    (else (or (string->number (%strip-underscores s)) (%toml-error "invalid value" s)))))

;; Values are parsed as opaque tokens up to the next terminator (a comma,
;; closing bracket/brace, comment, or line end) and then classified —
;; simplest correct way to distinguish "1979-05-27" (a datetime) from
;; "19790527" (an integer) and from "1e5" (a float) without a much larger
;; hand-rolled grammar, since none of those tokens can themselves contain
;; a terminator character.
(define (%parse-atom-value! c)
  (let ((tok (%scan-bareword! c)))
    (cond
      ((string=? tok "true") #t)
      ((string=? tok "false") #f)
      ((%looks-like-date-or-time? tok) (toml-datetime tok))
      (else (%parse-number-token tok)))))

;;; =========================================================================
;;; Values (strings, atoms, arrays, inline tables)
;;; =========================================================================

(define (%parse-value! c)
  (cond
    ((memv (%c-ch c) (list #\" #\')) (%parse-string-value! c))
    ((char=? (%c-ch c) #\[) (%parse-array! c))
    ((char=? (%c-ch c) #\{) (%parse-inline-table! c))
    (else (%parse-atom-value! c))))

(define (%parse-array! c)
  (%c-adv! c) ; [
  (%skip-flow-ws! c)
  (let loop ((acc '()))
    (if (char=? (%c-ch c) #\])
        (begin (%c-adv! c) (reverse acc))
        (let ((v (%parse-value! c)))
          (%skip-flow-ws! c)
          (cond
            ((char=? (%c-ch c) #\,) (%c-adv! c) (%skip-flow-ws! c) (loop (cons v acc)))
            ((char=? (%c-ch c) #\]) (%c-adv! c) (reverse (cons v acc)))
            (else (%toml-error "expected , or ] in array" (%c-ch c))))))))

;; Inline tables are single-line per the spec; leniently tolerated across
;; lines here too (see module header) rather than rejected.
(define (%parse-inline-table! c)
  (%c-adv! c) ; {
  (%skip-flow-ws! c)
  (let ((tbl (%make-table)))
    (let loop ()
      (if (char=? (%c-ch c) #\})
          (begin (%c-adv! c) (%table->value tbl))
          (let* ((segs (%parse-key-path! c)))
            (%skip-flow-ws! c)
            (unless (char=? (%c-ch c) #\=) (%toml-error "expected = in inline table"))
            (%c-adv! c) (%skip-flow-ws! c)
            (let ((v (%parse-value! c)))
              (%insert-dotted! tbl segs v)
              (%skip-flow-ws! c)
              (cond
                ((char=? (%c-ch c) #\,) (%c-adv! c) (%skip-flow-ws! c) (loop))
                ((char=? (%c-ch c) #\}) (%c-adv! c) (%table->value tbl))
                (else (%toml-error "expected , or } in inline table" (%c-ch c))))))))))

;;; =========================================================================
;;; Mutable tables (parse-time only) and dotted-key/table-header navigation
;;; =========================================================================

;; A parser-time table is a 1-element vector wrapping an insertion-ordered
;; alist of (key . value) pairs. Lookup/update walk the list (assoc,
;; set-cdr! on the found pair) rather than using a hash table — realistic
;; config files are small, and this keeps insertion order for free, which
;; the writer wants back for round-tripping. Values held in a table during
;; parsing are one of: a scalar/string/datetime/list(array), another
;; %make-table (a sub-table), or a list of %make-table (an array of
;; tables, from `[[...]]` headers).
(define (%make-table) (vector '()))
(define (%table? v) (and (vector? v) (= (vector-length v) 1) (list? (vector-ref v 0))))
(define (%table-alist t) (vector-ref t 0))
(define (%table-ref t key) (let ((p (assoc key (%table-alist t)))) (and p (cdr p))))
(define (%table-has? t key) (if (assoc key (%table-alist t)) #t #f))

(define (%table-set! t key val)
  (let ((p (assoc key (%table-alist t))))
    (if p
        (set-cdr! p val)
        (vector-set! t 0 (append (%table-alist t) (list (cons key val)))))))

;; True iff `v` is a list of sub-tables (i.e. what a `[[...]]` array-of-
;; tables header produces) — distinguished from an ordinary array (which
;; may hold anything, including plain values) by checking its elements.
(define (%table-list? v) (and (list? v) (pair? v) (%table? (car v))))

;; Navigates `segments` from `root`, creating standard sub-tables along
;; the way (or descending into the LAST element of an array-of-tables, if
;; a segment names one — how `[[fruits]]` followed by `[fruits.physical]`
;; means "physical" belongs to the most-recently-appended fruit). For the
;; final segment: if `create-array?`, ensures it's a list of tables and
;; appends (returns) a fresh one; otherwise ensures it's a single table
;; (returns the existing one, or creates it).
(define (%navigate-table! root segments create-array?)
  (let loop ((tbl root) (segs segments))
    (let ((key (car segs)) (rest (cdr segs)))
      (if (null? rest)
          (let ((existing (%table-ref tbl key)))
            (if create-array?
                (let ((new-t (%make-table)))
                  (if (%table-list? existing)
                      (%table-set! tbl key (append existing (list new-t)))
                      (%table-set! tbl key (list new-t)))
                  new-t)
                (cond
                  ((and existing (%table? existing)) existing)
                  ((not existing) (let ((new-t (%make-table))) (%table-set! tbl key new-t) new-t))
                  (else (%toml-error "key already defined as a non-table value" key)))))
          (let ((existing (%table-ref tbl key)))
            (cond
              ((and existing (%table? existing)) (loop existing rest))
              ((%table-list? existing) (loop (car (%last-pair existing)) rest))
              ((not existing) (let ((new-t (%make-table))) (%table-set! tbl key new-t) (loop new-t rest)))
              (else (%toml-error "key already defined as a non-table value" key))))))))

;; Inserts a (possibly dotted) key = value pair. Unlike table headers,
;; dotted keys in a key/value line never create or extend arrays-of-
;; tables — only plain intermediate sub-tables.
(define (%insert-dotted! tbl segments value)
  (if (null? (cdr segments))
      (%table-set! tbl (car segments) value)
      (%navigate-table-plain! tbl segments value)))

(define (%navigate-table-plain! root segments value)
  (let loop ((tbl root) (segs segments))
    (let ((key (car segs)) (rest (cdr segs)))
      (if (null? (cdr rest))
          (let ((sub (%get-or-make-subtable! tbl key)))
            (%table-set! sub (car rest) value))
          (loop (%get-or-make-subtable! tbl key) rest)))))

(define (%get-or-make-subtable! tbl key)
  (let ((existing (%table-ref tbl key)))
    (cond
      ((and existing (%table? existing)) existing)
      ((%table-list? existing) (car (%last-pair existing)))
      ((not existing) (let ((new-t (%make-table))) (%table-set! tbl key new-t) new-t))
      (else (%toml-error "key already defined as a non-table value" key)))))

;; Converts the mutable parse-time table structure into the final
;; immutable value: an association list, recursively converting any
;; sub-table or array-of-tables values the same way. Plain arrays are
;; left as-is (already ordinary lists of already-converted values).
(define (%table->value t)
  (map (lambda (kv) (cons (car kv) (%deep-convert (cdr kv)))) (%table-alist t)))

(define (%deep-convert v)
  (cond
    ((%table? v) (%table->value v))
    ((%table-list? v) (map %table->value v))
    ((and (list? v) (pair? v)) (map %deep-convert v))
    (else v)))

;;; =========================================================================
;;; Document driver
;;; =========================================================================

(define (%parse-table-header! c root)
  ;; cursor is at "[" ; either "[[" array-of-tables or "[" std table
  (%c-adv! c)
  (let ((array? (eqv? (%c-ch c) #\[)))
    (when array? (%c-adv! c))
    (%skip-line-ws! c)
    (let ((segs (%parse-key-path! c)))
      (%skip-line-ws! c)
      (unless (char=? (%c-ch c) #\]) (%toml-error "expected ] closing table header"))
      (%c-adv! c)
      (when array?
        (unless (char=? (%c-ch c) #\]) (%toml-error "expected ]] closing array-of-tables header"))
        (%c-adv! c))
      (%navigate-table! root segs array?))))

(define (%parse-key-value-line! c current)
  (let ((segs (%parse-key-path! c)))
    (%skip-line-ws! c)
    (unless (char=? (%c-ch c) #\=) (%toml-error "expected = after key" segs))
    (%c-adv! c) (%skip-line-ws! c)
    (%insert-dotted! current segs (%parse-value! c))))

(define (%toml-parse-into str)
  (let ((c (%mk-cursor str)) (root (%make-table)))
    (let loop ((current root))
      (%skip-to-content! c)
      (if (%c-eof? c)
          root
          (let ((next (if (eqv? (%c-ch c) #\[) (%parse-table-header! c root) (begin (%parse-key-value-line! c current) current))))
            (%skip-line-ws! c)
            (loop next))))))

;;; =========================================================================
;;; Reading
;;; =========================================================================

(define (toml-parse str) (%table->value (%toml-parse-into str)))

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

;; (toml-read port) -> value — parses a whole port's content as TOML. TOML's
;; structure (nested tables/arrays, multi-line strings) means the full
;; document has to be in hand before parsing can even start, so this is
;; "slurp the port to a string, then toml-parse it" rather than genuine
;; incremental/streaming parsing — still useful as the port-native entry
;; point `toml-load-file` and any other port source (stdin, a socket, an
;; in-memory pipe) build on, instead of each needing its own
;; read-the-whole-thing-first boilerplate.
(define (toml-read port) (toml-parse (%port->string port)))

(define (toml-load-file path)
  (call-with-input-file path toml-read))

;;; =========================================================================
;;; Writing
;;; =========================================================================

;; A value is a "table" (for writer purposes) if it's a non-empty
;; association list whose keys are strings — same shape check (curry
;; yaml)'s %alist? does, specialized to TOML's string-only keys.
(define (%value-table? v)
  (and (pair? v)
       (let loop ((l v))
         (cond ((null? l) #t)
               ((not (pair? l)) #f)
               ((not (pair? (car l))) #f)
               ((not (string? (caar l))) #f)
               (else (loop (cdr l)))))))

(define (%value-table-list? v) (and (list? v) (pair? v) (every-table? v)))
(define (every-table? lst) (or (null? lst) (and (%value-table? (car lst)) (every-table? (cdr lst)))))

(define (%bare-key-safe? s)
  (and (> (string-length s) 0)
       (let loop ((i 0))
         (or (= i (string-length s))
             (and (%bare-key-char? (string-ref s i)) (loop (+ i 1)))))))

(define (%write-key s out)
  (if (%bare-key-safe? s) (write-string s out) (%write-basic-string s out)))

(define (%write-key-path segs out)
  (let loop ((segs segs) (first #t))
    (unless (null? segs)
      (unless first (write-char #\. out))
      (%write-key (car segs) out)
      (loop (cdr segs) #f))))

(define (%write-basic-string s out)
  (write-char #\" out)
  (string-for-each
    (lambda (ch)
      (cond
        ((char=? ch #\") (write-string "\\\"" out))
        ((char=? ch #\\) (write-string "\\\\" out))
        ((char=? ch #\newline) (write-string "\\n" out))
        ((char=? ch #\tab) (write-string "\\t" out))
        ((char=? ch #\return) (write-string "\\r" out))
        (else (write-char ch out))))
    s)
  (write-char #\" out))

(define (%write-number v out)
  (cond
    ((and (inexact? v) (= v +inf.0)) (write-string "inf" out))
    ((and (inexact? v) (= v -inf.0)) (write-string "-inf" out))
    ((and (inexact? v) (not (= v v))) (write-string "nan" out))
    ((and (inexact? v) (integer? v))
     ;; TOML floats must have a decimal point or exponent — 1.0, not 1 —
     ;; to stay distinguishable from an integer on reparse.
     (write-string (number->string v) out)
     (unless (%string-index (number->string v) #\. 0) (write-string ".0" out)))
    (else (write-string (number->string v) out))))

;; Writes a value in the position an ARRAY ELEMENT occupies (inline
;; everything, including tables — `[[...]]` header syntax only applies to
;; a named key's direct value at a table's own top level, not to arbitrary
;; nested structure, so a table nested inside an array is always written
;; as an inline `{ k = v }`).
(define (%write-inline-value v out)
  (cond
    ((toml-datetime? v) (write-string (toml-datetime->string v) out))
    ((eq? v #t) (write-string "true" out))
    ((eq? v #f) (write-string "false" out))
    ((number? v) (%write-number v out))
    ((string? v) (%write-basic-string v out))
    ((symbol? v) (%write-basic-string (symbol->string v) out))
    ((%value-table? v) (%write-inline-table v out))
    ((or (list? v) (vector? v)) (%write-inline-array (if (vector? v) (vector->list v) v) out))
    (else (%toml-error "toml-stringify: unsupported value" v))))

(define (%write-inline-array items out)
  (write-char #\[ out)
  (let loop ((xs items) (first #t))
    (unless (null? xs)
      (unless first (write-string ", " out))
      (%write-inline-value (car xs) out)
      (loop (cdr xs) #f)))
  (write-char #\] out))

(define (%write-inline-table alist out)
  (write-char #\{ out)
  (let loop ((kvs alist) (first #t))
    (unless (null? kvs)
      (unless first (write-string ", " out))
      (%write-key (caar kvs) out)
      (write-string " = " out)
      (%write-inline-value (cdar kvs) out)
      (loop (cdr kvs) #f)))
  (write-char #\} out))

;; A value counts as "inline" (written directly as `key = value`, never
;; needing its own [section]/[[section]] header) unless it's a table, or
;; a non-empty list whose every element is itself a table.
(define (%inline-toplevel-value? v)
  (not (or (%value-table? v) (%value-table-list? v))))

;; Writes every direct key of `alist` whose value is inline first (as
;; `key = value` lines), then recurses into sub-tables and arrays-of-
;; tables afterward, each under its own `path`-qualified header — the
;; conventional TOML layout (a table's own scalar/array keys precede any
;; of its nested [sub-table] blocks).
(define (%write-table-body alist path out)
  (for-each
    (lambda (kv)
      (when (%inline-toplevel-value? (cdr kv))
        (%write-key (car kv) out)
        (write-string " = " out)
        (%write-inline-value (cdr kv) out)
        (write-char #\newline out)))
    alist)
  (for-each
    (lambda (kv)
      (let ((v (cdr kv)) (here (append path (list (car kv)))))
        (cond
          ((%value-table? v)
           (write-char #\newline out)
           (write-char #\[ out) (%write-key-path here out) (write-string "]\n" out)
           (%write-table-body v here out))
          ((%value-table-list? v)
           (for-each
             (lambda (sub)
               (write-char #\newline out)
               (write-string "[[" out) (%write-key-path here out) (write-string "]]\n" out)
               (%write-table-body sub here out))
             v)))))
    alist))

;; (toml-write value port) — writes `value` as TOML directly to `port`,
;; without ever materializing the whole document as one intermediate
;; string first — `toml-stringify`/`toml-dump-file` below are both thin
;; wrappers around this, not the other way around, so a large document
;; written straight to a file only ever needs one line/value in flight at
;; a time.
(define (toml-write value port)
  (unless (or (null? value) (%value-table? value))
    (%toml-error "toml-write: the top-level value must be a table (an association list of string keys)" value))
  (%write-table-body value '() port))

(define (toml-stringify value)
  (let ((out (open-output-string)))
    (toml-write value out)
    (get-output-string out)))

(define (toml-dump-file value path)
  (call-with-output-file path (lambda (p) (toml-write value p))))

  )) ;; end begin, define-library
