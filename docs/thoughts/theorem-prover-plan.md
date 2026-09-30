# Theorem proving — plan

*Drafted 2026-09-30. Status: planning only, nothing implemented yet.*

## Why

Two motivations, both genuine, neither one alone would justify the project:

1. **It closes a loop the language's own name gestures at.** curry is named
   after Haskell Curry, and the Curry-Howard correspondence — "a proof is a
   program, a proposition is a type" — is one of the cleanest ideas in all
   of logic and computer science. Building a theorem prover *inside* curry
   means eventually being able to point at a running program and say "that
   function is a proof," not just talk about the correspondence
   abstractly.
2. **It's a genuinely good way to learn how proof assistants work, and a
   genuinely good way to learn how to *do* mathematical proofs**, by
   building the machinery up one honest layer at a time instead of reading
   Lean/Coq's source cold. Every stage below is a real, complete, usable
   thing on its own — not scaffolding you throw away once the "real" prover
   is done.

This is explicitly **not** a plan to wire curry up to a real Lean/Coq
installation via FFI or subprocess (that would be a fine, useful,
*different* project — "get curry talking to an existing prover" — but it
teaches you how to drive someone else's kernel, not how a kernel works).
This plan is about building the kernel, slowly, by hand, in curry itself.

## Where curry is now

There is no proof-term machinery yet. What already exists and will matter:

- **`(curry symbolic)`** (`src/symbolic.c`, `docs/reference/symbolic.md`) —
  a real CAS with expression trees, substitution, simplification, and
  assumption tracking (`'positive`, `'real`, etc. stored per-variable).
  Stage 3 onward will want *some* of this machinery (expression
  representation, substitution) but proof terms are not CAS expressions —
  expect to borrow patterns, not the code itself.
- **`(curry logic)`** (`lib/curry/modules/curry/logic.scm`,
  see [set-theory-synthesis-plan.md](set-theory-synthesis-plan.md)) —
  pluggable non-classical truth-value logics (classical, Belnap four-valued,
  fuzzy, intuitionistic, probabilistic, defeasible) and a knowledge-base
  layer. This is about *truth values*, not *proofs* — a fundamentally
  different question ("is P true, false, both, neither, 73% likely?" vs.
  "here is a term whose type is a proof of P"). Worth reading before Stage
  1 anyway, since intuitionistic logic's `{refuted, open, proved}` truth
  domain is exactly the three-way distinction constructive proof search
  cares about.
- **`syntax-rules`/`define-record-type`/the module system** — everything
  below is ordinary curry code in a `(curry prover ...)` family of
  libraries, no C required until (if ever) performance demands it.

## The five stages

Each stage is independently real — something you could stop at and still
have learned something and shipped something usable. They get harder in
order; skipping ahead without building the earlier stages is possible but
defeats the point of doing this to learn.

### Stage 1 — Propositional logic prover

Truth tables first (trivial, but establishes "what does it mean for a
formula to be a tautology" concretely, by brute force, before doing
anything clever). Then real proof search: resolution refutation or a
tableau (semantic tree) method, either is a good first "real" algorithm.
Output should be a genuine proof object — a resolution refutation trace or
a closed tableau — not just `#t`/`#f`, even though the logic itself has no
proof *terms* yet.

**What this teaches:** proof search as *search* (backtracking, choosing
which formula to work on next), and the difference between "this is valid"
(semantic, truth-table-checkable for propositional logic) and "here is a
proof" (syntactic, a specific derivation).

**Landing spot:** `(curry prover propositional)`.

### Stage 2 — Natural deduction / sequent calculus

Same logic, different presentation: proofs as trees of inference-rule
applications (`∧-intro`, `∧-elim`, `→-intro` / hypothesis discharge, etc.),
built and checked as actual data structures you can print and inspect —
this is where "a proof is an object" stops being an abstraction. A small,
separate **proof checker** (given a claimed proof tree, verify every step
follows the rules) is worth building distinctly from the **prover** (search
for a proof) — the checker is much smaller and is the piece every later
stage's trust ultimately rests on.

**What this teaches:** proof terms as first-class data; the
checker/searcher distinction that every real proof assistant makes (Lean's
own kernel is small and dumb on purpose — all the cleverness lives in
tactics that *produce* terms for the kernel to check, not in the kernel
itself).

**Landing spot:** `(curry prover natural-deduction)`.

### Stage 3 — Simply-typed lambda calculus, and Curry-Howard made literal

The actual "aha": under Curry-Howard, natural deduction for implication
*is* the simply-typed lambda calculus. `→-intro` is lambda abstraction.
`→-elim` is function application. A well-typed term *is* a proof of its
type, and type-checking a term *is* proof-checking. Build a tiny STLC
type-checker and notice — really notice, by testing it — that it's doing
the same job as Stage 2's checker for the implicational fragment.

**What this teaches:** the correspondence itself, concretely, by
implementing both sides and comparing. This is the stage most worth
writing up publicly once it's done — it's the best "come see something
genuinely beautiful" entry point for bringing someone else into this
project (see below).

**Landing spot:** `(curry prover stlc)`.

### Stage 4 — A small dependently-typed core

The real leap: types that depend on *values*, not just other types
(`Vec n` where `n` is a runtime-ish value; `∀x:Nat. P(x)` where the
proposition genuinely varies per `x`, not just per type). This is roughly
where Lean/Coq/Agda's own kernels live — small (a few thousand lines, by
design — see "small trusted kernel" above), but conceptually the hardest
stage by a wide margin. Budget real time here; this is not a weekend.

