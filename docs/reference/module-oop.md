# Module: `(curry oop)`

*v1.11.0 — 2026-07-26*

Slim CLOS: classes, generic functions, and multiple dispatch, as a pure-Scheme
library (`lib/curry/modules/curry/oop.scm`, no C code required to use it — see
[Performance](#performance) below for the parts that *are* backed by C).
Design rationale, alternatives considered, and what's deliberately left out
are in `docs/thoughts/oop.md`; this page documents the API as built.

For worked, runnable examples — extending the numeric tower, driving Qt6
widgets, and wrapping actors in an object interface — see
[Using the object system](../guides/oop-guide.md).

## Import

```scheme
(import (curry oop))
```

## Why this instead of `define-record-type`?

`define-record-type` (R7RS, always available) gives you a flat struct: a
constructor, a predicate, and accessors/mutators. There is no inheritance, no
dispatch, and no way to make an *existing* operation — `+`, `distance`,
`describe` — do something different for your new type without editing every
caller. `(curry oop)` adds exactly that on top, and existing records keep
working unchanged; nothing here requires migrating away from
`define-record-type` for code that doesn't need dispatch.

## Classes

```scheme
(define-class <point> ()
  (x #:init 0 #:accessor point-x)
  (y #:init 0 #:accessor point-y))

(define-class <point-3d> (<point>)        ; single inheritance
  (z #:init 0 #:accessor point-z))
```

`()` after the class name means "direct subclass of `<object>`", the root of
the hierarchy. Multiple inheritance is supported — `(define-class <d> (<b>
<c>) ...)` — and method resolution order follows C3 linearization (the same
algorithm Python, Dylan, and Raku use), not naive left-to-right depth-first
search, so it stays well-defined even with diamond inheritance:

```scheme
(define-class <a> ())
(define-class <b> (<a>))
(define-class <c> (<a>))
(define-class <d> (<b> <c>))

(class-precedence-list <d>)   ; => (<d> <b> <c> <a> <object>)
```

A subclass redeclaring an ancestor's slot name overrides it — the subclass's
`#:init`/`#:mutable`/`#:accessor` win, not the ancestor's.

### Slot options

| Option | Meaning |
|---|---|
| `#:init expr` | Default value if not supplied to `make`. Re-evaluated fresh for every instance (like CLOS's `:initform`) — `#:init (make-hash-table)` gives each instance its own table, not one shared table. |
| `#:accessor name` | Generates a reader procedure `(name obj)` — sugar for `(slot-ref obj 'slot-name)`. |
| `#:mutable` | Allows `slot-set!` on this slot. **Slots are immutable by default** — see below. |
| `#:type cls` | Parsed, stored nowhere, not enforced. Documentation only in this version. |

### Immutable by default

This is the one deliberate departure from standard CLOS. A slot can only be
changed with `slot-set!` if declared `#:mutable`:

```scheme
(define-class <point> ()
  (x #:init 0 #:accessor point-x))          ; immutable
(define-class <counter> ()
  (n #:init 0 #:mutable))                    ; mutable

(define p (make <point> #:x 5))
(slot-set! p 'x 10)      ; error: slot is immutable

(define c (make <counter>))
(slot-set! c 'n 1)        ; ok
```

The reasoning: an object whose state can be accidentally mutated is a worse
simulation primitive for curry's physics/CAS domain than one that can't, and
immutable values share freely across actors with no synchronization needed.
Mutable slots remain available — declare `#:mutable` — for the ordinary cases
where mutation is the point (counters, caches, hardware handles).

## Instances

```scheme
(define p (make <point> #:x 3 #:y 4))

(slot-ref p 'x)              ; => 3
(slot-set! p 'x 10)          ; error unless the slot is #:mutable
(point-x p)                  ; => 3, via the generated accessor
```

`make` takes the class followed by `#:keyword value ...` pairs matching slot
names. A slot with neither a supplied value nor `#:init` raises on
`slot-ref` if read before being set.

## Generic functions and methods

```scheme
(define-generic distance (a b))

(define-method distance ((a <point>) (b <point>))
  (sqrt (+ (square (- (point-x b) (point-x a)))
           (square (- (point-y b) (point-y a))))))

(define-method distance ((a <number>) (b <number>))
  (abs (- b a)))
```

Every parameter in a `define-method` argument list must be written as
`(name specializer-class)` — there's no unspecialized-parameter shorthand;
write `(x <object>)` for a parameter that should match anything (see
[Built-in type hierarchy](#built-in-type-hierarchy) for why `<object>` is the
right choice there, not omitting the specializer).

**Multiple dispatch is real**: all argument positions participate in method
selection, and specificity is resolved position-by-position using each
argument's own class-precedence-list, most-significant argument first. A
method specializing `(distance <point-3d> <point>)` is more specific than one
specializing `(distance <point> <point>)` and wins when both apply.

### `call-next-method`

```scheme
(define-method describe ((x <object>)) "an object")
(define-method describe ((x <point>))
  (string-append "point: " (call-next-method)))

(describe (make <point>))   ; => "point: an object"
```

Invokes the next-most-specific applicable method (including the fallback, if
any — see below) in the resolution chain. There is no method combination —
no `:before`/`:after`/`:around` qualifiers, no metaobject protocol. This is a
deliberate scope decision (see `docs/thoughts/oop.md`), not an oversight;
`call-next-method` in primary methods covers the overwhelming majority of
real use.

### Redefinition

Re-evaluating a `define-method` with the same specializers *replaces* the
existing method — it does not add a second, shadowed one. This makes the
ordinary REPL/script-reload workflow do what you'd expect: reload a file with
an updated method body, and the new body is what runs.

## Extending an existing procedure

`define-generic`/`define-method` on a name that's already bound to an
ordinary procedure — including a builtin like `+`, `sqrt`, or `abs` —
*promotes* that binding into a generic function whose fallback is the
original procedure:

```scheme
(define-class <poly> ()
  (coeffs #:init '() #:accessor poly-coeffs))

(define-method + ((a <poly>) (b <poly>))
  (make <poly> #:coeffs (map + (poly-coeffs a) (poly-coeffs b))))

(+ (make <poly> #:coeffs '(1 2)) (make <poly> #:coeffs '(3 4)))
; => a <poly> with coeffs (4 6)

(+ 1 2)   ; => 3, unaffected — falls through to the original + when no
          ;    user-defined method's specializers match
```

User-defined methods are tried first (most specific wins, as always); if none
apply, the original procedure runs exactly as it did before any method was
ever added. This is how `simplify`, `∂`, or any operator in the numeric
tower can be made to understand a new algebraic type without touching a
single line of C — see the [numeric tower example](../guides/oop-guide.md#extending-the-numeric-tower)
in the usage guide.

Calling `define-method` on a name bound to a non-procedure value (a plain
number, a string, ...) raises rather than silently clobbering it.

## Introspection

```scheme
(is-a? p <point>)                        ; => #t, respects inheritance
(class-of p)                             ; => <point>
(subclass? <point-3d> <point>)           ; => #t
(class-name <point>)                     ; => '<point>
(class-slots <point-3d>)                 ; => '(z x y) — own slots first
(class-precedence-list <point-3d>)       ; => (<point-3d> <point> <object>)
```

## Built-in type hierarchy

Every plain Curry value has a class, so generic functions dispatch on
built-in types with no wrapper:

```
<object>
  <number>
    <integer>          ; exact-integer? — fixnum/bignum are not
    <inexact-real>     ;   distinguished at the Scheme level, see below
  <quaternion> <octonion> <multivector> <surreal> <symbolic> <quantum>
  <tuple>
    <up-tuple> <down-tuple>
  <boolean> <pair> <null> <vector> <bytevector> <string> <symbol> <char>
  <procedure> <port> <actor> <promise> <hash-table> <set>
```

`(class-of 5)` is `<integer>`; `(class-of 3.14)` is `<inexact-real>`;
anything not matched by a more specific predicate is `<object>`. There is no
`<fixnum>`/`<bignum>`/`<flonum>` split — Curry has no Scheme-visible
`fixnum?`/`bignum?`/`flonum?` predicates at all (that distinction exists only
as a C-level `val_t` tag inside `src/numeric.c`), so both exact-integer
representations collapse into `<integer>` and both inexact representations
collapse into `<inexact-real>` here.

### The `<object>`-specializer rule

A method specializing every parameter as `<object>` always applies — it's
the generic-programming equivalent of a default case, and it can outrank a
promoted builtin's fallback (see [Performance](#performance) for why this
matters beyond just correctness):

```scheme
(define-method sqrt ((x <object>)) 'not-really-a-square-root)
(sqrt 4)   ; => 'not-really-a-square-root, NOT 2.0 — the <object> method
           ;    is more specific than "no method at all", so it wins
```

This is intentional and matches ordinary CLOS specificity rules — `<object>`
is a real class in the hierarchy, not a signal meaning "ignore this
parameter." If you want a method that only fires for genuinely unknown
types, specialize on the actual classes you care about instead.

## Performance

Three implementation layers, all transparent to the API above:

- **Layer 1** — the mechanism described in this page: a method-table lookup
  per generic function, filtered by `is-a?` and sorted by specificity on
  every call. This is the whole story for application-level code; the
  overhead is not worth worrying about outside a hot numeric loop.
- **Layer 2** — each generic function's dispatch closure carries a small (4-
  entry) polymorphic inline cache, implemented in C (`src/pic.c`). A repeat
  call with argument types seen recently skips straight to the cached,
  already-sorted dispatch chain — no `is-a?`/class-precedence-list walk. The
  cache is invalidated automatically whenever `define-method` changes the
  generic's method table. It lives on the *generic function*, not the call
  site, specifically so it isn't silently bypassed once curry's JIT compiles
  a hot caller to native code — see `docs/thoughts/oop.md`'s Layer 2 section
  for why a call-site cache would have gone cold exactly when it mattered
  most, and why every generic-function dispatcher is deliberately pinned to
  never itself be JIT-compiled (`jit-never!`).
- **Layer 3** — once a builtin like `+` has been extended for a user-defined
  type, a call whose argument types could not possibly match any
  user-defined method (e.g. `(+ 1 2)` after extending `+` for `<poly>`)
  skips the class-tuple construction and the Layer 2 cache lookup entirely,
  going straight to the original C primitive. This is why the promoted-`+`
  example above imposes no measurable cost on ordinary numeric code — the
  `<object>`-specializer rule exists specifically so this fast path can be
  proven safe: it's disabled automatically for any generic where a method
  could always apply regardless of argument type.

None of this changes what any call returns — Layers 2 and 3 are pure
optimizations over Layer 1's semantics, and the full test suite
(`tests/oop_tests.scm`) exercises the same assertions with the cache cold,
warm, and invalidated to confirm that.

## What this deliberately does not build

Per `docs/thoughts/oop.md`'s own recommendation, evaluated and left out on
purpose rather than not-yet-implemented:

- **No metaobject protocol.** No `compute-applicable-methods`,
  `compute-effective-method`, metaclasses, or `allocate-instance` hooks.
- **No method-combination qualifiers.** No `:before`/`:after`/`:around`;
  `call-next-method` in primary methods only.
- **No logic-valued/fuzzy dispatch.** A separate, more radical design
  (`docs/thoughts/object-system.md`, "Actors All the Way Down" — every
  instance is literally its own actor) explored this along with a full MOP;
  it was not the design chosen. Nothing in that document is implemented.

These can be added later with demonstrated need. See
`docs/thoughts/oop.md`'s "Open questions" for the reasoning.
