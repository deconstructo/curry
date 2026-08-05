# Module: (curry json)

*v1.2.2 — 2026-06-07*

Pure-C JSON parser and serialiser. No external dependencies.

## Installation

No extra packages required. Enabled by default (`-DBUILD_MODULE_JSON=ON`).

## Import

```scheme
(import (curry json))
```

## Procedures

### `(json-parse string)` → value

Parse a JSON string and return the corresponding Scheme value.

| JSON | Scheme |
|------|--------|
| `null` | `#f` |
| `true` | `#t` |
| `false` | `#f` |
| number | fixnum or flonum |
| string | string |
| array | vector |
| object | association list `((key . value) ...)` where keys are strings |

Raises an error if the input is not valid JSON.

### `(json-read port)` → value

Like `json-parse`, but reads from `port` (string, bytevector, or file) instead of an already-in-hand string. Reads `port` to end-of-file first — JSON's structure (nested objects/arrays) means the parser needs to look ahead arbitrarily anyway, so this isn't incremental/streaming parsing, just a port-native entry point. Raises if `port` isn't actually a port.

### `(json-load-file path)` → value

Convenience wrapper: opens `path`, reads and parses its entire content, closes the file. Raises if `path` can't be opened.

### `(json-stringify value)` → string

Serialise a Scheme value to a JSON string.

| Scheme | JSON |
|--------|------|
| `#f` | `null` |
| `#t` | `true` |
| fixnum / flonum | number |
| bignum / exact rational / other numeric-tower value | number (converted to its nearest double — JSON's own number type is IEEE double, so this is the same lossy tradeoff as `exact->inexact`; a bignum too large to fit becomes `+inf.0`/`-inf.0`-range double behavior; for a complex value, only the real part is used — the imaginary part is silently dropped, matching how the rest of curry's numeric tower already treats a complex-to-real conversion elsewhere) |
| string | string (with escaping) |
| list of pairs `((k . v) ...)` | object |
| list | array |
| vector | array |

### `(json-write value port)`

Like `json-stringify`, but writes directly to `port` (string, bytevector, or file) instead of building and returning a string. Raises if `port` isn't actually a port.

### `(json-dump-file value path)`

Convenience wrapper: opens `path`, writes `value` as JSON, closes the file. Raises if `path` can't be opened for writing.

## Examples

```scheme
(import (curry json))

(define data (json-parse "{\"name\":\"Alice\",\"age\":30,\"tags\":[\"admin\",\"user\"]}"))
; => (("tags" . #("admin" "user")) ("age" . 30) ("name" . "Alice"))

(cdr (assoc "name" data))   ; => "Alice"
(cdr (assoc "age"  data))   ; => 30

(json-stringify '(("x" . 1) ("y" . 2)))
; => "{\"x\":1,\"y\":2}"

(json-stringify '(1 2 3 "hello" #t))
; => "[1,2,3,\"hello\",true]"

;; Port and file forms
(json-read (open-input-string "{\"n\":1}"))       ; => (("n" . 1))
(json-write '(("n" . 1)) (current-output-port))   ; writes {"n":1} to stdout

(json-dump-file '(("hello" . "world")) "config.json")
(json-load-file "config.json")                    ; => (("hello" . "world"))
```

## Notes

- `null` maps to `#f` (there is no distinct null value in Scheme). If you need to distinguish null from false, check the JSON source before parsing or use a wrapper.
- Object keys are returned as strings, not symbols.
- Numbers with a decimal point or exponent become flonums; integers become fixnums.
