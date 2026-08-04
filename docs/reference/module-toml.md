# Module: (curry toml)

*unreleased*

TOML 1.0 reader and writer, pure Scheme — no C, no external dependency. Mirrors [`(curry yaml)`](module-yaml.md)'s shape deliberately: tables become association lists (string keys), arrays become lists, and — since TOML datetimes are unquoted literals distinct from strings (a bare `1979-05-27` is not the same value as the quoted string `"1979-05-27"`) — datetimes get the same "distinguished sentinel" treatment `(curry yaml)`'s `yaml-null` uses for YAML's null: a `toml-datetime` record (tested with `toml-datetime?`), rather than collapsing them into plain strings and losing the distinction on write.

## Import

```scheme
(import (curry toml))
```

## Scope

Supported: key/value pairs (bare, quoted, and dotted keys), standard tables `[table]` and dotted-path tables `[a.b.c]`, arrays of tables `[[table]]`, inline tables `{ k = v, ... }`, arrays (including multi-line and heterogeneous, per TOML 1.0), all four string forms (basic `"..."`, literal `'...'`, multi-line basic `"""..."""` with line-continuation backslashes, multi-line literal `'''...'''`), integers (decimal/hex/octal/binary, underscores, sign), floats (exponents, `inf`/`nan` with sign, underscores), booleans, and all four RFC-3339-ish datetime forms (offset date-time, local date-time, local date, local time).

Deliberately **not** supported:

- **Duplicate-key/redefinition errors.** The TOML spec requires rejecting a key defined twice; this module simply overwrites (the same simplification `(curry yaml)` makes for merge keys) — *except* when the redefinition changes a key's shape from a plain value to a table (or vice versa), which does raise, since silently reinterpreting the existing value as something it isn't would corrupt already-parsed data rather than just discard a duplicate.
- **Strict inline-table single-line/no-trailing-comma enforcement** — accepted leniently rather than rejected.
- **Full Unicode-escape validation** in basic strings beyond `\uXXXX`/`\UXXXXXXXX` decoding into the corresponding character.

## Values

| TOML | Scheme |
|------|--------|
| string (any of the four forms) | string |
| integer (decimal, `0x…`/`0o…`/`0b…`, `1_000`) | exact integer |
| float (`3.14`, `1e10`, `inf`, `nan`, `1_234.5`) | flonum |
| `true`/`false` | `#t`/`#f` |
| datetime (any of the four RFC-3339-ish forms) | `toml-datetime` (a distinguished record — see below) |
| array `[ ... ]` | list |
| table / inline table `{ ... }` | association list, string keys |

### `toml-datetime` / `(toml-datetime? x)` / `(toml-datetime->string x)`

```scheme
(import (curry toml))
(define d (cdr (assoc "dob" (toml-parse "dob = 1979-05-27T07:32:00-08:00"))))
(toml-datetime? d)          ; => #t
(toml-datetime->string d)   ; => "1979-05-27T07:32:00-08:00"
```

The raw literal text is kept as-is (not decomposed into year/month/day/etc. fields) — this module doesn't do date arithmetic, and preserving the exact text is what round-tripping on write needs anyway.

## Reading

### `(toml-parse string)` → value

Parse a TOML document string and return its root table (an association list) — every TOML document's top level is a table, so this never returns a bare scalar or array.

### `(toml-load-file path)` → value

Convenience wrapper reading the whole file first.

```scheme
(import (curry toml))

(toml-parse "title = \"TOML Example\"\n[owner]\nname = \"Tom\"")
; => (("title" . "TOML Example") ("owner" ("name" . "Tom")))

(toml-parse "[[fruits]]\nname = \"apple\"\n\n[[fruits]]\nname = \"banana\"")
; => (("fruits" (("name" . "apple")) (("name" . "banana"))))
```

## Writing

### `(toml-stringify value)` → string

Serialize a Scheme value — an association list of string keys, i.e. what `toml-parse` itself returns — to a TOML string. Raises if `value` isn't a table (TOML has no concept of a document whose top level is a bare array or scalar).

| Scheme | TOML |
|--------|------|
| string | quoted with `"..."`, backslash-escaping `"`/`\`/newline/tab/return |
| exact integer | decimal integer |
| flonum | float — always with a decimal point or `inf`/`nan`, so `1.0` prints as `1.0`, not `1` (which would reparse as an integer) |
| `#t` / `#f` | `true` / `false` |
| `toml-datetime` | its own literal text, unquoted |
| association list (string keys) as a **named key's own value** | `[section]` header, followed by that table's own body |
| a **non-empty list whose every element is itself such a table** | `[[section]]` headers, one per element |
| any other list, or a table occurring as an *array element* | inline `[ ... ]` / `{ k = v, ... }`, recursively |

```scheme
(import (curry toml))
(toml-stringify '(("title" . "Example") ("owner" ("name" . "Tom"))))
; =>
; "title = \"Example\"\n\n[owner]\nname = \"Tom\"\n"
```

### `(toml-dump-file value path)`

Write `(toml-stringify value)` to a file.

## Notes

- A table written to TOML always uses the `[section]`/`[[section]]` header style, never the inline `{ }` style, when it's the direct value of a named key at a table's own top level — TOML's inline-table syntax is reserved for tables occurring as *array elements* (or a table nested inside another table's value in a way that isn't itself a direct top-level key, which doesn't arise from `toml-parse`'s own output). There's no way to tell, from the Scheme value alone, whether a table was originally written inline or as a full section in the source — like `(curry yaml)` not preserving flow-vs-block style, this module doesn't preserve that either; only the data round-trips exactly, not the original formatting choices.
- An empty table and an empty array are both just `'()` in Scheme — same inherent ambiguity `(curry yaml)` has for empty mappings vs. empty sequences. An empty sub-table value is written as `[]` (an empty array), not as an empty `[section]` header.
- `toml-parse ""` (an empty document) returns `'()`; `(toml-stringify '())` returns `""`.

## See also

- [`module-yaml.md`](module-yaml.md) — the sibling module this one's API deliberately mirrors
- [`module-json.md`](module-json.md) — the third structured-config-format reader/writer, using a different null-handling convention (see that module's own notes)
