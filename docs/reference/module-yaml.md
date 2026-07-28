# Module: (curry yaml)

*unreleased*

YAML reader and writer, pure Scheme — no C, no external dependency. Unlike [`(curry json)`](module-json.md), which collapses `null` and `#f` into the same Scheme value, YAML `null` here is a distinguished sentinel (`yaml-null`, tested with `yaml-null?`), the same approach [SRFI-180](https://srfi.schemers.org/srfi-180/) takes for JSON — the right call for YAML specifically, since real config files (Kubernetes manifests, CI configs) often rely on null-vs-false being distinguishable.

## Import

```scheme
(import (curry yaml))
```

## Scope

Supported: block and flow mappings/sequences, all scalar styles (plain, single-/double-quoted, literal `|`, folded `>`), comments, `---`/`...` document markers and multi-document streams, implicit scalar typing (null/bool/int/float/string, including hex `0x`/octal `0o`/underscore-separated `1_000` integers and `.inf`/`-.inf`/`.nan`), the core explicit tags (`!!str`/`!!int`/`!!float`/`!!bool`/`!!null`) to override implicit typing, anchors (`&name`) and aliases (`*name`), and merge keys in both block and flow mappings — both single-source (`<<: *anchor`) and multi-source (`<<: [*a, *b, ...]`, flattening each source's keys in order with first-source-wins on any key present in more than one source).

Deliberately **not** supported:

- **Complex mapping keys** (`? key` / `: value` explicit-key form) — a mapping key is always a scalar (plain, quoted, or anchor/alias/tag-wrapped) in this module. Covers the overwhelming majority of real-world YAML; explicit complex keys are rare.
- **YAML directives** (`%YAML`, `%TAG`) are skipped, not processed.
- **Custom/application tags** other than the core-schema scalar tags are ignored — content still parses with normal implicit typing under an unrecognized tag, it just isn't specially interpreted.
- **YAML 1.1 sexagesimal numbers** (e.g. `1:20:30` as 4830) are not recognized — parsed as a plain string instead.
- **Shared/circular structure preservation on write** — `yaml-stringify` does not detect `eq?`-shared substructure and re-emit it as anchors/aliases; each occurrence is serialized independently. A script that builds a genuinely circular structure and stringifies it will loop forever.

## Values

| YAML | Scheme |
|------|--------|
| `null`, `~`, empty | `yaml-null` (a distinguished sentinel — see below) |
| `true`/`yes`/`on`, `false`/`no`/`off` (any case) | `#t`, `#f` |
| integer (decimal, `0x…`, `0o…`, `1_000`) | exact integer |
| float (`3.14`, `1e10`, `.inf`, `-.inf`, `.nan`) | flonum |
| string (any scalar style) | string |
| block/flow sequence | list |
| block/flow mapping | association list `((key . value) ...)`, keys type-resolved the same as values (not forced to strings) |

### `yaml-null` and `(yaml-null? x)`

```scheme
(import (curry yaml))
(yaml-null? (yaml-parse "~"))              ; => #t
(yaml-null? (yaml-parse "false"))          ; => #f  (distinct from null, unlike (curry json))
```

## Reading

### `(yaml-parse string)` → value

Parse a YAML string and return the first (or only) document. Returns `yaml-null` for an empty or all-comments input.

### `(yaml-parse-all string)` → list of values

Parse a `---`-separated multi-document stream and return every document as a list.

### `(yaml-load-file path)` → value
### `(yaml-load-file-all path)` → list of values

Convenience wrappers reading the whole file first.

```scheme
(import (curry yaml))

(yaml-parse "name: Alice\nage: 30")
; => (("name" . "Alice") ("age" . 30))

(yaml-parse "
base: &defaults
  retries: 3
  timeout: 30
service:
  <<: *defaults
  timeout: 60
")
; => (("base" ("retries" . 3) ("timeout" . 30))
;     ("service" ("timeout" . 60) ("retries" . 3)))
```

## Writing

### `(yaml-stringify value)` → string

Serialize a Scheme value to a YAML string, block style, 2-space indentation.

| Scheme | YAML |
|--------|------|
| `yaml-null` | `null` |
| `#t` / `#f` | `true` / `false` |
| exact integer / flonum | number (`.inf`/`-.inf`/`.nan` for the corresponding flonums) |
| string | plain scalar if safe to leave unquoted, else single- or double-quoted; a string containing a newline is emitted as a literal block scalar `\|` with the chomping indicator chosen so it round-trips exactly |
| association list `((k . v) ...)` or a hash table | block mapping |
| list or vector | block sequence |
| `'()` / empty vector / empty hash table | inline `[]` / `{}` |

### `(yaml-dump-file value path)`

Write `(yaml-stringify value)` to a file.

```scheme
(import (curry yaml))
(yaml-stringify '(("name" . "Alice") ("tags" "admin" "user")))
; =>
; "name: Alice\ntags:\n  - admin\n  - user\n"

(yaml-dump-file '(("port" . 8080)) "config.yaml")
```

## Notes

- A string that would be read back as something other than itself (e.g. `"true"`, `"42"`, `"null"`) is automatically quoted by `yaml-stringify` so it round-trips as a string.
- Plain scalar values that span multiple more-indented lines are folded per YAML's line-folding rule: consecutive lines join with a single space, a blank line becomes a real newline.
- Anchors/aliases are resolved by simple `eq?`-shared reference during read — `(yaml-parse "x: &n [1,2]\ny: *n")` gives two keys whose values are the *same* list object.
