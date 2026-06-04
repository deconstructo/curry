# Curry Parallelism Design Notes

*Captured from design session, June 2026. Intended as input to Curry 2 design and the Anarchist's Cookbook.*

---

## Background: ParaSail Review

ParaSail (Parallel Specification and Implementation Language, S. Tucker Taft / AdaCore) was reviewed as a reference point for parallelism models worth considering for Curry and Cill.

### What ParaSail Does

ParaSail's core move is *pervasive implicit parallelism achieved through structural simplification* rather than programmer annotation. Every expression has parallel evaluation semantics by default. `F(X) + G(Y)` is safe to evaluate in parallel because the language rules guarantee it — not because you annotated it. The compiler decides whether to actually spawn parallel activities based on complexity heuristics; a work-stealing scheduler maps those activities to physical cores.

Safety derives from three eliminations:

- No global variables
- No parameter aliasing (if either parameter is updatable)
- No re-assignable pointers

The net effect: the compiler can determine parallel safety from local analysis alone, without examining function bodies. Parameter passing uses *hand-off semantics* — when a variable is passed with read/write access, the caller may not manipulate it or pass it elsewhere until the function returns. Ownership transfer without Rust's syntax overhead.

Memory is region-based rather than a global GC heap. Work stealing uses a small number of heavyweight worker processes (~one per core), each with a LIFO picothread queue, periodically stealing from others in FIFO order (so stolen threads are those that have been waiting longest).

### What Is and Isn't Worth Importing

**Worth serious consideration:**

**Hand-off ownership semantics for actor message passing.** Curry's actor model handles isolation at the mailbox level, but formalising a hand-off discipline at the language level — where passing a value to an actor *moves* it rather than copying or sharing — would eliminate a class of race conditions by construction rather than convention. Cill especially, if it gets a static type system, could enforce this at compile time.

**Work-stealing scheduler for the actor system.** Per-actor LIFO queues with FIFO stealing gives better multicore utilisation without programmer involvement. Interaction with Boehm GC (pause behaviour across worker threads) needs thought but is tractable.

**No-global-variables as convention or lint advisory** in actor contexts, particularly for pedagogical purposes in the Cookbook.

**Not worth importing:**

The pointer-free region-based memory model is load-bearing for ParaSail's parallelism guarantees but is inseparable from the language being designed around it. It doesn't transplant onto a Scheme substrate with Boehm GC already in place. The module/interface system is Ada-flavoured OOP and doesn't map onto Curry's architecture usefully.

**Summary principle:** ParaSail's interesting insight is *achieve safety through subtraction, not addition*. Remove what makes parallelism unsafe and parallelism becomes the default rather than the exception. This is philosophically congenial to Curry's sensibility. The specific mechanisms — hand-off ownership, work-stealing — are portable ideas. The rest isn't.

---

## Parallel Argument Evaluation in S-Expressions

### The Problem

Given `(f a b c)`, we want to evaluate `a`, `b`, `c` in parallel before applying `f`. This is safe if and only if none of `a`, `b`, `c` share mutable state. In a pure functional subset that's trivially guaranteed. In Scheme with side effects it is not.

### The R7RS Gift

R7RS explicitly leaves argument evaluation order **unspecified**. Implementations may evaluate left-to-right, right-to-left, or in any order. This is deliberate — it leaves room for implementations to do something clever, including parallel evaluation, without violating the standard.

Parallel argument evaluation is therefore a *conforming implementation choice*, not a standards violation. The race condition problem is real but it is an implementation problem, not a standards problem.

### The AST Boundary

The design space is clean: **function application** gets the parallel treatment; **special forms** do not.

`(if a b c)`, `(and a b c)`, `(let ...)` etc. pass arguments unevaluated — the form itself controls evaluation. `b` and `c` in `(if a b c)` must not be evaluated until `a` is known. But these are syntactic forms, not function application, and the distinction is unambiguous in the AST.

So: function application → parallel candidate. Special form → sequential by definition. The AST already tells you which is which.

### Approaches

**Static dependency analysis at the form level.** Walk the form, build a dependency graph. Subexpressions sharing no free mutable variables — no common actors, no common mutable bindings — evaluate in parallel. Dependent subexpressions get sequenced. This is what ParaSail's compiler does, but it can do it statically because it has no mutable globals. In Curry this would be partially dynamic, at actor-local scope.

