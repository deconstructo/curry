# `(surfage s69 hash-tables)` and `(surfage s90 hash-tables)`

*unreleased*

Pure Scheme, no C — wrappers around curry's built-in hash table primitives providing the [SRFI-69](https://srfi.schemers.org/srfi-69/) API and, on top of it, [SRFI-90](https://srfi.schemers.org/srfi-90/srfi-90.html)'s single `make-table` constructor.

## Import

```scheme
(import (surfage s69 hash-tables))
(import (surfage s90 hash-tables))   ; layered on s69; import both together
```

## Why a wrapper, not just the builtins?

curry's global `make-hash-table`/`hash-table-ref` (used directly, without this library) are close to but not the same as SRFI-69:

- **`hash-table-ref`**: curry's built-in treats a 3rd argument as a plain *default value* returned as-is on a miss (`#f` if omitted) — SRFI-69 requires it to be a *thunk* called on a miss, and requires an error if the key is absent **and** no thunk was given. This library's `hash-table-ref` implements the correct SRFI-69 semantics; curry's own default-value behavior is available here under its SRFI-69 name, `hash-table-ref/default`.
- **`make-hash-table`**: SRFI-69 accepts an arbitrary equivalence predicate. curry's underlying table only supports three fixed comparator modes (`eq?`/`eqv?`/`equal?`, matching how `(curry sets)` works) — this library accepts exactly `eq?`, `eqv?`, `equal?`, or no argument (defaulting to `equal?`), and raises a clear error for any other predicate rather than silently hashing keys under the wrong equivalence.

## Procedures (SRFI-69)

### Constructors / predicates

- `(make-hash-table [equal?])` — `equal?` must be `eq?`, `eqv?`, `equal?`, or omitted.
- `(hash-table? x)`
- `(alist->hash-table alist [equal?])`

### Reflective queries

- `(hash-table-equivalence-function t)` — returns `eq?`/`eqv?`/`equal?`, whichever the table was created with. Only tracked for tables created through *this* library's `make-hash-table`/`alist->hash-table`/`make-table`; a table created directly via curry's raw builtin reports `equal?` by default.
- `(hash-table-hash-function t)` — returns `hash-by-identity` for an `eq?`-comparator table, `hash` otherwise.

Comparator tracking works by recording each table created through this library's `make-hash-table`/`alist->hash-table`/`make-table` in an internal side table, keyed by the table object itself — entries are never removed, so a program that creates and discards a very large number of hash tables over a long run will keep each discarded table's registry entry (and therefore the table itself) alive indefinitely. Not a concern for normal usage (most programs create a bounded number of long-lived tables), but worth knowing if you're dynamically creating many short-lived tables in a loop.

### Single-element operations

- `(hash-table-ref t key [thunk])` — see above; errors on a miss with no thunk.
- `(hash-table-ref/default t key default)`
- `(hash-table-set! t key value)`
- `(hash-table-delete! t key)`
- `(hash-table-exists? t key)`
- `(hash-table-update! t key proc [thunk])` — `(hash-table-set! t key (proc (hash-table-ref t key [thunk])))`.
- `(hash-table-update!/default t key proc default)`

### Whole-table operations

- `(hash-table-size t)`, `(hash-table-keys t)`, `(hash-table-values t)`, `(hash-table->alist t)`
- `(hash-table-walk t proc)` — calls `(proc key value)` for each entry.
- `(hash-table-fold t f init)` — `(f key value acc)`, left fold.
- `(hash-table-copy t)` — an independent table with the same comparator and entries.
- `(hash-table-merge! t1 t2)` — destructively copies every entry of `t2` into `t1`; returns `t1`.

### Hash functions

- `(hash obj [bound])`, `(string-hash s [bound])`, `(string-ci-hash s [bound])`, `(hash-by-identity obj [bound])`

All four return a non-negative exact integer less than `bound` (default 2³⁰). Implemented as a pure-Scheme polynomial rolling hash — `hash`/`hash-by-identity` hash the object's printed (`write`) representation, so two `equal?` values always hash equally, but the specific numbers won't match any other Scheme implementation's hash values (the SRFI doesn't require that). `hash-by-identity` doesn't hash by true pointer identity (no such primitive is exposed at the Scheme level in curry) — it falls back to the same content hash as `hash`, which is a valid (if not maximally efficient) implementation per the SRFI.

```scheme
(import (surfage s69 hash-tables))

(define t (make-hash-table))
(hash-table-set! t 'name "Alice")
(hash-table-ref t 'name)                          ; => "Alice"
(hash-table-ref t 'missing (lambda () 'nope))      ; => nope
(hash-table-ref t 'missing)                        ; error: key not found

(define counts (make-hash-table))
(for-each
  (lambda (word) (hash-table-update!/default counts word (lambda (n) (+ n 1)) 0))
  '("a" "b" "a" "c" "a"))
(hash-table->alist counts)  ; => (("a" . 3) ("b" . 1) ("c" . 1))  [order unspecified]
```

## `make-table` (SRFI-90)

```scheme
(import (surfage s90 hash-tables) (surfage s69 hash-tables))
```

A single constructor, `make-table`, using Gambit-style colon-suffixed keyword markers (`test:`, `hash:`, `size:`, `min-load:`, `max-load:`, `weak-keys:`, `weak-values:`). Gambit's reader treats a colon-suffixed identifier as a self-evaluating keyword object; curry has no such reader feature, so **the markers must be quoted** at the call site — `(make-table 'test: eq?)`, not the bare `(make-table test: eq?)` shown in the SRFI text.

Only `test:` affects behavior (subject to the same `eq?`/`eqv?`/`equal?` restriction as `make-hash-table`, above). `hash:` is accepted but has no effect (curry's tables don't take a caller-supplied hash function). `size:`/`min-load:`/`max-load:`/`weak-keys:`/`weak-values:` are purely advisory per the SRFI itself ("implementations may ignore" them) and are parsed and discarded.

```scheme
(define t (make-table 'test: eq? 'size: 64))
(hash-table-set! t 'k 'v)
```
