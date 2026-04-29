# Module: (curry json)

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
| array | list |
| object | association list `((key . value) ...)` where keys are strings |

Raises an error if the input is not valid JSON.

### `(json-stringify value)` → string

Serialise a Scheme value to a JSON string.

| Scheme | JSON |
|--------|------|
| `#f` | `null` |
| `#t` | `true` |
| fixnum / flonum | number |
| string | string (with escaping) |
| list of pairs `((k . v) ...)` | object |
| list | array |
| vector | array |

## Examples

```scheme
(import (curry json))

(define data (json-parse "{\"name\":\"Alice\",\"age\":30,\"tags\":[\"admin\",\"user\"]}"))
; => (("name" . "Alice") ("age" . 30) ("tags" . ("admin" "user")))

(cdr (assoc "name" data))   ; => "Alice"
(cdr (assoc "age"  data))   ; => 30

(json-stringify '(("x" . 1) ("y" . 2)))
; => "{\"x\":1,\"y\":2}"

(json-stringify '(1 2 3 "hello" #t))
; => "[1,2,3,\"hello\",true]"
```

## Notes

- `null` maps to `#f` (there is no distinct null value in Scheme). If you need to distinguish null from false, check the JSON source before parsing or use a wrapper.
- Object keys are returned as strings, not symbols.
- Numbers with a decimal point or exponent become flonums; integers become fixnums.
