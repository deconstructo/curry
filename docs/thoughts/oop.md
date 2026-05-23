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

## Chosen model: CLOS-style generic functions

### Why not message-passing?

The closure-based message-passing style (`(obj 'method arg)`) is idiomatic in toy Scheme OOP but doesn't compose. You cannot write a generic `distance` function that dispatches on *both* of its arguments without embedding the dispatch logic inside every object. It also makes the method body invisible to the toolchain.

### Why not single dispatch?

Single dispatch (Python / Ruby style) is familiar but artificially constrains the model. Curry's numeric tower already does something richer — `(+ quaternion symbolic)` dispatches on the combination of types, not just the first argument. A single-dispatch OOP layer would be a step backwards.

### CLOS-style

Generic functions own the dispatch. Methods are attached to generic functions and specialise on zero or more argument types. Any module can add a new method to an existing generic function — the system is *open*.

This fits Curry naturally:
- The numeric tower's internal type dispatch becomes visible at the Scheme level.
- Users can extend `+`, `*`, `simplify`, `display` for their own types.
- Multiple inheritance with C3 linearisation avoids the diamond problem.

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
(slot-set! p 'x 10)

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
```

### Method combination

```scheme
; call-next-method — invoke the next most specific method.
(define-method area ((s <square>))
  (* (side s) (side s)))

(define-method area :before ((s <shape>))
  (assert (> (side s) 0)))

; Standard method combination: primary, :before, :after, :around.
; Default combination: run :before chain, then primary, then :after chain.
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

All existing Curry types sit in the class hierarchy as pre-defined classes. This means generic functions can dispatch on them without any wrapper.

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

The most important integration point. Currently `+`, `*`, etc. are C-level functions that dispatch by tag. The OOP layer should make this dispatch visible and extensible:

```scheme
; A user-defined polynomial type participates in arithmetic.
(define-class <polynomial> ()
  (coeffs #:accessor poly-coeffs))

(define-method + ((a <polynomial>) (b <polynomial>))
  (make <polynomial>
    #:coeffs (map + (poly-coeffs a) (poly-coeffs b))))

(define-method * ((a <polynomial>) (b <number>))
  (make <polynomial>
    #:coeffs (map (lambda (c) (* c b)) (poly-coeffs a))))
```

The C-level numeric operators would call the generic function machinery when neither argument is a known built-in type — a fast-path / slow-path split.

---

## Integration with `define-record-type`

Two options:

1. **Replace** — `define-class` supersedes `define-record-type`. Existing code using `define-record-type` continues to work via a compatibility shim that translates it to `define-class`.

2. **Coexist** — `define-record-type` remains for low-level use; `define-class` is the high-level OOP layer. Records created with `define-record-type` do not participate in the generic function dispatch unless explicitly wrapped.

Option 1 is cleaner long-term. Option 2 is safer for backward compatibility. Lean toward option 1 with a shim.

---

## Implementation layers

### Layer 1 — Pure Scheme macro layer (start here)

`define-class`, `define-generic`, `define-method`, `make`, `slot-ref`, `slot-set!`, `is-a?`, `class-of` as a Scheme library (`lib/curry/modules/curry/oop.scm`). Dispatch via a hash table of method tables keyed by argument type tags. No C changes required.

This gives us the full API at the cost of some dispatch overhead — acceptable for application-level code.

### Layer 2 — C-level dispatch cache

Add a polymorphic inline cache (PIC) to the VM's generic function call site. The common case (same types as last call) hits the cache and skips the hash lookup. This brings generic function call overhead close to a direct procedure call for hot paths.

### Layer 3 — Tower integration

Wire the numeric tower's internal C-level type dispatch through the generic function machinery so that user-defined numeric types participate in `+`, `*`, `abs`, `sqrt`, etc. without patching the C core.

---

## Open questions

1. **Slot mutability** — should slots be immutable by default (functional style) with `#:mutable` to opt in? Or mutable by default (Smalltalk style)?

2. **Metaclasses** — do we expose metaclass protocol (class-of a class, methods on classes)? Powerful but adds significant complexity. Defer.

3. **`initialize` method** — called by `make` after slot population, lets subclasses run setup logic. Needed for non-trivial constructors.

4. **Method visibility** — are methods global (CLOS) or module-scoped? Global is simpler; module-scoped prevents accidental interference but requires an export mechanism for generic functions.

5. **Performance baseline** — before implementing, benchmark the pure Scheme macro layer against direct record access to understand the overhead budget.

6. **Serialisation** — do class instances participate in `write`/`read` round-trips? Requires a `print-object` generic and a reader extension.

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

Get the API right before optimising anything.
