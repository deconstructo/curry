# Set theory synthesis — plan

*Drafted 2026-08-31. Status: planning only, nothing implemented yet.*

## Why

`docs/roadmap.md` Phase 7 already records that pluggable set-theory
foundations "shipped differently-shaped" in v1.7.0 — as a pure-Scheme
`(curry logic)`-parameterized mechanism rather than the C vtable originally
sketched — and that ordinals, cardinals, ZFC-style bounded comprehension,
and a foundations layer proper were never built. This was revisited after
comparing against `cill_spec.pdf` §18 (Set Theory — Pluggable Foundations),
the design doc for a from-scratch sibling project (Cill) that was
prototyped and abandoned (poor implementation) but whose design was kept
because it was clean.

Cill's set-theory design and curry's actual set-theory code turn out to be
strong in complementary places, not simply "ahead of" and "behind" each
other. This plan is about combining them rather than porting one wholesale.

## Where curry is now

Spread across three modules, none badged "set theory":

- **`SRFI-113`** (`lib/curry/modules/srfi/s113/sets-and-bags.scm`) — crisp,
  comparator-based sets/bags: construction, union/intersection/difference,
  subset/equal predicates. This is the plain-set layer.
- **`(curry sets)`** — multisets (hash-backed element→count bags, full
  arithmetic: union/intersection/sum/difference/scale/map/filter/fold), plus
  **logical-sets**: sets whose membership is graded by a pluggable logic
  from `(curry logic)`, threaded via `current-logic`/`with-logic`.
- **`(curry logic)`** — six first-class non-classical logics (classical,
  Belnap four-valued, fuzzy, fuzzy-product, intuitionistic,
  probabilistic/Bayesian, defeasible), each supplying meet/join/complement/
  implies/entails? for its truth-value domain, plus a small knowledge-base
  layer (`make-kb`, `kb-assert!`, `kb-consistent?`, ...).

