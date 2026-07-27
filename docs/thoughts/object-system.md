# Object System Design: Actors All the Way Down

*Design document for Curry's object system.*
*Status: pre-implementation design; not yet built.*

**Not the design chosen (2026-07-26).** `docs/thoughts/oop.md`'s minimal Slim-CLOS recommendation shipped instead as `(curry oop)` — see [its reference doc](../reference/module-oop.md). Nothing in this document (the full MOP, method combination, logic-valued dispatch, or "every instance is its own actor") is implemented. The object-and-actor pairing this document treats as fundamental is instead available as an ordinary pattern you write by hand — one actor wrapping one instance, dispatching on message shape — shown in [the usage guide's §3](../guides/oop-guide.md#3-objects-owned-by-actors). This document remains as a record of the more radical alternative that was considered and not pursued.

---

## The Core Decision

Every object is an actor. No exceptions, no escape hatch, no "lightweight object that's
just a record really." This is Alan Kay's original vision for object-oriented programming
— objects as autonomous entities that communicate exclusively by message passing — taken
seriously.

The consequence: **encapsulation is physical, not a convention.** There is no mechanism
by which external code can reach into an object's state, because there is no shared
memory. Only messages.

---

## What This Unifies

Curry already has actors (`spawn`, `send!`, `receive`, `self`). It already has records
(`define-record-type`). It already has a logic framework (`(curry logic)`). The object
system is the seam that joins them.

| Concept | Actor model | CLOS model | Unified |
|---------|-------------|------------|---------|
| Instance | actor | instance | actor with named slots |
| State | mailbox + thread-local | slots | private slots in actor |
| Method call | `send!` | generic function | generic function → message |
| Dispatch | `receive` pattern | type-based | type + guard, logic-valued |
| Inheritance | — | class hierarchy | C3 linearization |
| Method combination | — | before/after/around | runs in receiver's thread |
| Creation | `spawn` | `make-instance` | `make-instance` → `spawn` |
| Introspection | `actor-alive?` | `class-of` | class is itself an actor |

---

## Syntax

### Defining a class

```scheme
(define-class <point> ()
  (x :initarg :x :reader point-x)
  (y :initarg :y :reader point-y))

(define-class <colored-point> (<point>)
  (color :initarg :color :reader point-color
                         :writer set-point-color!))
```

The superclass list `()` means direct subclass of `<object>`, the root class.
Multiple inheritance is allowed; C3 linearization determines method resolution order.

Slot options:

| Option | Meaning |
|--------|---------|
| `:initarg :name` | keyword used in `make-instance` |
| `:initform expr` | default value if initarg not supplied |
| `:reader name` | generate read accessor generic function |
| `:writer name` | generate write accessor generic function |
| `:accessor name` | generate both reader and `(setf name)` |

### Creating an instance

```scheme
(define p1 (make-instance <point> :x 0 :y 0))
(define p2 (make-instance <point> :x 3 :y 4))
```

`make-instance` spawns a new actor, sends it an `:initialize` message with the initargs,
and returns the actor reference. The actor is running before `make-instance` returns.

### Defining generic functions

```scheme
(define-generic distance (a b))
(define-generic move!    (point dx dy))
(define-generic describe (obj))
```

A generic function is itself an actor. It holds the method table and handles dispatch.

### Defining methods

```scheme
(define-method distance ((a <point>) (b <point>))
  (let ((dx (- (point-x b) (point-x a)))
        (dy (- (point-y b) (point-y a))))
    (sqrt (+ (* dx dx) (* dy dy)))))

(define-method move! ((p <point>) (dx <number>) (dy <number>))
  (slot-set! self 'x (+ (slot-ref self 'x) dx))
  (slot-set! self 'y (+ (slot-ref self 'y) dy)))

(define-method describe ((p <point>))
  (format #f "<point ~a ~a>" (point-x p) (point-y p)))
```

Inside a method body, `self` refers to the primary receiver (the first specialised
argument). `slot-ref` and `slot-set!` on `self` are direct — no message overhead. `self`
is the one object whose internals the method can touch directly.

### Calling a generic function

```scheme
;; Synchronous (default) — blocks until the result arrives
(distance p1 p2)       ;; => 5.0
(describe p1)          ;; => "<point 0 0>"
(move! p1 1 2)         ;; => (void); p1's slots updated

;; Asynchronous — returns immediately with a future
(define fut (distance? p1 p2))
;; ... do other work ...
(await fut)            ;; => 5.0

;; Fire and forget — no reply expected
(move!! p1 1 2)
```

The `?` suffix returns a future. The `!!` suffix (two bangs) sends with no reply channel
at all — pure fire-and-forget. The `!` suffix from the existing actor API is kept for
raw message sends that bypass the generic function machinery.

---

## How Dispatch Works

### The dispatch chain

When you call `(distance p1 p2)`:

1. A message is sent to the `distance` generic function actor:
   `(:invoke (p1 p2) reply-channel)`

2. The GF actor queries the class of each argument:
   `(:class-of)` → sent to `p1` and `p2` concurrently

3. `p1` and `p2` each reply with their class descriptor.

4. The GF actor runs `compute-applicable-methods` (MOP hook) over the class descriptors
   and the method table. Finds the most specific applicable method.

5. The GF actor runs `compute-effective-method` (MOP hook) — applies method combination
   to produce the effective method closure.

6. The GF actor sends `:invoke-effective-method (effective-method args reply-channel)`
   to the **primary receiver** (`p1`, the first argument).

7. `p1`'s actor runs the effective method closure. Within the closure, `self` is bound to
   `p1`'s slot store (direct access). Other arguments (`p2`) are actor references —
   accessed via the generic function machinery or explicit messages.

8. When the closure returns, `p1`'s actor sends the result to `reply-channel`.

9. The synchronous caller receives the result.

### Why the primary receiver runs the method

The method body may need to call `slot-ref self` and `slot-set! self` without message
overhead. Running the method inside the primary receiver's actor thread gives it that
direct access while preserving encapsulation for all other objects.

Non-primary arguments are accessed through their public interface (generic functions or
explicit messages). This is a tighter encapsulation guarantee than CLOS — in CLOS,
`slot-value` can reach into any object from any context if you have the instance.

### Multiple dispatch is real

All argument types participate in method selection, not just the first. A method
specialising on `(distance <polar-point> <cartesian-point>)` is more specific than one
specialising on `(distance <point> <point>)` and will be preferred when appropriate.

---

## Method Combination

Standard method combination works as in CLOS, running entirely within the primary
receiver's actor thread:

```scheme
;; before/after/around qualifiers
(define-method distance :before ((a <point>) (b <point>))
  (display "computing distance...") (newline))

(define-method describe :around ((p <colored-point>))
  (string-append (call-next-method) " color=" (symbol->string (point-color p))))
```

`call-next-method` invokes the next less-specific method in the linearized order.

### Custom method combinators

```scheme
;; Define a combinator that sums all applicable method results
(define-method-combinator sum-combinator (methods args)
  (apply + (map (lambda (m) (apply m args)) methods)))

(define-generic total-weight (obj)
  :method-combination sum-combinator)
```

Custom combinators run in the primary receiver's thread.

---

## Logic-Valued Dispatch

Method guards allow dispatch conditions that return truth values in any `(curry logic)`
logic, not just booleans.

```scheme
;; A guard that returns a fuzzy truth value
(define-method distance :guard (lambda (a b) (nearby? a b))
  ((a <point>) (b <point>))
  0.0)   ; fast path: nearby points are distance ~0 at this zoom level

;; The :dispatch-logic on the generic function determines how guards are evaluated
(define-generic render (scene obj)
  :dispatch-logic fuzzy-logic
  :dispatch-threshold 0.7)
```

When `dispatch-logic` is set:

1. All applicable methods have their guards evaluated, returning truth values in the
   specified logic.
2. Methods whose guard does not entail truth (below the threshold) are excluded.
3. Among remaining methods, the most specific (by class hierarchy) is preferred.
4. On ties in specificity, the method with the higher guard truth value wins.

Under `classical-logic` (the default), this reduces to ordinary predicate dispatch —
guards are boolean, any non-`#f` value passes.

Under `fuzzy-logic`, a method that is 0.9-applicable beats one that is 0.6-applicable
at the same specificity level.

Under `defeasible-logic`, method priority annotations defeat less-specific defaults
without requiring more-specific subclasses.

```scheme
;; Defeasible dispatch: penguins don't fly, despite being birds
(define-method can-fly? ((b <bird>))   #t)
(define-method can-fly? ((p <penguin>)) #f
  :priority 10)   ; defeats the <bird> method
```

---

## Slot Access Protocol

From outside an object:

```scheme
(point-x p1)          ;; reader — sends :get-slot message to p1, blocks for reply
(set-point-color! p3 'red)  ;; writer — sends :set-slot! message, blocks for ack
```

From inside a method (self is the primary receiver):

```scheme
(slot-ref  self 'x)         ;; direct — no message, no overhead
(slot-set! self 'x new-val) ;; direct — no message, no overhead
```

Slots that are not exposed via `:reader`/`:writer`/`:accessor` are entirely inaccessible
from outside. Not "private by convention" — physically inaccessible because there is no
message that will return them.

---

## Object Lifecycle

```
make-instance
  → allocate-instance (MOP)   ;; compute slot layout
  → spawn actor               ;; start running
  → send :initialize          ;; fill slots from initargs + initforms
  → return actor reference

actor runs its receive loop indefinitely:
  :class-of              → reply with class descriptor
  :invoke-effective-method (method args reply) → run method, reply
  :get-slot name         → reply with slot value
  :set-slot! name val    → update slot, reply :ok
  :initialize initargs   → fill slots, reply :ok
  :finalize              → run finalizers, exit

When the actor reference is garbage collected:
  → GC sends :finalize (if a finalizer is registered)
  → actor exits
```

---

## The Meta-Object Protocol

Classes and generic functions are themselves first-class actors, queryable and
extensible. The MOP defines how the object system works on itself.

### Class actors

```scheme
(class-of p1)                    ;; => the <point> class actor
(send! (class-of p1) :name)      ;; => '<point>
(send! (class-of p1) :slots)     ;; => '(x y)
(send! (class-of p1) :superclasses)   ;; => (list <object>)
(send! (class-of p1) :subclasses)     ;; => list of known subclasses
(send! (class-of p1) :methods)        ;; => list of method actors
```

### Generic function actors

```scheme
(send! distance :methods)               ;; all defined methods
(send! distance :add-method! method)    ;; extend at runtime
(send! distance :remove-method! method) ;; retract at runtime
```

Adding a method to a generic function at runtime is just a message send. This is the
mechanism by which `define-method` works at the top level — it compiles to a message
send to the generic function actor.

### MOP hooks (overridable per class)

| Hook | When called | Default |
|------|-------------|---------|
| `allocate-instance` | before `initialize` | allocate slot vector |
| `initialize` | on `make-instance` | fill slots from initargs |
| `compute-applicable-methods` | on each dispatch | type-based selection |
| `compute-effective-method` | on each dispatch | standard combination |
| `slot-value-using-class` | on slot read | return from slot vector |
| `(setf slot-value-using-class)` | on slot write | write to slot vector |
| `class-finalized?` | after class defined | compute C3 MRO |

Overriding `compute-applicable-methods` on a metaclass gives you a completely custom
dispatch strategy for all classes of that metaclass.

---

## Inheritance and the MRO

C3 linearization (as in Python, Dylan, and Raku) determines the Method Resolution Order.
C3 is more predictable than CLOS's original left-to-right depth-first algorithm in the
presence of multiple inheritance.

```scheme
(define-class <a> ())
(define-class <b> (<a>))
(define-class <c> (<a>))
(define-class <d> (<b> <c>))

;; C3 MRO of <d>: (<d> <b> <c> <a> <object>)
;; <b> before <c> because <d> lists <b> first
;; <a> after both because it must come after anything that inherits from it
```

`call-next-method` walks this linearization.

---

## Integration with Existing Curry Features

### With the actor system

The existing `spawn`/`send!`/`receive`/`self` API continues to work unchanged for raw
actors. Objects are actors, but actors are not required to be objects. A raw actor can
receive messages from object-actors and respond — they share the same mailbox protocol.

```scheme
;; A raw actor as a service, called by object methods
(define cache-actor
  (spawn (lambda ()
    (let loop ((cache (make-hash-table)))
      (receive
        ((':get key reply)      (send! reply (hash-table-ref/default cache key #f))
                                (loop cache))
        ((':put key val reply)  (hash-table-set! cache key val)
                                (send! reply ':ok)
                                (loop cache)))))))
```

Object methods can send messages to `cache-actor` directly. No conversion needed.

### With the logic framework

The `:dispatch-logic` option on `define-generic` connects method selection to any logic
in `(curry logic)`. See the Logic-Valued Dispatch section above.

Additionally, logical sets can describe class membership:

```scheme
;; A fuzzy class — membership is a degree, not binary
(define-class <tall-person> (<person>)
  :membership-logic fuzzy-logic
  :membership-fn (lambda (p) (height-score (person-height p))))

;; instance-of? returns a fuzzy truth value
(instance-of? alice <tall-person>)   ;; => 0.73
```

### With `define-record-type`

Existing R7RS records continue to work. `define-class` is a superset — you can
gradually migrate records to classes when you need dispatch and encapsulation.

Records defined with `define-record-type` are *not* actors — they remain plain structs.
This is the only exception to "everything is an actor": the R7RS compatibility layer.
New code should use `define-class`.

### With the symbolic CAS

Symbolic expressions benefit from dispatch — `simplify`, `∂`, `∫`, `expand` can all be
generic functions that dispatch on the expression type. This is cleaner than the current
large `cond` trees in `src/symbolic.c`.

---

## Implementation Sketch

### Phase 1 — Class and instance machinery

- `define-class` macro: compiles class definition, spawns a class actor
- Class actor: holds name, slot descriptors, superclass list, computed MRO, method table
- `make-instance`: calls `allocate-instance` hook, spawns instance actor, sends
  `:initialize`
- Instance actor main loop: handles `:class-of`, `:get-slot`, `:set-slot!`,
  `:invoke-effective-method`, `:finalize`
- `slot-ref`/`slot-set!` with `self`: compiler lowers these to direct slot-vector
  accesses when the receiver is statically known to be `self`

### Phase 2 — Generic functions and dispatch

- `define-generic` macro: spawns a GF actor with empty method table
- `define-method` macro: compiles method, sends `:add-method!` to GF actor
- GF actor main loop: handles `:invoke` (runs dispatch), `:add-method!`,
  `:remove-method!`, `:methods`
- `compute-applicable-methods`: default implementation in Scheme, overridable
- `compute-effective-method`: standard combination, overridable

### Phase 3 — Method combination

- `:before`, `:after`, `:around` qualifiers in `define-method`
- `call-next-method` desugars to a continuation threaded through the effective method

### Phase 4 — Logic-valued dispatch

- `:dispatch-logic` and `:dispatch-threshold` on `define-generic`
- `:guard` on `define-method`
- GF actor evaluates guards during dispatch, uses logic's entailment predicate

### Phase 5 — MOP

- All MOP hooks callable from Scheme
- `define-metaclass` for custom class behaviors
- Bootstrapping: class actors are instances of `<metaclass>`, which is itself a class

---

## Open Questions

1. **Synchronous call overhead**: the default synchronous call involves at minimum
   three message sends (caller→GF, GF→instance for class query, GF→instance for
   invocation) plus the reply chain. This is acceptable for object interactions but
   may be surprising if users replace simple record accessors with class instances.
   Should there be a compiler optimisation that elides the GF actor for monomorphic
   call sites?

2. **`slot-ref` on non-self arguments**: inside a method, `(slot-ref other 'x)` on a
   non-self actor-object requires a message send. Should there be sugar for this, or
   should it always be spelled as a generic function call (`(point-x other)`)? The
   latter seems cleaner and enforces the public interface.

3. **Class redefinition**: CLOS supports redefining a class and updating existing
   instances. In the actor model this means sending all live instances an
   `:update-instance-for-redefined-class` message. Complex but implementable. Defer
   to Phase 5.

4. **Persistence**: can an actor-object be serialized and restored? The slot values
   are easy; the running thread is not. Probably needs an explicit `freeze`/`thaw`
   protocol. Defer.

5. **The `<class>` bootstrapping problem**: `<class>` is a class, but it must exist
   before any class can be defined. This requires a small hand-built bootstrap in C
   before the Scheme MOP takes over — the same approach taken by SBCL and Guile.

---

## Why Not Prototype-Based?

Prototype systems (Self, early JavaScript) are simpler to implement and make objects
fully first-class values without requiring class declarations. They were considered.

The problem: Curry already has a rich numeric tower, a CAS, and a logic framework. All
of these benefit enormously from multiple dispatch — `(+ a b)` should dispatch on the
types of *both* arguments to decide whether to produce a fixnum, bignum, rational,
complex, symbolic expression, or fuzzy number. Prototype systems give you single dispatch
at best.

CLOS-style generic functions with multiple dispatch are the right substrate for a
language that needs to unify as many different kinds of things as Curry does. The actor
model gives us the encapsulation and concurrency that CLOS originally lacked.

---

## Prior Art

- **Smalltalk-80**: objects as actors, message passing, no shared state. Curry takes this
  seriously where most "OOP" languages (Java, Python, Ruby) abandoned it.
- **CLOS**: generic functions, multiple dispatch, method combination, MOP. The gold
  standard for object systems in Lisp.
- **Erlang/OTP**: actors as the unit of concurrency and fault isolation. gen_server is
  effectively an object without the dispatch machinery.
- **E language**: capability-secure object model, eventual sends vs. immediate calls,
  explicit async. Influenced the `?`-suffix async design here.
- **Gerbil Scheme**: actors + objects, closer to Erlang's model. Shows the combination
  is viable in a Scheme.
- **Predicate dispatch** (Ernst et al., 1998): method guards as arbitrary predicates,
  more expressive than type-based dispatch. Influenced the `:guard` design.