**Actor-boundary parallelism as the safe default.** Rather than parallelising within an expression, parallelise across actors. Argument expressions that are themselves actor sends or receives are already isolated by the mailbox discipline — spawn those concurrently, collect results. Conservative but compositionally safe, and it maps naturally onto Curry's existing actor system. The unit of parallelism is the message boundary, not the subexpression.

**Type-directed parallelism.** If Curry 2 acquires a partial or optional type system, tag values as pure or effectful. Pure subexpressions parallelise freely; effectful ones sequence. Used as a hint to the scheduler rather than a proof obligation.

**Speculative evaluation with rollback (STM).** Evaluate optimistically in parallel, track writes, detect conflicts, roll back and resequence on conflict. Expensive and complex. Probably overkill for Curry but worth knowing it exists.

**The most Curry-native answer** is actor-boundary parallelism — it is already the unit Curry's concurrency model thinks in, the safety guarantee is already there structurally, and it composes with the epistemological plugin system cleanly.

---

## Map-Reduce: Parallel by Default

### Map

`map` over a list is embarrassingly parallel by definition. Each application of `f` to an element is independent of every other by the *contract* of `map`. If `f` has side effects that create dependencies between elements, the programmer has violated the semantic contract of `map`. Parallel evaluation makes the misbehaviour visible faster — arguably a feature.

**Decision: `map` is parallel by default in Curry.**

This is conforming — R7RS makes no guarantees about evaluation order across elements for the same reason argument evaluation order is unspecified.

A sequential `map` is the special case that needs justifying, not the parallel one. If someone writes a Curry `map` with a side-effecting `f` and gets surprising results, the answer is: you used `map` wrong. `map` is a declaration that applications are independent. If they are not independent, use something else. The language teaches correct thinking by making the correct case fast and the incorrect case visible.

**Implementation shape:**

- Below a threshold (tunable, worth exposing as a parameter), `map` falls back to sequential evaluation — actor spawning overhead would dwarf the computation for small lists.
- Above the threshold, spawn actors, collect from mailbox, reassemble in order.
- Reassembly: each actor returns `(index . value)`; the collector reassembles by index. Simple and correct.

**Naming convention:**

- `map` — parallel (the default, the correct case)
- `map/seq` — sequential escape hatch for the rare case where evaluation order genuinely matters

**`for-each`:** same question applies; same answer.

### Reduce

Parallel `reduce` requires the combining function to be associative — which the programmer is implicitly asserting when using `reduce`. This gives a parallel reduction tree rather than a left fold, which changes the result if the operation is not associative. That is the programmer's contract to honour.

---

## Actor Model as Natural Parallelism Unit

The actor model is not just an abstraction — it is a practical parallelism tool. Each `map` application is a natural unit of actor work. Spawn an actor per element (above threshold), collect into a mailbox, done. The work-stealing scheduler handles load balancing. Parallel `map` follows almost for free from the actor infrastructure, without dependency analysis.

For the *Anarchist's Cookbook*: parallel `map` is a natural first concrete example of the actor model as performance tool rather than merely an abstraction. The reader sees embarrassing parallelism emerge from the structure of computation rather than being bolted on.

---

## Explicit Parallelism: `par`

Because Curry cannot make ParaSail's eliminations (no globals, no aliasing) without breaking Scheme compatibility, the honest position is:

- `par` as an explicit opt-in form — the programmer asserts "I know these are independent"
- Static analysis as an advisory layer that can *suggest* where `par` would be safe
- `map` parallel by default handles the overwhelming majority of the practical cases

---

## Open Questions

- Threshold value for `map` parallelism fallback: what is the right default, and should it be runtime-tunable or compile-time?
- Interaction of work-stealing scheduler with Boehm GC pause behaviour across worker threads.
- Whether `for-each` follows the same parallel-by-default decision as `map`.
- Hand-off ownership semantics: at the language level (Cill type system) or as a convention enforced by the actor runtime?
- Whether the epistemological plugin system (Nyāya, songline, whakapapa etc.) evaluators running as actors gives implicit parallelism across epistemological modules for free.