A reasonable scoped target: a Martin-Löf-style core with Π-types
(dependent function types), Σ-types (dependent pairs) if there's appetite,
and definitional equality up to β-reduction — deliberately *not* the full
calculus of inductive constructions Lean/Coq actually use, which is a
research-level undertaking on its own.

**What this teaches:** why dependent types are hard (definitional equality
checking, universe levels if you go that far, normalization) and gives
enough real vocabulary to read Lean/Coq/Agda source afterward and actually
follow it.

**Landing spot:** `(curry prover core)`.

### Stage 5 — Tactics

A scripting layer over Stage 4's raw term construction, so proofs can be
written as `(intro x) (apply lemma) (auto)` instead of hand-built terms —
this is what makes a prover *usable* day to day rather than a research
artifact. Tactics *produce* Stage 4 terms; the Stage 4 checker remains the
sole source of trust, exactly like real systems.

**Landing spot:** `(curry prover tactics)`.

## How to actually work this plan

- **One stage at a time, in order.** Each stage's landing spot is a real
  library with its own tests and its own doc page — treat it like any
  other SRFI/module port in this codebase (see
  `docs/reference/writing-a-module.md`): `define-library`, a
  `tests/prover_<stage>_tests.scm` registered in `tests/CMakeLists.txt`, a
  doc page under `docs/reference/`.
- **No stage is required to unlock the next stage's *ideas*** — reading
  ahead is fine and often clarifying — but building Stage N+1's actual code
  before Stage N is solid will mean redoing work later, since Stage N+1
  genuinely depends on Stage N's data structures (a proof term in Stage 3
  is built directly out of Stage 2's inference-rule vocabulary, etc).
- **Small, working, and honest beats large and aspirational.** A truth-table
  checker that handles ten propositional variables and nothing else is a
  legitimate, mergeable Stage 1 contribution. Don't wait to have "the whole
  prover" figured out before writing code.

## Bringing other people in

This project is unusually good for onboarding contributors, for a specific
reason: **every stage is a self-contained, well-understood, extensively
documented piece of computer science with decades of textbook treatment**,
so a new contributor doesn't need to trust curry's own design taste the way
they might for, say, a novel GC algorithm — they can go read a textbook
chapter on resolution refutation, or natural deduction, or STLC, that
matches this plan's stage almost 1:1, and arrive already knowing roughly
what "done" looks like.

Suggested ways to pitch a specific stage to someone:

- **Stage 1 (propositional prover):** "Have you done any classical logic —
  truth tables, resolution? This is a self-contained weekend project that
  teaches you proof search." Good first-contribution size; no dependency on
  anything else in this plan.
- **Stage 2 (natural deduction):** "Want to see what it means for a proof
  to literally be a data structure you can print?" Good for someone who
  already knows some formal logic (a CS or math undergrad course covers
  this) but hasn't implemented it.
- **Stage 3 (STLC / Curry-Howard):** the single best stage to show off —
  "this function IS this proof, watch" is a genuinely striking thing to see
  work for the first time. Good pitch to a functional-programming person
  who's heard of Curry-Howard but never seen it made concrete.
- **Stage 4 (dependent types):** pitch only to someone who already wants to
  understand how Lean/Coq/Agda's kernels work, or already has some type
  theory background — this is the research-adjacent stage, not a starter
  task.
- **Stage 5 (tactics):** good for someone who likes language/DSL design
  more than theory — tactics are "just" an interpreter over a small
  command language that happens to build proof terms.

A short, concrete artifact from Stage 3 (a handful of STLC terms next to
the propositions they prove, with a one-paragraph "here's what
Curry-Howard means" writeup) is probably the single best thing to actually
show someone to get them curious enough to look at the rest of this plan.

## Non-goals (for now)

- No FFI bridge to a real Lean/Coq/Agda — a fine project, a different one.
- No attempt at the full calculus of inductive constructions in Stage 4 —
  scoped down to Π/Σ-types deliberately.
- No performance work until something is slow *and* working — every stage
  above is plain curry Scheme; a C module is not on the table unless a
  later stage's proof checker turns out to need it for real workloads.