Missing entirely: ordinals, cardinals, a foundations layer (naive vs. ZFC —
i.e. no comprehension discipline, `{x : x∉x}` is simply not representable
one way or the other because there's no comprehension primitive to guard),
relations (composition, closures, equivalence classes, order predicates),
and named infinite sets (`integers`, `reals`, ... as first-class set
objects with membership tests).

## Where Cill is (design only — never built past a spec)

§18: a C vtable (`cill_foundations_ops_t`) swapped at startup between naive
(unrestricted comprehension, Russell's-paradox-is-your-problem) and ZFC
(bounded comprehension — `set-comprehension` requires an existing domain
set, raises otherwise; regularity enforced so `(set-member? S S)` is always
`#f`; axiom of choice present but flagged). On top: foundation-agnostic
crisp set ops (union/intersection/difference/complement/symmetric-difference,
subset/proper-subset/equal/disjoint, cardinality returning an integer or
`'aleph-0`/`'beth-1`/..., finite?/countable?/same-cardinality?, map/filter/
fold), ordinals and cardinals (`ordinal 'omega`, non-commutative
ordinal +/*, `cardinal-expt`, continuum hypothesis explicitly represented as
a flagged assumption rather than either proved or silently assumed),
relations (compose/inverse/transitive-closure/equivalence-closure, reflexive/
symmetric/transitive/partial-order/total-order/well-founded predicates,
equivalence-classes/quotient-set), and — separately, not unified with
anything else — bare fuzzy sets (a raw membership function, `fuzzy-union`/
`-intersection`/`-complement` as max/min/1-minus).

## The actual comparison

**curry's graded-membership design is better than Cill's.** Cill's fuzzy
sets are a one-off bolted next to crisp sets with their own union/
intersection defined ad hoc as max/min. curry's fuzzy sets are just
`(curry logic)`'s general graded-membership machinery applied to the fuzzy
logic instance — so paraconsistent, probabilistic, intuitionistic, and
defeasible-logic sets all fall out of the *same* mechanism for free. Cill
has nothing structurally equivalent; adding a seventh kind of graded set to
Cill's design means writing a seventh bolt-on, not reusing anything.

**Cill's axiomatic layer is better than curry's, because curry doesn't have
one.** Comprehension discipline, ordinals/cardinals, and relations are all
zero-coverage gaps in curry today, and Cill's spec for that layer (however
un-implemented) is a clean, usable shape to build from.

These are genuinely orthogonal axes, not two takes on the same problem:
**foundations** governs what comprehension is legal (a syntactic/logical
discipline question — is `{x : P(x)}` well-formed without a domain?);
**logic** governs the truth value of a specific membership claim once
comprehension has already produced a set (a semantic question — is
`x ∈ S` true, false, 0.7-true, contradictory, ...?). A ZFC-foundation set
graded by fuzzy membership is a coherent, meaningful object — "the ZFC-legal
set of things at least 0.7-fuzzy-true of being prime" — and neither system
currently models that combination because each only has one of the two
axes.

## Synthesis plan

### Principle: two independent dynamic-extent parameters, not one

Add `current-foundations`/`with-foundations` alongside the existing
`current-logic`/`with-logic`, following the *exact* precedent curry already
set with logics — a Scheme-level parameter object, not a C vtable. The
roadmap already concluded the C vtable is "optional, not required follow-up
work" unless a non-Scheme-level foundation implementation is ever needed;
nothing about this plan changes that conclusion, it just finally builds the
Scheme-level version the roadmap called for.

```scheme
(with-foundations 'zfc
  (set-comprehension x (integers) #:where (prime? x)))   ; fine
(with-foundations 'zfc
  (set-comprehension x #:where (prime? x)))               ; error — no domain

(with-foundations 'naive
  (set-comprehension x #:where (not (set-member? x x)))) ; permitted; Russell's
                                                           ; set loops/errors
                                                           ; on actual use,
                                                           ; not construction —
                                                           ; matches Cill's own
                                                           ; documented choice
```

Default foundation: `naive`, matching curry's existing unrestricted
`SRFI-113`/`(curry sets)` behavior — this is additive, not a breaking change
to anything that already works.

### Ordinals: reuse the surreal-number tower, don't duplicate it

curry's surreal numbers (`src/surreal.h`, `src/surreal.c`) already have
`SUR_OMEGA` (ω) and `SUR_EPSILON` (1/ω) as first-class Hahn-series values
with real arithmetic defined on them. Cill's ordinal layer wants exactly
this concept — ω as a value, non-commutative arithmetic on transfinite
quantities. Rather than a parallel `<ordinal>` type duplicating "a thing
called ω with its own arithmetic" a second time, model ordinals as a
distinguished view/wrapper over the existing surreal representation:
`(ordinal 'omega)` returns (or wraps) the existing `SUR_OMEGA` value,
`ordinal+`/`ordinal*` are defined as non-commutative wrappers around the
existing surreal arithmetic rather than a new C type.

This needs a concrete feasibility check before committing to it as
more than a plan: surreal numbers are a *field*-like Hahn-series
construction (commutative addition, in particular) built for numeric/
calculus use (forward-mode autodiff via ε), while ordinal arithmetic is
famously **non-commutative** (`1 + ω = ω ≠ ω + 1`). If the existing surreal
representation's addition is unconditionally commutative at the C level,
"wrapping" it cannot produce correct ordinal arithmetic — the wrapper would
need to track which operand order was used and apply ordinal-specific
normal-form rules (Cantor normal form) on top, which may end up being most
of a new implementation anyway. Phase 3 below starts with exactly this
feasibility question rather than assuming the reuse works.

### Cardinals: new, but small

No existing curry type covers cardinal arithmetic (aleph/beth, `beth-1 =
2^aleph-0`). This is a small, self-contained addition — symbolic tags
(`'aleph-0`, `'aleph-1`, `'beth-1`, ...) plus a handful of comparison/
arithmetic rules, most of which are fixed facts (`aleph-0 + aleph-0 =
aleph-0`, `2^aleph-0 = beth-1` by definition) rather than a general
algorithm. The continuum-hypothesis-as-flagged-assumption idea from Cill
(`(assuming 'continuum-hypothesis (assert (cardinal= ...)))`) is worth
keeping as-is — it fits curry's existing CAS assumptions system
(`with-assumptions`, `assume!`) almost exactly as a new assumption keyword,
no new mechanism needed.

### Relations: new small module, `(curry relations)`

Composition, inverse, transitive/equivalence closures, reflexive/symmetric/
transitive/partial-order/total-order/well-founded predicates,
equivalence-classes, quotient-set — sits on top of existing `SRFI-113` sets
plus pairs, no dependency on the foundations/logic work above. This is also
the direct prerequisite for the confirmed topology gap (§19 of Cill; "not
started" in curry's own roadmap) — a quotient topology needs quotient sets,
which need equivalence classes, which need this module. Building it now
means topology work later doesn't stall on a missing dependency.

### Named infinite sets: wire up existing predicates, no new representation

`integers`, `naturals`, `rationals`, `reals` as symbolic set objects with
working `set-member?` — implemented as thin wrappers over predicates the
CAS assumptions system already has (`real?`, `integer?` assumptions already
exist and are used in `simplify`/`with-assumptions`). Not a new numeric
representation, just a `<named-set>` record whose membership test dispatches
to an existing predicate.

## Phased plan

Each phase its own PR off `origin/main`, independent code+security review,
full `ctest --clear-cache` run, per the usual project workflow.

### Phase 1 — `current-foundations`/`with-foundations`
Naive (default, current behavior) and ZFC (bounded comprehension enforced,
regularity enforced so `set-member?` on a set against itself is always
`#f`, axiom-of-choice flagged via `with-axiom-of-choice`) as the two
foundations. `set-comprehension` gains the domain-required check under ZFC.
*Estimate: 1–2 weeks.*

### Phase 2 — `(curry relations)`
Composition/inverse/closures/order-predicates/equivalence-classes/
quotient-set over existing `SRFI-113` sets. No dependency on Phase 1.
Can be done in parallel with it, or first — it's the more clearly bounded
and more clearly useful-elsewhere (topology) of the two.
*Estimate: 1.5–2 weeks.*

### Phase 3 — Ordinals feasibility spike, then implementation
Concrete spike: can `SUR_OMEGA`/`SUR_EPSILON`'s existing arithmetic be
wrapped to produce correct non-commutative ordinal addition/multiplication
(Cantor normal form), or does ordinal arithmetic need its own
representation after all? Answer this before committing to either path.
If reuse works: thin wrapper module. If not: small standalone
`<ordinal>` type, scoped to what Cill's spec actually needs (omega,
ordinal +/*, expt) rather than a general ordinal-arithmetic library.
*Estimate: 3–5 days spike, then 1–3 weeks implementation depending on the
spike's answer.*

### Phase 4 — Cardinals
Symbolic aleph/beth tags, fixed arithmetic facts, `cardinal-expt`,
continuum-hypothesis as a new CAS assumption keyword. Depends on nothing
above except reusing the existing assumptions system.
*Estimate: 1 week.*

### Phase 5 — Named infinite sets
`integers`/`naturals`/`rationals`/`reals` as `<named-set>` wrappers over
existing predicates. Small, mostly plumbing.
*Estimate: 3–5 days.*

## Total estimate

Roughly 6–10 weeks depending on how Phase 3's feasibility spike resolves.
Phases 1, 2, 4, 5 are independent of each other and can be parallelized or
reordered freely; Phase 3 is the only one with real design risk.

## Relationship to topology (Phase 7 of the main roadmap)

Topology (`docs/roadmap.md`, confirmed "not started") is layered directly
on top of this work in both curry's and Cill's designs: topological spaces
are built from sets, and Cill's `product-topology`/`quotient-topology`
need exactly the relations/equivalence-classes machinery from Phase 2 here.
Doing this set-theory synthesis first is what makes a future topology pass
tractable rather than needing its own from-scratch set/relations layer.

## Non-goals

- **NFU, constructive (Martin-Löf), HoTT foundations** — Cill lists these as
  named future slots with no design content behind them. Not worth
  speculatively designing curry equivalents; revisit only if a concrete need
  arises.
- **A C-level foundations vtable** — per the roadmap's existing conclusion,
  stays optional/unbuilt unless a genuine non-Scheme-level pluggability need
  shows up.
- **General ordinal arithmetic beyond what Cill's spec actually shows**
  (omega, +, *, expt, continuum hypothesis) — no open-ended ordinal-theory
  library.
