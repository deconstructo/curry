# SRFI Compatibility: `(surfage sN …)`

*v1.2.2 — 2026-06-07*

Curry ships a set of pure-Scheme SRFI compatibility libraries under the `(surfage sN name)` namespace — a naming convention used by several Scheme implementations so that the same `(import …)` line works across them. The libraries delegate to Curry's built-ins wherever the procedure is already present; only missing names are defined in Scheme.

## Available libraries

| Library | SRFI | Description |
|---------|------|-------------|
| `(surfage s1 lists)` | [SRFI-1](https://srfi.schemers.org/srfi-1/) | List library |
| `(surfage s27 random-bits)` | [SRFI-27](https://srfi.schemers.org/srfi-27/) | Random-number sources |
| `(surfage s215 log)` | [SRFI-215](https://srfi.schemers.org/srfi-215/) | Central log exchange |
| `(surfage s170 posix)` | [SRFI-170](https://srfi.schemers.org/srfi-170/) | POSIX API (subset) — thin re-export of `(curry posix)`, requires `-DBUILD_MODULE_POSIX=ON` (default) |
| `(surfage s112 environment-inquiry)` | [SRFI-112](https://srfi.schemers.org/srfi-112/) | Implementation/OS/machine identity queries — thin re-export of `(curry posix)`, requires `-DBUILD_MODULE_POSIX=ON` (default) |

---

## `(surfage s1 lists)` — SRFI-1 list library

```scheme
(import (surfage s1 lists))
```

### Constructors and accessors

Most constructors and accessors are re-exported from `(scheme base)`: `cons`, `car`, `cdr`, `list`, `list*`, `make-list`, `length`, `append`, `reverse`, `list-tail`, `list-ref`.

#### `(iota count)` → *list*
#### `(iota count start)` → *list*
#### `(iota count start step)` → *list*

Return a list of `count` numbers beginning at `start` (default 0) and incrementing by `step` (default 1).

```scheme
(iota 5)           ; => (0 1 2 3 4)
(iota 4 1)         ; => (1 2 3 4)
(iota 5 0 0.5)     ; => (0 0.5 1.0 1.5 2.0)
```

#### `(last-pair lst)` → *pair*

Return the last pair of `lst`.

```scheme
(last-pair '(a b c))  ; => (c)
```

### Named selectors

```scheme
(first  lst)   ; (car lst)
(second lst)   ; (cadr lst)
(third  lst)   ; (caddr lst)
(fourth lst)   ; fourth element
(fifth  lst)   ; fifth element
```

### Predicates

#### `(any pred lst)` → *boolean*

Return `#t` if `pred` returns true for at least one element of `lst`.

#### `(every pred lst)` → *boolean*

Return `#t` if `pred` returns true for every element of `lst`.

#### `(count pred lst)` → *integer*

Count the number of elements for which `pred` returns true.

### Filtering

#### `(remove pred lst)` → *list*

Return a list of all elements for which `pred` returns `#f`. Complement of `filter`.

#### `(delete x lst)` → *list*
#### `(delete x lst =?)` → *list*

Remove all elements `equal?` (or `=?`) to `x`.

#### `(partition pred lst)` → *list list*

Return two values: elements satisfying `pred`, elements not satisfying `pred`.

```scheme
(define-values (evens odds) (partition even? '(1 2 3 4 5)))
; evens => (2 4), odds => (1 3 5)
```

### Folding

#### `(fold kons knil lst)` → *value*

Left fold. Alias for `fold-left`.

#### `(fold-right kons knil lst)` → *value*

Right fold. Re-exported from `(scheme base)`.

### Mapping

#### `(append-map f lst)` → *list*

Map `f` over `lst` and append the results. Equivalent to `(apply append (map f lst))`.

#### `(filter-map f lst)` → *list*

Map `f` and keep non-`#f` results.

#### `(flat-map f lst)` → *list*

Alias for `append-map`.

### Sublists

#### `(take lst n)` → *list*

Return the first `n` elements of `lst`.

#### `(drop lst n)` → *list*

Return `lst` with the first `n` elements removed.

#### `(take-while pred lst)` → *list*

Return the longest prefix of `lst` whose elements all satisfy `pred`.

#### `(drop-while pred lst)` → *list*

Drop elements from the front while `pred` is true; return the remainder.

---

## `(surfage s27 random-bits)` — SRFI-27 random sources

```scheme
(import (surfage s27 random-bits))
```

All procedures delegate to Curry's built-in SRFI-27 implementation (xoshiro256+ seeded from `/dev/urandom`). They are re-exported here so that portable code using `(surfage s27 random-bits)` works unchanged.

### Random sources

#### `default-random-source`

The default random source. Seeded from `/dev/urandom` at startup.

#### `(make-random-source)` → *source*

Create a new, independent random source.

#### `(random-source? x)` → *boolean*

Return `#t` if `x` is a random source.

#### `(random-source-randomize! src)`

Re-seed `src` from the system entropy source.

#### `(random-source-pseudo-randomize! src i j)`

Set `src` to a deterministic state parameterised by non-negative integers `i` and `j`. Useful for reproducible tests.

### Generating random numbers

#### `(random-source->random-integer src)` → *procedure*

Return a procedure `(lambda (n) …)` that returns a uniform random integer in `[0, n)`.

#### `(random-source->random-real src)` → *procedure*

Return a procedure `(lambda () …)` that returns a uniform random flonum in `(0.0, 1.0)`.

#### `(random-integer n)` → *integer*

Return a uniform random integer in `[0, n)` using `default-random-source`.

#### `(random-real)` → *flonum*

Return a uniform random flonum in `(0.0, 1.0)` using `default-random-source`.

---

## `(surfage s215 log)` — SRFI-215 central log exchange

```scheme
(import (surfage s215 log))
```

Decouples log producers (library code) from log consumers (the application). Library code calls `send-log` without knowing which logging backend, if any, is active; the application installs a callback to route messages wherever it likes.

#### `(send-log severity message key value ...)` → *unspecified*

Construct a log message and pass it to the current log callback as an association list. `severity` is stored under the key `SEVERITY`; `message` (a string) under `MESSAGE`. Any additional `key value ...` pairs are added to the message, followed by the fields from `current-log-fields`. Signals an error if an odd number of trailing arguments is given, or if a `key` is not a symbol.

Each `value` is stored as-is if it satisfies `string?`, `bytevector?`, `exact-integer?`, `error-object?`, or `condition?`; otherwise it is converted to a string as if by `write`.

```scheme
(send-log INFO "server started" 'PORT 8080)
; => callback receives ((SEVERITY . 6) (MESSAGE . "server started") (PORT . 8080))
```

#### Severity constants

`EMERGENCY` (0), `ALERT` (1), `CRITICAL` (2), `ERROR` (3), `WARNING` (4), `NOTICE` (5), `INFO` (6), `DEBUG` (7) — matching syslog severity levels.

#### `current-log-fields` — *parameter*, default `()`

A list of `(key . value)` pairs automatically appended to every message built by `send-log`. Use `parameterize` to scope contextual fields (e.g. a request ID) to a dynamic extent.

#### `current-log-callback` — *parameter*, default buffers messages

A procedure of one argument (the association-list message), called by `send-log` after building each message. Set it with `(current-log-callback proc)` to install a global handler, or scope one with `parameterize`.

```scheme
(current-log-callback
 (lambda (msg)
   (display (cdr (assq 'MESSAGE msg)) (current-error-port))
   (newline (current-error-port))))
```

Before the application installs its own callback, messages are held in a bounded buffer (most-recent 100 kept). The first time `current-log-callback` is set to a non-default procedure, the buffer replays into it in order and is cleared — so log calls made during startup, before logging is configured, aren't lost.

---

## `(surfage s170 posix)` — SRFI-170 POSIX API (subset)

```scheme
(import (surfage s170 posix))
```

Thin re-export of `(curry posix)` under the portable SRFI-170 name — see [`docs/reference/module-posix.md`](module-posix.md) for the full procedure list and the C module's scope (what's implemented vs. deliberately left out for a first pass: `posix-error?` introspection, `open-file`/`fd->port`, `create-fifo`, temp-file helpers, `file-space`, and the directory-generator API). Requires curry built with `-DBUILD_MODULE_POSIX=ON` (the default) — unlike the other `surfage` libraries, this one can't be pure Scheme, since `stat`, `opendir`/`readdir`, `getuid`, `chmod`, `symlink`, `umask`, and `getpid` have no Scheme-level equivalent to build on.

---

## `(surfage s112 environment-inquiry)` — SRFI-112 environment inquiry

```scheme
(import (surfage s112 environment-inquiry))
```

Thin re-export of `(curry posix)`'s six SRFI-112 procedures (`implementation-name`, `implementation-version`, `cpu-architecture`, `machine-name`, `os-name`, `os-version`) — see [`docs/reference/module-posix.md`](module-posix.md#environment-inquiry-srfi-112). Also requires `-DBUILD_MODULE_POSIX=ON` (the default), since `uname(2)`/`gethostname(2)` are syscalls with no Scheme-level equivalent to build on.

## Portability note

Code written against `(surfage s1 lists)`, `(surfage s27 random-bits)`, `(surfage s215 log)`, `(surfage s170 posix)`, and `(surfage s112 environment-inquiry)` is compatible with Guile, Chicken (via the `surfage-egg`), Chibi-Scheme, and other implementations that follow the same naming convention. The only difference is that Curry's `(surfage s27 random-bits)` uses xoshiro256+ internally rather than the Mersenne Twister typically found in other implementations; the statistical properties are equivalent or better.
