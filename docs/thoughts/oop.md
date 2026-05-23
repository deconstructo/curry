# Object-Oriented Model for Curry Scheme

*Draft — 2026-05-23*

A design sketch for adding a first-class OOP layer to Curry. Nothing here is implemented yet. The intent is to nail down the model before writing any code.

---

## Why OOP, and why now?

Curry already has powerful tools for abstraction: closures, actors, the numeric tower, the CAS. What it lacks is a systematic way to define new types that participate in generic operations — types that can be passed to `+`, `print`, `distance`, `simplify`, or any user-defined operation without the caller needing to know the concrete type.

The motivating use cases:

- **Physics / SICM**: a `<rigid-body>` type that participates in `Lagrange-equations` without special-casing; a `<manifold>` type for differential geometry.
- **CAS extension**: user-defined algebraic structures (groups, rings, modules) that plug into `+`, `*`, `simplify`.
- **Hardware abstraction**: `<i2c-device>`, `<spi-device>` as typed handles with methods rather than opaque integers.
- **UI components**: `<widget>` hierarchy that can be extended without modifying the Qt6 module.

---

## Evaluating the options

### Option 1 — Closure-based message-passing (SICP style)

```scheme
(define (make-point x y)
  (lambda (msg . args)
    (case msg
      ((x) x)
      ((distance) (let ((b (car args)))
                    (sqrt (+ (square (- (b 'x) x)) ...)))))))
```

**Pros:** Zero infrastructure, pure Scheme, teaches the pattern beautifully.

**Cons:** Disqualified by the requirements. You cannot dispatch `(+ polynomial symbolic)` on both argument types without embedding the dispatch table inside every object, and the method bodies are invisible to any tool or documentation system. Not viable.

---

### Option 2 — Single dispatch (Python / Ruby / Smalltalk style)

One designated receiver argument drives method lookup; all other arguments are untyped.

**Pros:** Familiar, fast, straightforward to implement, excellent tooling story (introspection, reflection, autocomplete).

**Cons — and this needs to be specific, not just dismissed:**

The real problem for Curry is that the motivating operations are all multi-argument. `(inner-product u v)` on a manifold, `(+ polynomial symbolic)`, `(Lagrange-equations L)` where `L` is itself a typed structure — none of these fit cleanly into "receiver.method(arg)". You end up writing double-dispatch by hand for every cross-type operation, which is exactly what generic functions solve.

*If* the use cases were primarily hardware abstraction and UI — typed handles, event handlers, widget hierarchies — single dispatch would be fine and considerably simpler. The deciding factor is the CAS and SICM. Those domains require multi-argument dispatch as a first-class primitive.

---

### Option 3 — Full CLOS

Generic functions with multiple dispatch, C3 linearisation across an inheritance hierarchy, method combination qualifiers (`:before` / `:after` / `:around` / `call-next-method`), and an optional metaobject protocol (MOP).

**Pros:** The theoretically correct answer for the use cases. scmutils — the original system behind SICM — is built on CLOS. The numeric tower already does implicit CLOS-style dispatch at the C level; surfacing this at the Scheme level is a natural move.

**Cons — and these are real:**

- Method combination (`:before`, `:after`, `:around`) is almost never used in practice. In years of Common Lisp production code it shows up mainly in frameworks, not user code. The complexity is real; the payoff is rare.
- The full metaobject protocol is enormous. *The Art of the Metaobject Protocol* is a book. Metaclasses, `compute-applicable-methods`, `make-method-lambda` — you almost certainly don't want to build or maintain this.
- The MOP creates a genuine bootstrapping problem: you need classes to define classes.
- Debugging CLOS dispatch failures is unpleasant. Error messages are deep and hard to trace.

The core dispatch model is sound. The method combination and MOP are the parts that should not be built.

---

### Option 4 — Slim CLOS (no MOP, no method qualifiers)

This is what TinyCLOS (Gregor Kiczales, ~600 lines of Scheme) implements, and what Swindle refined for PLT Scheme. The core:

- `define-class` with single or multiple inheritance
- `define-generic` / `define-method` with multiple dispatch
- `make`, `slot-ref`, `slot-set!`, `is-a?`, `class-of`
- C3 linearisation for the method resolution order
- `call-next-method` in primary methods
- **No MOP. No method combination qualifiers.**

This covers 98% of real use cases. Method combination can be added later if there is demonstrated need; leaving it out now costs nothing.

---

### Option 5 — Clojure protocols

Separate the data definition from the dispatch mechanism entirely:

```scheme
(define-record-type <point> ...)      ; data only

(define-protocol Metric               ; named interface
  (distance a b)
  (norm a))

(extend-type <point> Metric           ; attach implementation to type
  (distance (a b) ...)
  (norm (a) ...))
```

**Pros:** Very clean separation of concerns. Easily toolable — you can enumerate all implementations of a protocol. Simpler dispatch than CLOS. Easier to optimise (one dispatch table per protocol per type). This is what Clojure converged on after years of production use.

