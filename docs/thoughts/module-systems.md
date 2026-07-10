# Module Systems: A Comparative Survey
## Toward a Design for Curry's Next Module System

*Background reading. Status: pre-design, no implementation decisions made.*

---

## What Curry Has Now

Two kinds of modules, both functional but limited:

**C extension modules**: a `.so` file exposing `curry_module_init(CurryVM*)`. Loaded
when `(import (curry name))` is first evaluated. No namespacing beyond the names
registered in `builtins_register`. No re-export. No composition.

**Scheme modules**: a `.sld` or `.scm` file found on `CURRY_MODULE_PATH`. `import` is
handled as a special form in `eval.c`, which calls `modules_import`. Files are loaded
once (single instantiation). No explicit export declaration — whatever the file defines
at top level is exported. No re-export, no filtering, no aliasing.

What's missing that will matter as the system grows:

- No way to say "export these names but not those"
- No way to compose two modules or parameterize one by another
- No way to have multiple instances of the same module with different configuration
- No phase separation — macros and values live in the same namespace
- No module-level opacity (you can't hide the implementation type of an exported value)

These gaps are already visible in `(curry sets)` and `(curry logic)`: the logical-set
module is parameterized at *runtime* by a logic value, but there is no way to express
"give me a module that is `(curry sets)` specialized for `fuzzy-logic`" at the module
level.

---

## The Landscape

### R7RS `define-library`

```scheme
(define-library (my lib name)
  (import (scheme base))
  (export foo bar)
  (begin
    (define (foo x) ...)
    (define (bar x) ...)))
```

Simple, portable, widely supported. Import filters: `only`, `except`, `rename`,
`prefix`. One library per file. No nested libraries. No re-export. No parameterization.
No phase control beyond `(for-syntax ...)`.

Good for portability. Not enough for a language with Curry's ambitions.

### R6RS `library`

Adds phase separation: `(for lib expand)`, `(for lib run)`, `(for lib (meta 1))`.
Lets you import a library at macro-expansion time separately from runtime. Curry
currently strips phases (as does Gerbil), which is pragmatic — full phase semantics
are complex and rarely needed.

No parameterization. No re-export. Same fundamental limitations as R7RS.

### Gerbil

Gerbil's module system is a significant step up from R7RS/R6RS. Every file is a module.
Rich import/export filters:

```scheme
;; Import
(import :std/text/json)
(import (only-in :std/text/json read-json write-json))
(import (rename-in :foo (old-name new-name)))
(import (prefix-in :foo foo:))
(import (except-in :foo internal-helper))
(import (for-syntax :foo))          ; phi+1 — macro phase

;; Export
(export foo bar)
(export #t)                          ; everything
(export (rename: foo public-foo))
(export (import: :other/module))     ; re-export everything from another module
(export (except-out #t internal))    ; everything except internal
```

Re-export is first-class. Phase imports are clean. Nested `(module id ...)` submodules
work inside a file. Custom import/export expanders via `defsyntax-for-import` allow
user-defined import transformers.

**What Gerbil doesn't have:** ML-style functors. No way to parameterize a module over
another module statically. The workaround is runtime dispatch (`:std/generic`) or custom
preludes (`#lang :custom/prelude`) for language-level parameterization — neither of which
is the same thing.

**Actor/module integration in Gerbil:** Actors are not module-level constructs.
`defmessage` registers marshalable message types in a type registry — both sender and
receiver must import the message type to unmarshal correctly. That's the only point
where the actor and module systems touch. Actors are discovered at runtime via
`register-actor!`, not at the module level.

This is a meaningful limitation for Curry's design: if objects are actors, and classes
define families of actors, the module system should know about classes. Gerbil's model
doesn't support this.

### Racket

Racket's module system is the most sophisticated in the Lisp/Scheme world. Key features:

**`require`/`provide` with contracts:**
```racket
(provide (contract-out [foo (-> integer? integer?)]))
(require (only-in "other.rkt" bar)
         (prefix-in str: racket/string))
```

**Submodules** — a module nested inside another file, can be required independently:
```racket
(module+ test
  (require rackunit)
  (check-equal? (foo 1) 2))
```

**Units** — Racket's answer to ML functors. A unit is a module that declares
*imports it expects to be linked* and *exports it provides*. Units are first-class
values, composed with `compound-unit`, linked with `define-values/invoke-unit`:

```racket
(define-unit set-unit
  (import ordered-type^)          ; signature: expects these bindings
  (export set^)                   ; signature: provides these bindings
  (define (make-set) ...)
  (define (set-add s x) ...))

;; Link the unit with a specific ordered-type implementation
(define-values/invoke-unit/infer
  (compound-unit/infer (import) (export)
    (link [((OT : ordered-type^)) integer-ordered-type-unit]
          [((S  : set^)) set-unit OT])))
```

This is exactly the pattern we want: `set-unit` parameterized over `ordered-type^`.
Linking produces a concrete module.

Units are powerful but verbose. They require explicit signatures (`define-signature`),
explicit linking, and the composition syntax is heavy. Racket uses them mostly for
large-scale parameterization (the teaching languages, the GUI framework).

**Phases:** Racket has a full phase tower. `(require (for-syntax ...))` imports at
macro-expansion phase. `(require (for-meta 2 ...))` at the phase above that. This is
mostly transparent to users but essential for hygienic macros.

**Single vs. multiple instantiation:** Racket supports both, which creates subtleties
when modules have side effects. The default is single instantiation per phase.

### ML / Standard ML / OCaml

The ML module system is a programming language within the programming language.

**Structures** are modules — named collections of types and values:
```sml
structure IntSet = struct
  type t = int list
  fun empty () = []
  fun add s x = x :: s
  fun member s x = List.exists (fn y => y = x) s
end
```

**Signatures** are module types — interfaces:
```sml
signature SET = sig
  type elem
  type t
  val empty   : unit -> t
  val add     : t -> elem -> t
  val member  : t -> elem -> bool
end
```

**Functors** are functions from structures to structures:
```sml
functor MakeSet (Elem : sig
  type t
  val compare : t * t -> order
end) : SET = struct
  type elem = Elem.t
  (* red-black tree implementation using Elem.compare *)
  ...
end

(* Instantiate for integers *)
structure IntSet = MakeSet(struct
  type t = int
  val compare = Int.compare
end)

(* Instantiate for strings *)
structure StringSet = MakeSet(struct
  type t = string
  val compare = String.compare
end)
```

Each instantiation is a separate, statically typed module. The type checker verifies
that `Elem.compare` satisfies the required signature. The functor is applied at compile
time.

This is *exactly* what we want for `(curry sets)`:

```scheme
;; What we'd like to write:
(define-module <fuzzy-sets> (LogicalSets fuzzy-logic))
;; <fuzzy-sets> is now a module specialized for fuzzy-logic
;; (fuzzy-set 'hot 1.0) is unambiguous within its namespace
```

ML functors solve this cleanly. The cost: full ML-style static typing is a large
dependency, and Curry is dynamically typed.

**OCaml first-class modules** go further: modules can be packed into values and unpacked
at runtime. This bridges the static/dynamic gap:

```ocaml
module type LOGIC = sig
  type truth
  val bottom : truth
  val top    : truth
  val meet   : truth -> truth -> truth
  val join   : truth -> truth -> truth
end

(* A module as a first-class value *)
let fuzzy_logic : (module LOGIC with type truth = float) = (module struct
  type truth = float
  let bottom = 0.0
  let top    = 1.0
  let meet a b = min a b
  let join a b = max a b
end)

(* Unpack and use *)
let (module L) = fuzzy_logic in
L.join 0.3 0.7   (* => 0.7 *)
```

OCaml first-class modules are a runtime value that carries a module, queryable and
passable. This is the closest prior art to what Curry does when it passes `fuzzy-logic`
as a runtime value to `make-logical-set`.

---

## The Functor Question for Curry

Curry already does runtime functors. When you call `(make-logical-set fuzzy-logic)`, you
are applying a runtime functor — a function from a logic to a set implementation. This
works. The question is whether we also want *static* (module-level) functors.

Arguments for:

- A `<fuzzy-sets>` module gives you a clean namespace: `(fuzzy-set-union a b)` rather
  than having to carry `fuzzy-logic` around as a parameter
- Module-level specialization is visible to tooling — an IDE can know what operations
  are available on values from `<fuzzy-sets>`
- Consistent with how the object system will work: a `define-class` is a module that
  produces actors; it should be composable at the module level

Arguments against:

- Curry is dynamically typed — the static guarantees that make ML functors valuable
  don't transfer directly
- Runtime parameterization (`make-logical-set fuzzy-logic`) already works and is
  simpler to understand
- Adding static functors before we understand all the use cases risks building the wrong
  thing

**Tentative conclusion**: runtime functors (what we have) are the right first step.
Module-level composition can be a later addition, informed by how the object system
actually gets used. We should design the module system so that adding functors later is
possible, without committing to them now.

---

## What Curry's Module System Should Become

Based on the survey, a concrete set of requirements:

### Must have

**Explicit export declarations.** Every module should state what it exports. Implicit
"everything defined is exported" is a maintenance hazard. Something like:

```scheme
(define-module (curry sets)
  (export make-logical-set logical-set-union logical-set-member ...))
```

**Import filtering.** At minimum: `only`, `except`, `rename`, `prefix`. Gerbil's
`only-in`/`except-in`/`rename-in`/`prefix-in` is a good syntax.

**Re-export.** Essential for building layered modules:

```scheme
(define-module (curry sets)
  (import (curry logic))
  (export (re-export (curry logic)))   ; re-export logic along with sets
  (export make-logical-set ...))
```

**Module aliases.** Being able to say "this name refers to that module" without loading
it separately.

### Should have

**Submodules.** Useful for test modules, implementation details that are conditionally
available. Gerbil's nested `(module ...)` is sufficient.

**Phase control.** At least `(import (for-syntax ...))` for macro definitions. Curry's
current approach (macros and values in the same phase) works for small programs but
becomes ambiguous in larger ones.

**Sealing.** The ability to export a name but hide its implementation type:

```scheme
(export make-set set? set-add set-member)  ; sealed — callers can't see it's a vector
```

This is important for the object system: `make-instance` should return an opaque actor
reference, not something callers can inspect as a specific struct type.

### Would be nice

**Module-level parameterization (functors).** Deferred — see above. Design should not
preclude it.

**Module introspection.** Being able to ask a module what it exports, query its version,
list its dependencies. Useful for tooling and the eventual package manager.

---

## How This Connects to the Object System

The connection is direct. A class definition:

```scheme
(define-class <point> ()
  (x :initarg :x :reader point-x)
  (y :initarg :y :reader point-y))
```

Is semantically a module that:
- Exports `make-instance` (specialized for `<point>`)
- Exports `point-x`, `point-y` (the accessor generic functions)
- Exports `<point>` (the class descriptor, itself an actor)
- Seals the slot representation (callers can't see `x` and `y` are a vector)

If the module system supports sealing and explicit exports, the object system gets
opacity for free. If not, it has to implement its own layer of protection.

Similarly, a generic function definition:

```scheme
(define-generic distance (a b))
```

Is a module-level entity — a named GF actor that should be importable, re-exportable,
and extensible across module boundaries. Methods added in one module should be visible
when the generic function is imported in another.

This is the standard problem in CLOS-style systems: method definitions can appear
anywhere, but the generic function needs a home. The module system needs to have an
answer for where GFs live and how they are extended across module boundaries.

---

## Recommended Next Step

Before designing the object system's module integration, design the module system
itself. The minimal viable upgrade:

1. Add `(define-module ...)` as a new top-level form that declares exports explicitly
2. Add `only`/`except`/`rename`/`prefix` import filters
3. Add `re-export`
4. Keep backward compatibility — existing `.sld`/`.scm` modules continue to work

This is a modest change to `src/modules.c` and `src/eval.c` but gives us the foundation
for everything else. The object system, when designed, can then express class
definitions as proper module-level entities with sealed implementations.

---

## Quick Reference: Module System Comparison

| Feature | R7RS | R6RS | Gerbil | Racket | ML/OCaml | Curry now | Curry target |
|---------|------|------|--------|--------|----------|-----------|--------------|
| Explicit exports | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ✓ |
| Import filters | basic | basic | rich | rich | N/A | ✗ | rich |
| Re-export | ✗ | ✗ | ✓ | ✓ | ✓ | ✗ | ✓ |
| Submodules | ✗ | ✗ | ✓ | ✓ | ✓ | ✗ | ✓ |
| Phase control | basic | full | partial | full | N/A | ✗ | basic |
| Sealing/opacity | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ | ✓ |
| Functors | ✗ | ✗ | ✗ | units | ✓ | runtime only | deferred |
| First-class modules | ✗ | ✗ | ✗ | partial | OCaml | runtime only | deferred |
| Actor integration | N/A | N/A | loose | N/A | N/A | N/A | class = module |
