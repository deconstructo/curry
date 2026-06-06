# SRFI Compatibility: `(surfage sN …)`

*v1.1.1 — 2026-06-06*

Curry ships a set of pure-Scheme SRFI compatibility libraries under the `(surfage sN name)` namespace — a naming convention used by several Scheme implementations so that the same `(import …)` line works across them. The libraries delegate to Curry's built-ins wherever the procedure is already present; only missing names are defined in Scheme.

## Available libraries

| Library | SRFI | Description |
|---------|------|-------------|
| `(surfage s1 lists)` | [SRFI-1](https://srfi.schemers.org/srfi-1/) | List library |
| `(surfage s27 random-bits)` | [SRFI-27](https://srfi.schemers.org/srfi-27/) | Random-number sources |

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

## Portability note

Code written against `(surfage s1 lists)` and `(surfage s27 random-bits)` is compatible with Guile, Chicken (via the `surfage-egg`), Chibi-Scheme, and other implementations that follow the same naming convention. The only difference is that Curry's `(surfage s27 random-bits)` uses xoshiro256+ internally rather than the Mersenne Twister typically found in other implementations; the statistical properties are equivalent or better.
