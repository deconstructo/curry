;;; (curry csv) — RFC 4180 CSV reader and writer, pure Scheme.
;;;
;;; Mirrors (curry yaml)/(curry toml)'s shape deliberately: a document is a
;;; list of rows, each row a list of field strings — no coercion to numbers
;;; or symbols, since CSV has no type system of its own and "2" vs 2 is a
;;; decision only the caller can make. `csv-parse` takes an optional
;;; `#:header?` keyword-style second argument (a plain positional boolean,
;;; curry has no `#:` keyword args) to instead return a list of association
;;; lists keyed by the first row's field values, the common "treat row 1 as
;;; column names" convention every other CSV library offers.
;;;
;;; Supported: RFC 4180 quoting (`"..."` fields, `""` escaping an embedded
;;; quote, embedded commas/newlines/CRLF inside quoted fields), a
;;; configurable single-character delimiter (defaults to `,`; `\t` for
;;; TSV-as-CSV is a one-argument change, not a separate module), both LF and
;;; CRLF row separators (accepted interchangeably on read; the writer always
;;; emits CRLF per RFC 4180 §2.1, the same way (curry toml)/(curry yaml)
;;; always normalize line endings away on read rather than preserving
;;; whatever the input happened to use), and a trailing-newline-optional
;;; final row.
;;;
;;; Deliberately not supported (see docs/reference/module-csv.md):
;;;
;;; - Per-column type inference or coercion. Every field is a string; a
;;;   caller who wants numbers calls `string->number` themselves. This is
;;;   the same "don't guess the caller's types" stance (curry toml) and
;;;   (curry yaml) take for their *unquoted*-but-still-atomic scalar forms
;;;   — the difference here is that CSV has no unquoted-vs-quoted type
;;;   distinction to preserve at all, so there is nothing left to infer.
;;; - Ragged-row validation. RFC 4180 says every row in a well-formed CSV
;;;   file has the same field count; this module does not check or enforce
;;;   that on read (short and long rows are both returned as-is) — except
;;;   under `#:header?`, where a data row with fewer fields than the header
;;;   row necessarily produces an alist with fewer entries than the header
;;;   has columns, which is left to the caller to notice, not raised as an
;;;   error.
;;; - Comment lines / `#`-prefixed skip lines — not part of RFC 4180; several
;;;   CSV dialects (and some CSV *libraries*) support them, but not on a
;;;   plain read.