**Cons:** Protocols are single-dispatch. `(distance <point-3d> <quaternion>)` dispatches on the first argument only. For Curry's multi-argument operations — any place in the CAS or SICM where the operation depends on the types of two or more arguments — this is a fundamental limitation. You would need a separate multi-method mechanism alongside protocols, making the system a partial solution.

Protocols are an excellent complement to generic functions. They are not a replacement.

---

### Option 6 — Julia style: structs + pure multiple dispatch, no class hierarchy

Julia separates "having data" from "being dispatchable":

```scheme
(define-abstract <metric-space>)              ; dispatch anchor, no slots
(define-struct <point> (<metric-space>)       ; concrete, inherits dispatch tag
  (x y))

(define-method distance ((a <metric-space>) (b <metric-space>)) ...)
```

Structs carry data. Abstract types exist only for dispatch. Methods live on functions, never on types. There is no concept of "a method belonging to a class." Field inheritance is not permitted — only dispatch inheritance.

**Pros:** Very clean for mathematical code. No object identity confusion. Methods are always global and composable. Fits Curry's functional orientation — values are immutable data, functions dispatch generically. Julia chose this model after careful thought about scientific computing, which is precisely Curry's target domain.

**Cons:** "No field inheritance" surprises people who come from class-based OOP. Abstract types without slots feel slightly alien in Scheme. The model is less familiar to the Scheme community than a CLOS-derived one.

---

## Recommendation: Slim CLOS, informed by Julia's field/method separation

The conclusion across the options:

- Closure-based message-passing: too limited.
- Single dispatch: wrong fit for multi-argument operations.
- Full CLOS: the dispatch core is right; the MOP and method combination are complexity that should not be built.
- Protocols: excellent complement, not a full solution.
- Julia-style: the cleanest design for scientific computing, but the "no field inheritance" constraint may be unnecessarily strict.

The right design is **Slim CLOS with no MOP and no method combination qualifiers**, with one non-obvious choice borrowed from Julia and functional Scheme style: **slots are immutable by default**.

### Immutable slots by default

Standard CLOS uses mutable slots. This document proposes the opposite: slots are read-only after `make` unless declared `#:mutable`.

The reason is specific to Curry's domain. A `<rigid-body>` or `<coordinate-system>` whose state can be accidentally mutated is a worse simulation primitive than one that cannot. Immutable objects can be freely shared across actors without synchronisation. The functional style of CAS expressions (build a new expression tree, don't patch the old one) generalises cleanly to user-defined types.

Mutable slots remain available for hardware handles and UI state where mutation is the point.

### What is *not* being built

- No metaobject protocol.
- No method combination qualifiers (`:before`, `:after`, `:around`). `call-next-method` in primary methods only.
- No metaclasses.

These can be added later with demonstrated need. Leaving them out now is not a limitation — it is a decision.

### Note on scmutils compatibility

If Curry's primary users are physicists who know scmutils, they expect CLOS and expect it to behave like CLOS including method combination. In that case, following scmutils more closely may outweigh elegance. This is worth revisiting once there are real users writing real physics code.

---

## API sketch

### Classes

```scheme
; Define a class.  Superclasses default to (<object>) if omitted.
(define-class <point> ()
  (x #:init 0 #:type <real>)
  (y #:init 0 #:type <real>))

; Inheritance — single or multiple.
(define-class <point-3d> (<point>)
  (z #:init 0 #:type <real>))

; Construction via keyword arguments.
(define p (make <point> #:x 3 #:y 4))

; Slot access.
(slot-ref  p 'x)          ; → 3
(slot-set! p 'x 10)       ; error unless slot declared #:mutable

; Generated accessors (optional, declared in define-class).
(define-class <point> ()
  (x #:init 0 #:accessor point-x)
  (y #:init 0 #:accessor point-y))

(point-x p)               ; → 3
```

### Generic functions and methods

```scheme
; Declare a generic function.
(define-generic distance (a b))

; Add a method specialised on two <point> arguments.
(define-method distance ((a <point>) (b <point>))
  (sqrt (+ (square (- (point-x b) (point-x a)))
           (square (- (point-y b) (point-y a))))))

; Specialise on a built-in type.
(define-method distance ((a <number>) (b <number>))
  (abs (- b a)))

; Unspecialised argument — matches anything.
(define-method describe ((x <object>))
  (display (class-of x)) (newline))

; call-next-method — invoke the next most specific primary method.
(define-method describe ((x <point>))
  (display "point: ")
  (call-next-method))
```

### Predicates and introspection

```scheme
(is-a?     p <point>)        ; → #t  (instance check, respects inheritance)
(class-of  p)                ; → <point>
(subclass? <point-3d> <point>) ; → #t
(class-name <point>)         ; → 'point
(class-slots <point>)        ; → '(x y)
(class-precedence-list <point-3d>)  ; → (<point-3d> <point> <object>)
```

