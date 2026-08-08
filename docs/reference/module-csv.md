# Module: (curry csv)

*unreleased*

RFC 4180 CSV reader and writer, pure Scheme — no C, no external dependency. Mirrors [`(curry toml)`](module-toml.md)/[`(curry yaml)`](module-yaml.md)'s shape deliberately: a document is a list of rows, each row a list of field strings, with no type inference — CSV has no type system of its own, so every field stays a string and a caller who wants numbers calls `string->number` themselves.

## Import

```scheme
(import (curry csv))
```

## Scope

Supported: RFC 4180 quoting (`"..."` fields, `""` escaping an embedded quote, embedded commas/newlines/CRLF inside quoted fields), a configurable single-character delimiter (default `,`; pass `#\tab` for TSV), both LF and CRLF row separators on read (the writer always emits CRLF per RFC 4180 §2.1), and an optional trailing newline on the final row.

Deliberately **not** supported:

- **Per-column type inference or coercion.** Every field is a string.
- **Ragged-row validation.** RFC 4180 requires every row to have the same field count; this module does not check or enforce that on read.
- **Comment lines** or other CSV-dialect extensions beyond plain RFC 4180.

## Reading

### `(csv-parse string)` → list of rows

### `(csv-parse string delim)` → list of rows, with a custom delimiter character

### `(csv-parse string delim header?)` → list of association lists

If `header?` is true, the first row is consumed as column names and every remaining row is returned as an association list of `(column-name . field-value)` pairs, in column order — the common "row 1 is the header" convention.

```scheme
(import (curry csv))

(csv-parse "a,b\nc,d")
; => (("a" "b") ("c" "d"))

(csv-parse "name,age\nAlice,30\nBob,25" #\, #t)
; => ((("name" . "Alice") ("age" . "30")) (("name" . "Bob") ("age" . "25")))

(csv-parse "a\tb\tc" #\tab)
; => (("a" "b" "c"))
```

Raises if a quoted field is unterminated, or if anything other than the delimiter, a row terminator, or EOF immediately follows a quoted field's closing quote (`"a"x,b` is rejected, not silently misread as a new field starting with `x`).

A quoted field may embed the delimiter, a literal `"` (doubled, `""`), or a newline:

```scheme
(csv-parse "\"a,b\",\"c\"\"d\",\"e\nf\"")
; => (("a,b" "c\"d" "e\nf"))
```

An empty document parses to `'()` (zero rows); a document consisting of just a row terminator parses to one row of one empty field, `(("" ))` — RFC 4180's grammar has no way to distinguish "blank line" from "one empty field", and "blank line" is the reading every CSV library gives it.

### `(csv-read port . opts)` → same as `csv-parse`

Reads a whole port's content first, then parses it — CSV's quoted-field newline-embedding means a row can't reliably be identified one line at a time, so this isn't incremental/streaming parsing. `opts` are the same optional `delim`/`header?` arguments `csv-parse` takes.

### `(csv-load-file path . opts)` → same as `csv-read`

Convenience wrapper: `(call-with-input-file path (lambda (p) (csv-read p . opts)))`.

## Writing

### `(csv-stringify rows . opts)` → string

Serialize `rows` — a list of rows, each either a plain list of field strings or an association list of `(name . value)` pairs (only the values are written, in alist order, symmetric with `csv-parse`'s header mode) — to a CSV string. `opts` is an optional custom delimiter character. Raises if any field value isn't a string.

A field is quoted only when it needs to be: it contains the delimiter, a `"`, a CR, or an LF. An embedded `"` is escaped by doubling it.

```scheme
(import (curry csv))
(csv-stringify '(("a" "b") ("c" "d")))
; => "a,b\r\nc,d\r\n"

(csv-stringify '(("a,b" "c")))
; => "\"a,b\",c\r\n"   ; a field containing the delimiter itself is quoted
```

### `(csv-write rows port . opts)`

Write `rows` as CSV directly to `port`. `csv-stringify`/`csv-dump-file` are both thin wrappers around this (not the other way around), so writing straight to a file never needs the whole document materialized as one intermediate string first.

### `(csv-dump-file rows path . opts)`

Convenience wrapper: `(call-with-output-file path (lambda (p) (csv-write rows p . opts)))`.

## Notes

- Writing header-keyed rows (association lists) does *not* automatically write a header row of column names first — this module has no stored notion of "the" column order to reconstruct one from; write a plain list-of-strings header row yourself first if you want one.
- `csv-parse ""` returns `'()`; `(csv-stringify '())` returns `""`.
- The writer always emits `\r\n` row terminators regardless of what the input (if any) used — same as `(curry toml)`/`(curry yaml)` normalizing line endings away on read rather than preserving the original.

## See also

- [`module-toml.md`](module-toml.md), [`module-yaml.md`](module-yaml.md) — the sibling structured-format modules this one's API deliberately mirrors
