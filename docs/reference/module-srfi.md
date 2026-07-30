# SRFI Compatibility: `(srfi sN …)`

*v1.2.2 — 2026-06-07*

Curry ships a set of pure-Scheme SRFI compatibility libraries under the `(srfi sN name)` namespace — a naming convention used by several Scheme implementations so that the same `(import …)` line works across them. The libraries delegate to Curry's built-ins wherever the procedure is already present; only missing names are defined in Scheme.

## Bare-number `(srfi N)` shims

The naming convention SRFI 97 itself specifies — and the one Chibi-Scheme and Gauche actually implement — is a bare-number library name with no descriptive component: `(import (srfi 1))`, not `(import (srfi s1 lists))`. For every library below, curry also ships a one-line shim `lib/curry/modules/srfi/N.scm` that just imports the `(srfi sN name)` library and re-exports everything it exports, so both spellings work:

```scheme
(import (srfi 1))          ; same bindings as...
(import (srfi s1 lists))   ; ...this
```

`(srfi N)` library names use an *exact non-negative integer* as the second component, which R7RS's library-name grammar explicitly permits alongside identifiers. Note for combining multiple `(srfi N)` imports in one program: several of these libraries deliberately share generic names on purpose (`(srfi 69)`/`(srfi 90)` and `(srfi 125)`/`(srfi 126)` both touch hash tables, `(srfi 113)` touches sets) — if two imported libraries export the *same* name with different behavior (e.g. `(srfi 69)`'s and `(srfi 125)`'s both-named `make-hash-table`), only one wins in that flat top-level scope, same as any Scheme's colliding-import behavior. `(srfi 125)`/`(srfi 126)` avoid this with each other (`hash-table-*` vs `hashtable-*`), so they're safe to combine with each other, just not with `(srfi 69)`/`(srfi 90)`.

## Available `(srfi sN name)` libraries