---

## Built-in type hierarchy

All existing Curry types sit in the class hierarchy as pre-defined classes. Generic functions can dispatch on them without any wrapper.

```
<object>
  <boolean>
  <number>
    <exact>
      <integer>
        <fixnum>
        <bignum>
      <rational>
    <inexact>
      <flonum>
    <complex>
    <quaternion>
    <octonion>
    <multivector>
    <surreal>
    <symbolic>
  <pair>
  <null>
  <vector>
  <bytevector>
  <string>
  <symbol>
  <char>
  <procedure>
  <port>
  <actor>
  <quantum>
  <tuple>           ; up/down tuples from (curry sicm)
```

This lets you write:

```scheme
(define-method simplify ((x <symbolic>)) ...)
(define-method simplify ((x <number>))   x)   ; numbers are already simplified
```

---

## Integration with the numeric tower

Currently `+`, `*`, etc. are C-level functions that dispatch by tag. The OOP layer should make this dispatch visible and extensible:

```scheme
(define-class <polynomial> ()
  (coeffs #:accessor poly-coeffs))

(define-method + ((a <polynomial>) (b <polynomial>))
  (make <polynomial>
    #:coeffs (map + (poly-coeffs a) (poly-coeffs b))))

(define-method * ((a <polynomial>) (b <number>))
  (make <polynomial>
    #:coeffs (map (lambda (c) (* c b)) (poly-coeffs a))))
```

The C-level numeric operators call the generic function machinery when neither argument is a known built-in type — a fast-path / slow-path split.

---

## Integration with `define-record-type`

Two options:

1. **Replace** — `define-class` supersedes `define-record-type`. Existing code using `define-record-type` continues to work via a compatibility shim.

2. **Coexist** — `define-record-type` remains for low-level use; `define-class` is the high-level OOP layer. Records created with `define-record-type` do not participate in the generic function dispatch unless explicitly wrapped.

Option 1 is cleaner long-term. Option 2 is safer for backward compatibility. Lean toward option 1 with a shim.

---

## Implementation layers

### Layer 1 — Pure Scheme macro layer (start here)

`define-class`, `define-generic`, `define-method`, `make`, `slot-ref`, `slot-set!`, `is-a?`, `class-of` as a Scheme library (`lib/curry/modules/curry/oop.scm`). Dispatch via a hash table of method tables keyed by argument type tags. No C changes required.

This gives the full API at the cost of some dispatch overhead — acceptable for application-level code.

### Layer 2 — C-level dispatch cache

Add a polymorphic inline cache (PIC) to the VM's generic function call site. The common case (same types as last call) hits the cache and skips the hash lookup. This brings generic function call overhead close to a direct procedure call for hot paths.

### Layer 3 — Tower integration

Wire the numeric tower's internal C-level type dispatch through the generic function machinery so that user-defined numeric types participate in `+`, `*`, `abs`, `sqrt`, etc. without patching the C core.

---

## Open questions

1. **scmutils compatibility** — do we follow scmutils closely enough to include method combination? If the primary audience is SICM users who know the original system, diverging from it has a real cost.

2. **Slot mutability** — immutable by default with `#:mutable` to opt in (recommended above), or mutable by default? This is the most consequential design choice after the dispatch model itself.

3. **`initialize` method** — called by `make` after slot population, lets subclasses run setup logic. Needed for non-trivial constructors.

4. **Method visibility** — are methods global (CLOS) or module-scoped? Global is simpler; module-scoped prevents accidental interference but requires an export mechanism for generic functions.

5. **Protocols as a complement** — should there be a `define-protocol` form for declaring named sets of generic functions that a type "implements"? This gives tooling a handle (list all types implementing `<Metric>`) without changing the dispatch model.

6. **Performance baseline** — before implementing, benchmark the pure Scheme macro layer against direct record access to understand the overhead budget.

7. **Serialisation** — do class instances participate in `write`/`read` round-trips? Requires a `print-object` generic and a reader extension.

---

## Relation to existing features

| Feature | Interaction |
|---|---|
| Actors | An actor's message handler can dispatch on message type via generic functions — a natural fit |
| CAS / symbolic | `simplify`, `∂`, `∫` become generic, extensible to user-defined algebraic structures |
| Qt6 module | `<widget>` hierarchy; `on-click!`, `on-draw!` as generic functions |
| SICM | `<manifold>`, `<coordinate-system>`, `<lagrangian>` as first-class types |
| Numeric tower | Built-in types already in hierarchy; user types plug in via `define-method +` etc. |

---

## Suggested first step

Implement Layer 1 as `(curry oop)` — a pure-Scheme module with no C changes. Write tests covering:

- Single-inheritance dispatch
- Multiple-inheritance with C3 linearisation
- `call-next-method`
- Dispatch on built-in types (`<number>`, `<string>`)
- Extension of `+` for a user-defined type
- Immutable slot enforcement

Get the API right before optimising anything.