(define-library (curry csv)
  (import (scheme base))
  (export
    csv-parse csv-read csv-load-file
    csv-stringify csv-write csv-dump-file)
  (begin

;;; =========================================================================
;;; Small local helpers (kept independent of (srfi s1 lists))
;;; =========================================================================

(define (%csv-error msg . irritants) (apply error (string-append "csv: " msg) irritants))

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

;;; =========================================================================
;;; Cursor: a mutable (string pos len) triple, same imperative-recursive-
;;; descent convention (curry toml)/(curry yaml) use — CSV's row/field
;;; structure is naturally a linear scan with occasional lookahead (a
;;; doubled quote inside a quoted field), which a mutable position walks
;;; more directly than a functional accumulator would.
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

;;; =========================================================================
;;; Parsing
;;; =========================================================================

;; A quoted field: cursor is positioned right after the opening quote.
;; `""` inside a quoted field is a literal `"`; any other character
;; (including the delimiter and both newline forms) is taken verbatim
;; until the single closing quote — this is exactly what lets a quoted
;; field embed the delimiter or a newline without ending the field.
(define (%parse-quoted-field! c out)
  (let loop ()
    (cond
      ((%c-eof? c) (%csv-error "unterminated quoted field"))
      ((and (char=? (%c-ch c) #\") (eqv? (%c-ch-at c 1) #\"))
       (write-char #\" out) (%c-adv-n! c 2) (loop))
      ((char=? (%c-ch c) #\") (%c-adv! c))
      (else (write-char (%c-ch c) out) (%c-adv! c) (loop)))))

;; An unquoted field runs up to (not including) the next delimiter, LF,
;; CR, or EOF.
(define (%parse-unquoted-field! c delim out)
  (let loop ()
    (unless (or (%c-eof? c)
                (char=? (%c-ch c) delim)
                (char=? (%c-ch c) #\newline)
                (char=? (%c-ch c) #\return))
      (write-char (%c-ch c) out) (%c-adv! c) (loop))))

;; RFC 4180 permits nothing but a delimiter, a row terminator, or EOF
;; immediately after a quoted field's closing quote. Without this check,
;; malformed input like "a",x,b (stray "x" glued onto the closing quote)
;; would silently start parsing "x" as if it were unquoted content
;; rather than being rejected — a silent-corruption risk rather than a
;; clean, caller-visible error.
(define (%check-after-quoted-field! c delim)
  (unless (or (%c-eof? c) (char=? (%c-ch c) delim)
              (char=? (%c-ch c) #\newline) (char=? (%c-ch c) #\return))
    (%csv-error "unexpected character after closing quote" (%c-ch c))))

(define (%parse-field! c delim)
  (let ((out (open-output-string)))
    (if (and (not (%c-eof? c)) (char=? (%c-ch c) #\"))
        (begin (%c-adv! c) (%parse-quoted-field! c out) (%check-after-quoted-field! c delim))
        (%parse-unquoted-field! c delim out))
    (get-output-string out)))

;; Consumes exactly one row terminator (CRLF, lone LF, or lone CR) if the
;; cursor is sitting on one. A no-op at EOF, so a file with no trailing
;; newline on its last row still parses correctly.
(define (%skip-row-terminator! c)
  (cond
    ((and (char=? (%c-ch c) #\return) (eqv? (%c-ch-at c 1) #\newline)) (%c-adv-n! c 2))
    ((or (char=? (%c-ch c) #\newline) (char=? (%c-ch c) #\return)) (%c-adv! c))))

(define (%parse-row! c delim)
  (let loop ((fields (list (%parse-field! c delim))))
    (if (and (not (%c-eof? c)) (char=? (%c-ch c) delim))
        (begin (%c-adv! c) (loop (cons (%parse-field! c delim) fields)))
        (reverse fields))))

;; Returns a list of rows, each a list of field strings. A completely
;; empty document parses to '() (zero rows), not a single row of one
;; empty field — but a document consisting of just a row terminator (an
;; otherwise-blank line) DOES parse to one row of one empty field, same
;; as any other CSV reader: there is no way to distinguish "blank line"
;; from "one empty field" in RFC 4180's grammar, and blank line is the
;; standard reading.
(define (%csv-parse-rows str delim)
  (let ((c (%mk-cursor str)))
    (let loop ((rows '()))
      (if (%c-eof? c)
          (reverse rows)
          (let ((row (%parse-row! c delim)))
            (%skip-row-terminator! c)
            (loop (cons row rows)))))))

;;; =========================================================================
;;; Reading
;;; =========================================================================

;; (csv-parse string) -> list of rows (each a list of strings)
;; (csv-parse string delim) -> same, with a custom single-character delimiter
;; (csv-parse string delim header?) -> if header? is true, the first row is
;;   consumed as column names and every remaining row is returned as an
;;   association list (column-name . field-value), in column order.
(define (csv-parse str . opts)
  (let* ((delim (if (pair? opts) (car opts) #\,))
         (header? (if (and (pair? opts) (pair? (cdr opts))) (cadr opts) #f))
         (rows (%csv-parse-rows str delim)))
    (if (and header? (pair? rows))
        (let ((cols (car rows)))
          (map (lambda (row) (map cons cols row)) (cdr rows)))
        rows)))

;; (csv-read port . opts) -> same as csv-parse, reading a whole port's
;; content first. CSV's quoted-field newline-embedding means a row can't
;; always be identified by scanning one line at a time, so — same
;; rationale (curry toml)'s toml-read gives — this slurps the port rather
;; than reading incrementally.
(define (csv-read port . opts) (apply csv-parse (%port->string port) opts))

(define (csv-load-file path . opts)
  (call-with-input-file path (lambda (p) (apply csv-read p opts))))

;;; =========================================================================
;;; Writing
;;; =========================================================================

;; A field needs quoting if it contains the delimiter, a quote, a CR, or
;; an LF — the RFC 4180 §2.6 rule. `"` inside a quoted field is escaped
;; by doubling it.
(define (%field-needs-quoting? s delim)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (and (< i n)
           (or (char=? (string-ref s i) delim)
               (memv (string-ref s i) (list #\" #\newline #\return))
               (loop (+ i 1)))))))

(define (%write-field s delim out)
  (if (%field-needs-quoting? s delim)
      (begin
        (write-char #\" out)
        (string-for-each
          (lambda (ch) (when (char=? ch #\") (write-char #\" out)) (write-char ch out))
          s)
        (write-char #\" out))
      (write-string s out)))

(define (%write-row fields delim out)
  (let loop ((fs fields) (first #t))
    (unless (null? fs)
      (unless first (write-char delim out))
      (%write-field (car fs) delim out)
      (loop (cdr fs) #f)))
  (write-string "\r\n" out))

;; A "row" for writing purposes is either a plain list of field strings,
;; or (when the caller is writing header-keyed data back out, symmetric
;; with csv-parse's #:header? mode) an association list of (name . value)
;; pairs — in which case only the values are written, in alist order,
;; and the caller is responsible for writing a matching header row
;; themselves first if they want one (this module has no stored notion
;; of "the" column order to reconstruct a header from on its own).
(define (%row->fields row)
  (if (and (pair? row) (pair? (car row)))
      (map (lambda (kv) (let ((v (cdr kv))) (if (string? v) v (%csv-error "csv-write: field value is not a string" v)))) row)
      (map (lambda (v) (if (string? v) v (%csv-error "csv-write: field value is not a string" v))) row)))

;; (csv-write rows port) -> writes `rows` (a list of rows, RFC 4180 CRLF-
;; terminated, delimiter default ',') directly to `port`.
;; (csv-write rows port delim) -> custom delimiter.
(define (csv-write rows port . opts)
  (let ((delim (if (pair? opts) (car opts) #\,)))
    (for-each (lambda (row) (%write-row (%row->fields row) delim port)) rows)))

(define (csv-stringify rows . opts)
  (let ((out (open-output-string)))
    (apply csv-write rows out opts)
    (get-output-string out)))

(define (csv-dump-file rows path . opts)
  (call-with-output-file path (lambda (p) (apply csv-write rows p opts))))

  )) ;; end begin, define-library