| Library | SRFI | Description |
|---------|------|-------------|
| `(srfi s1 lists)` | [SRFI-1](https://srfi.schemers.org/srfi-1/) | List library |
| `(srfi s27 random-bits)` | [SRFI-27](https://srfi.schemers.org/srfi-27/) | Random-number sources |
| `(srfi s215 log)` | [SRFI-215](https://srfi.schemers.org/srfi-215/) | Central log exchange |
| `(srfi s170 posix)` | [SRFI-170](https://srfi.schemers.org/srfi-170/) | POSIX API (subset) — thin re-export of `(curry posix)`, requires `-DBUILD_MODULE_POSIX=ON` (default) |
| `(srfi s112 environment-inquiry)` | [SRFI-112](https://srfi.schemers.org/srfi-112/) | Implementation/OS/machine identity queries — thin re-export of `(curry posix)`, requires `-DBUILD_MODULE_POSIX=ON` (default) |
| `(srfi s238 codesets)` | [SRFI-238](https://srfi.schemers.org/srfi-238/) | `errno`/`signal`/`http-status` code ⟷ symbol ⟷ message lookup — thin re-export of `(curry codesets)`, requires `-DBUILD_MODULE_CODESETS=ON` (default) |
| `(srfi s69 hash-tables)` | [SRFI-69](https://srfi.schemers.org/srfi-69/) | Basic hash tables — full API, wrapping curry's built-in hash table with corrected `hash-table-ref` semantics |
| `(srfi s90 hash-tables)` | [SRFI-90](https://srfi.schemers.org/srfi-90/srfi-90.html) | `make-table`, a keyword-argument hash-table constructor layered on `(srfi s69 hash-tables)` |
| `(srfi s174 posix-timespecs)` | [SRFI-174](https://srfi.schemers.org/srfi-174/) | Immutable `(seconds nanoseconds)` time-instant type |
| `(srfi s19 time)` | [SRFI-19](https://srfi.schemers.org/srfi-19/) | Time/date objects, Julian Day conversions, `strftime`-style formatting — requires `-DBUILD_MODULE_POSIX=ON` (default) for `current-time` |
| `(srfi s8 receive)` | [SRFI-8](https://srfi.schemers.org/srfi-8/) | `receive` multiple-values binding macro — shadows curry's own actor `receive` special form when imported; see its section below |
| `(srfi s145 assume)` | [SRFI-145](https://srfi.schemers.org/srfi-145/) | `assume` runtime invariant declaration (always checked, never elided) |
| `(srfi s227 optional-arguments)` | [SRFI-227](https://srfi.schemers.org/srfi-227/) | `opt-lambda`, `let-optionals`/`let-optionals*`, `default-object`/`default-object?` — spelled with `#:optional`/`#:rest`, not `#!optional`/`#!rest` |
| `(srfi s128 comparators)` | [SRFI-128](https://srfi.schemers.org/srfi-128/) | Comparator type, basic-type comparators, simplified compound pair/list/vector comparators, `default-comparator` |
| `(srfi s125 hash-tables)` | [SRFI-125](https://srfi.schemers.org/srfi-125/) | Comparator-keyed "intermediate" hash tables, layered on curry's native eq?/eqv?/equal? table |
| `(srfi s126 hashtables)` | [SRFI-126](https://srfi.schemers.org/srfi-126/) | R6RS-style `hashtable-*` naming, layered on `(srfi s125 hash-tables)` |
| `(srfi s132 sorting)` | [SRFI-132](https://srfi.schemers.org/srfi-132/) | `list-sort`/`vector-sort`(`!`) — every variant is the same stable merge sort |
| `(srfi s133 vectors)` | [SRFI-133](https://srfi.schemers.org/srfi-133/) | Vector extras layered on curry's native vector ops: fold, index/count/any/every, binary-search, concatenate, unfold |
| `(srfi s113 sets-and-bags)` | [SRFI-113](https://srfi.schemers.org/srfi-113/) | Sets — comparator-adapter wrapper over curry's native set; bags — pure-Scheme multiset on a comparator-adapted hash table |
| `(srfi s158 generators-and-accumulators)` | [SRFI-158](https://srfi.schemers.org/srfi-158/) | Generator/accumulator combinators; `make-coroutine-generator` via a real thread — see its section below |
| `(srfi s18 multithreading)` | [SRFI-18](https://srfi.schemers.org/srfi-18/) | Thread/mutex/condition-variable naming over curry's actor `spawn` and `(curry sync)` |
| `(srfi s98 os-environment-variables)` | [SRFI-98](https://srfi.schemers.org/srfi-98/) | `get-environment-variable(s)` — thin re-export of native builtins |
| `(srfi s59 vicinity)` | [SRFI-59](https://srfi.schemers.org/srfi-59/) | Vicinity (directory-of-a-path) string utilities |
| `(srfi s194 random-data-samples)` | [SRFI-194](https://srfi.schemers.org/srfi-194/) | Random integer/real/boolean/char generators plus distribution samplers |

---

## `(srfi s1 lists)` — SRFI-1 list library

```scheme
(import (srfi s1 lists))
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

## `(srfi s27 random-bits)` — SRFI-27 random sources

```scheme
(import (srfi s27 random-bits))
```

All procedures delegate to Curry's built-in SRFI-27 implementation (xoshiro256+ seeded from `/dev/urandom`). They are re-exported here so that portable code using `(srfi s27 random-bits)` works unchanged.

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

## `(srfi s215 log)` — SRFI-215 central log exchange

```scheme
(import (srfi s215 log))
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

## `(srfi s170 posix)` — SRFI-170 POSIX API (subset)

```scheme
(import (srfi s170 posix))
```

Thin re-export of `(curry posix)` under the portable SRFI-170 name — see [`docs/reference/module-posix.md`](module-posix.md) for the full procedure list and the C module's scope (what's implemented vs. deliberately left out for a first pass: `posix-error?` introspection, `open-file`/`fd->port`, `create-fifo`, temp-file helpers, `file-space`, and the directory-generator API). Requires curry built with `-DBUILD_MODULE_POSIX=ON` (the default) — unlike the other `srfi` libraries, this one can't be pure Scheme, since `stat`, `opendir`/`readdir`, `getuid`, `chmod`, `symlink`, `umask`, and `getpid` have no Scheme-level equivalent to build on.

---

## `(srfi s112 environment-inquiry)` — SRFI-112 environment inquiry

```scheme
(import (srfi s112 environment-inquiry))
```

Thin re-export of `(curry posix)`'s six SRFI-112 procedures (`implementation-name`, `implementation-version`, `cpu-architecture`, `machine-name`, `os-name`, `os-version`) — see [`docs/reference/module-posix.md`](module-posix.md#environment-inquiry-srfi-112). Also requires `-DBUILD_MODULE_POSIX=ON` (the default), since `uname(2)`/`gethostname(2)` are syscalls with no Scheme-level equivalent to build on.

---

## `(srfi s238 codesets)` — SRFI-238 codesets

```scheme
(import (srfi s238 codesets))
```

Thin re-export of `(curry codesets)`'s five procedures (`codeset?`, `codeset-symbols`, `codeset-symbol`, `codeset-number`, `codeset-message`) covering the `errno`, `signal`, and `http-status` codesets — see [`docs/reference/module-codesets.md`](module-codesets.md). Requires `-DBUILD_MODULE_CODESETS=ON` (the default) — `errno`/`signal` symbol names and values come from this platform's own `<errno.h>`/`<signal.h>` macros, with no Scheme-level equivalent to build on.

---

## `(srfi s69 hash-tables)` and `(srfi s90 hash-tables)` — SRFI-69 / SRFI-90

```scheme
(import (srfi s69 hash-tables))
(import (srfi s90 hash-tables))   ; layered on s69; import both together
```

Pure Scheme, no C — see [`docs/reference/module-hash-tables-srfi.md`](module-hash-tables-srfi.md) for the full API. Unlike the other `srfi` libraries above, this one isn't a thin re-export of an existing curry primitive: curry's own built-in `hash-table-ref` has different (non-SRFI-69) semantics for its optional third argument, so this library reimplements the single-element accessors correctly in terms of the lower-level builtins, and restricts `make-hash-table`'s equivalence predicate to `eq?`/`eqv?`/`equal?` (curry's underlying table only supports those three comparator modes, not arbitrary predicates).

---

## `(srfi s174 posix-timespecs)` — SRFI-174 POSIX timespecs

```scheme
(import (srfi s174 posix-timespecs))
```

Pure Scheme, no C — see [`docs/reference/module-timespec-srfi.md`](module-timespec-srfi.md) for the full API. A small immutable `(seconds nanoseconds)` time-instant record type, implemented directly with `define-record-type`; no dependency on `(curry posix)` or any other module.

---

## `(srfi s19 time)` — SRFI-19 time/date

```scheme
(import (srfi s19 time))
```

See [`docs/reference/module-time-srfi.md`](module-time-srfi.md) for the full API and its scope (notably: `time-tai` is unsupported — raises rather than giving a plausible-but-wrong answer without a maintained leap-second table; `time-monotonic` is numerically identical to `time-utc`; `current-date` has no local-timezone auto-detection). Requires `-DBUILD_MODULE_POSIX=ON` (the default) for `current-time`/`current-date`; everything else (date arithmetic, Julian Day conversion, formatting/parsing) is pure Scheme.

## `(srfi s8 receive)` — SRFI-8 receive

```scheme
(import (srfi s8 receive))
(receive (q r) (floor/ 7 2) (list q r)) ; => (3 1)
```

A single macro: `(receive formals expression body ...)` binds `expression`'s multiple values to `formals` (as `lambda` formals — fixed, rest, or dotted) around `body`. **Importing this shadows curry's own `receive` special form** (actor mailbox receive — `spawn`/`send!`/`receive`, see `src/actors.c`) for the rest of the importing program's top level; don't import it into code that also uses actor mailboxes.

## `(srfi s145 assume)` — SRFI-145 assume

```scheme
(import (srfi s145 assume))
(assume (> x 0) "x must be positive")
```

`(assume expr message ...)` raises if `expr` is false, otherwise returns unspecified. The SRFI permits eliding the check in production builds; curry always checks it, since there's no separate "production mode" build flag to key elision on. `assume-type` is also provided: `(assume-type obj pred . message)` raises unless `(pred obj)`, otherwise returns `obj`. Unrelated to curry's own `assume!`/`can-assume?` (`src/builtins_curry.c`), which record algebraic assumptions on symbolic CAS variables.

## `(srfi s227 optional-arguments)` — SRFI-227 optional arguments

```scheme
(import (srfi s227 optional-arguments))
(define greet (opt-lambda (name #:optional (greeting "Hello")) (string-append greeting ", " name)))
(greet "Ada")            ; => "Hello, Ada"
(greet "Ada" "Hi")       ; => "Hi, Ada"
```

`opt-lambda`, `let-optionals`, `let-optionals*`, `default-object`, `default-object?`. **Spelled `#:optional`/`#:rest`, not the SRFI's `#!optional`/`#!rest`** — curry's reader treats any `#!` as a shebang-style line comment anywhere in the source (not only at file start), so that literal syntax can't be read at all; `#:keyword` is curry's existing Guile/Racket-style reader syntax and was the closest available substitute. `let-optionals`/`let-optionals*` are the same implementation (sequential binding, later defaults may reference earlier vars) — curry doesn't distinguish the stricter `let-optionals` the reference implementation defines.

## `(srfi s128 comparators)` — SRFI-128 comparators

```scheme
(import (srfi s128 comparators))
(<? string-comparator "a" "b" "c")        ; => #t
(=? (make-default-comparator) '(1 2) '(1 2)) ; => #t
```

Full comparator protocol (`make-comparator`, `comparator?`, `comparator-ordered?`/`-hashable?`, the four accessor procedures, `comparator-test-type`/`-check-type`/`-hash`, the variadic `=?`/`<?`/`>?`/`<=?`/`>=?`), plus basic-type comparators (`boolean-`, `real-`/`number-`, `char-`, `char-ci-`, `string-`, `string-ci-`, `symbol-`, `eq-`, `eqv-`, `equal-comparator`) and a `default-comparator`/`comparator-register-default!`/`make-default-comparator` registry that orders across types (booleans < numbers < chars < strings < symbols < pairs < vectors). `pair-comparator`, `list-comparator`, and `vector-comparator` are simplified relative to the full SRFI: each takes a single element comparator rather than the SRFI's fuller custom type-test/car/cdr/null? parameterization for non-pair "list-like" structures.

## `(srfi s125 hash-tables)` and `(srfi s126 hashtables)` — SRFI-125 / SRFI-126

```scheme
(import (srfi s128 comparators) (srfi s125 hash-tables))
(define t (make-hash-table string-comparator))
(hash-table-set! t "a" 1)
(hash-table-ref t "a" (lambda () 'missing))  ; => 1
```

s125 is the comparator-keyed "intermediate" hash-table API (`make-hash-table`, `hash-table`, `hash-table-unfold`, `hash-table-ref`/`-ref/default`/`-intern!`/`-update!`(`/default`), `-contains?`/`-empty?`/`-size`/`-clear!`/`-copy`/`-empty-copy`, `-keys`/`-values`/`-entries`/`->alist`, `-walk`/`-for-each`/`-map->list`/`-fold`/`-map!`/`-prune!`, `-union!`/`-intersection!`/`-difference!`). s126 layers R6RS `hashtable-*` naming (`make-eq-hashtable`, `make-eqv-hashtable`, `make-hashtable`, `hashtable-ref`/`-set!`/`-update!`/`-delete!`/`-contains?`/`-copy`/`-clear!`/`-size`/`-keys`/`-values`/`-entries`/`->alist`/`-walk`, `equal-hash`/`string-hash`/`symbol-hash`) on top of s125.

Both are bucketed under curry's three native equivalence modes (eq?/eqv?/equal?, from `src/set.h`) exactly as `(srfi s69 hash-tables)` already does: `eq-comparator`/`eqv-comparator` map to the matching native mode; every other comparator (including `string-comparator`, a custom comparator whose equality happens to be `equal?`-compatible, or `default-comparator`) is bucketed under native `equal?` mode. This is exact whenever the comparator's equality predicate agrees with `equal?` — true for every comparator this library ships and for the overwhelming majority of user-defined ones.

## `(srfi s132 sorting)` and `(srfi s133 vectors)` — SRFI-132 / SRFI-133

```scheme
(import (srfi s132 sorting) (srfi s133 vectors))
(list-sort < '(3 1 2))              ; => (1 2 3)
(vector-fold + 0 #(1 2 3 4))        ; => 10
(vector-binary-search #(1 3 5 7 9) 5 <) ; => 2
```

s132: `list-sorted?`/`vector-sorted?`, `list-sort`/`list-stable-sort`(`!`), `vector-sort`/`vector-stable-sort`(`!`), `list-merge`(`!`), `vector-merge`(`!`). Every sort is the same stable merge sort — the SRFI permits `list-sort`/`vector-sort` to be unstable, but there's no benefit to a second, less-predictable algorithm, so the "plain" and "stable" names are aliases. `vector-find-median`/`vector-select!`/`vector-separate!` from the full SRFI are out of scope.

s133 adds, on top of curry's native R7RS vector ops (`make-vector`, `vector-copy`(`!`), `vector-append`, `vector-map`/`-for-each`, etc.): `vector-empty?`, `vector=`, `vector-swap!`, `reverse!`/`vector-reverse!`(`*`), `vector-index`(`-right`), `vector-count`, `vector-any`/`-every`, `vector-fold`(`-right`), `vector-binary-search`, `vector-concatenate`, `vector-unfold`(`-right`).

## `(srfi s113 sets-and-bags)` — SRFI-113 sets and bags

```scheme
(import (srfi s128 comparators) (srfi s113 sets-and-bags))
(define s (set string-comparator "a" "b" "c"))
(set-size (set-union s (set string-comparator "b" "d")))  ; => 4
```

Sets are a comparator-adapter wrapper over curry's native set (`src/set.h`) — bucketed under eq?/eqv?/equal? exactly like s125/s126 above, and reusing several native primitive *names* (`set-union`, `set-copy`, `set=?`, `set-add!`, …) for this library's differently-shaped SRFI-113 API (variadic where the SRFI wants variadic, and tracking a comparator per set for `set-comparator`/`list->set`/etc). Bags (multisets) are pure Scheme, built on a comparator-adapted hash table mapping element → count: `bag`, `bag-element-count`, `bag-adjoin!`/`bag-delete!`, `bag-union`/`bag-sum`/`bag-intersection`(`!` variants where noted), `bag=?`, and the usual `-map`/`-filter`/`-fold`/`-for-each`/`-any?`/`-every?`/`-count`. `bag-search!` and the full SRFI's mutable/immutable distinction beyond the `!` suffix convention are out of scope.

## `(srfi s158 generators-and-accumulators)` — SRFI-158

```scheme
(import (srfi s158 generators-and-accumulators))
(generator->list (gtake (make-range-generator 0) 5))  ; => (0 1 2 3 4)

(define g (make-coroutine-generator
           (lambda (yield) (yield 1) (yield 2) (yield 3))))
(list (g) (g) (g) (eof-object? (g)))  ; => (1 2 3 #t)
```

A generator is a thunk returning successive values and the eof-object once exhausted — every combinator here builds directly on that (`list->generator`, `vector->generator`, `make-range-generator`, `make-iota-generator`, `circular-generator`, `generator->list`/`-vector`/`-string`, `gtake`/`gdrop`/`gappend`/`gmap`/`gfilter`/`gremove`/`gzip`/`gflatten`, `generator-fold`/`-for-each`/`-count`/`-any`/`-every`/`-find`) plus accumulators (`make-accumulator`, `count-`/`list-`/`reverse-list-`/`sum-`/`product-`/`vector-accumulator`).

`make-coroutine-generator` is the one exception: turning an arbitrary producer procedure (that calls a `yield`) into a generator genuinely needs suspend/resume, which curry's escape-only continuations (`setjmp`/`longjmp`, no CPS — see the R7RS compliance gaps in the top-level `CLAUDE.md`) can't provide. It's implemented instead on a real OS thread (curry's actor `spawn`) handed off through a `(curry sync)` mutex/condvar rendezvous, one value at a time — `make-for-each-generator` is built on top of it the same way.

## `(srfi s18 multithreading)` — SRFI-18

```scheme
(import (srfi s18 multithreading))
(define t (thread-start! (make-thread (lambda () (+ 1 2 3)))))
(thread-join! t)  ; => 6
```

curry's only real-thread primitive is the actor system (`spawn`/`send!`/`receive`); `(curry sync)`'s own header comment says as much and intentionally doesn't expose `pthread_create`. SRFI-18 threads are a thin wrapper: `make-thread` stores a thunk (SRFI-18 threads are created suspended); `thread-start!` is what actually calls `spawn`. `thread-join!` and `thread-sleep!` both ride a private mutex/condvar rendezvous — `thread-sleep!` in particular is just a `cond-wait-timeout!` that nothing ever signals, which `pthread_cond_timedwait` treats as a plain timed sleep. `mutex-lock!`/`-unlock!`, `make-condition-variable`, `condition-variable-signal!`/`-broadcast!` are re-exports of `(curry sync)`'s own procedures. `thread-terminate!` always raises — curry's actor threads are detached pthreads with no cancellation hook. `current-thread` returns `#f` (no reified handle for the calling thread).

## `(srfi s98 os-environment-variables)` — SRFI-98

```scheme
(import (srfi s98 os-environment-variables))
(get-environment-variable "HOME")
```

A thin re-export: `get-environment-variable`/`get-environment-variables` are already native builtins matching SRFI-98/R7RS `(scheme process-context)` exactly.

## `(srfi s59 vicinity)` — SRFI-59 vicinity

```scheme
(import (srfi s59 vicinity))
(pathname->vicinity "/a/b/c.scm")  ; => "/a/b/"
(program-vicinity)                 ; directory of the running script
```

`program-vicinity`, `library-vicinity`, `implementation-vicinity`, `user-vicinity`, `home-vicinity`, `in-vicinity`, `sub-vicinity`, `make-vicinity`, `pathname->vicinity`/`->vicinity`, `vicinity:suffix?`, `->namestring`. Unix-only, matching curry's own Linux/macOS build targets: the directory separator is always `#\/`. `program-vicinity` derives from `(command-line)`'s first element (the invoked script path); `library-vicinity` and `implementation-vicinity` are best-effort fallbacks to the same, since neither `CURRY_MODULE_PATH` nor curry's install prefix is introspectable from Scheme; `home-vicinity` reads `$HOME`.

## `(srfi s194 random-data-samples)` — SRFI-194

```scheme
(import (srfi s194 random-data-samples))
((make-random-integer-generator 0 10))
((make-normal-generator 0.0 1.0))
```

Generators follow `(srfi s158)`'s thunk protocol (these are infinite streams, so there's no dependency on that library for such a small surface). `make-random-integer-generator`, `make-random-real-generator`, `make-random-boolean-generator`, `make-random-char-generator`, and `make-uniform-`/`make-normal-`/`make-exponential-generator` delegate to `(curry random)`'s samplers. `make-bernoulli-`/`make-binomial-`/`make-geometric-`/`make-poisson-`/`make-categorical-generator` are implemented directly here, since `(curry random)` intentionally omits discrete distributions in favor of `T_QUANTUM`. `make-zipf-generator` and the geometry samplers (sphere/ball/rectangle) from the full SRFI are out of scope.

## Portability note

Code written against `(srfi s1 lists)`, `(srfi s27 random-bits)`, `(srfi s215 log)`, `(srfi s170 posix)`, `(srfi s112 environment-inquiry)`, `(srfi s238 codesets)`, `(srfi s69 hash-tables)`, `(srfi s90 hash-tables)`, `(srfi s174 posix-timespecs)`, `(srfi s19 time)`, `(srfi s8 receive)`, `(srfi s145 assume)`, `(srfi s128 comparators)`, `(srfi s125 hash-tables)`, `(srfi s126 hashtables)`, `(srfi s132 sorting)`, `(srfi s133 vectors)`, `(srfi s113 sets-and-bags)`, `(srfi s158 generators-and-accumulators)` (aside from `make-coroutine-generator`'s real-thread implementation, an implementation detail invisible from the API), `(srfi s18 multithreading)`, `(srfi s98 os-environment-variables)`, and `(srfi s194 random-data-samples)` is compatible with Guile, Chicken (via the `srfi-egg`), Chibi-Scheme, and other implementations that follow the same naming convention. The only two deviations are `(srfi s27 random-bits)`, which uses xoshiro256+ internally rather than the Mersenne Twister typically found in other implementations (statistically equivalent or better), and `(srfi s227 optional-arguments)`, which spells its markers `#:optional`/`#:rest` rather than `#!optional`/`#!rest` because curry's reader can't read the latter (see that library's section above).
