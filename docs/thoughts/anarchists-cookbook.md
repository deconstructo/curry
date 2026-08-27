# The Anarchist's Curry Cookbook — Ideas

A chapter-by-chapter ideas file. Chapters are not ordered; each should be
self-contained, runnable, and demonstrate something you can't easily do in
any other Scheme.

---

## Chapter: User-Defined Algebras (v1.4)

*What v1.4 unlocked: `define-rule`, `define-algebra`, `with-assumptions`,
polynomial machinery, Risch integration, special functions.*

The pitch: Curry's CAS is now user-extensible at the Scheme level. You can
declare a new algebra with a handful of rewrite rules and immediately get
symbolic simplification, differentiation, integration, and series expansion
for free — no C required.

### Recipe ideas

**Grassmann / exterior algebra**
Define `∧` with nilpotency (`e∧e = 0`) and anticommutativity
(`e_i∧e_j = −e_j∧e_i` for i<j) as `define-rule` entries. Then:
- Show `(e₁+e₂)³ = 0`
- Compute a 4×4 Pfaffian symbolically
- Build the exterior derivative `d` and verify `d∘d = 0`
- Connection to the Clifford algebra already in the numeric tower

**Weyl algebra (canonical commutation)**
Single rule `[∂, x] = 1` (as `∂x − x∂ = 1`). Then derive:
- Normal ordering: move all `x`s left, all `∂`s right
- Baker–Campbell–Hausdorff at low order
- The harmonic oscillator: `H = ∂² − x²`, eigenfunctions are Hermite
  polynomials — close the loop with the new `hermite` function
- Connection to the `wirtinger-d` / `wirtinger-dbar` already in the CAS

**Tropical algebra**
Redefine addition as `min`, multiplication as `+`. Rules:
```scheme
(define-ruleset tropical
  [(⊕ ?a ?b) (min ?a ?b)]
  [(⊗ ?a ?b) (+ ?a ?b)])
```
- Tropical matrix multiplication = shortest-path (Bellman–Ford for free)
- Tropical polynomial roots = Newton polygon breakpoints
- Exotic and completely non-classical — great "wait, what?" demo

**q-deformed / quantum plane**
Rule: `xy = q·yx` for symbolic `q`. Derive:
- `(x+y)ⁿ` in terms of Gaussian binomial coefficients
- Specialise `q → 1` to recover classical, `q → 0` for something else
- Preview of quantum groups

**Finite field GF(p) arithmetic**
`define-rule` for reduction mod p. Then:
- Polynomial factoring over GF(p) via `poly-factor`
- Reed–Solomon code construction
- Berlekamp's algorithm (stretch goal)

**Lie algebra from structure constants**
User supplies `[eᵢ, eⱼ] = Σ cᵢⱼᵏ eₖ`. Rules encode the bracket.
- Verify Jacobi identity automatically
- su(2): raise/lower operators, Casimir element
- Connects to the SICM material already in the tests

### Connecting thread for the chapter
Show that all five algebras above share the same substrate — `define-rule`,
`with-assumptions`, and `∂`/`∫`. A reader who builds all five will have
internalised the full v1.4 CAS interface.

---

---

## Chapter: The Laws Are Not What They Seem — A Rogue's Gallery of Truth

*Full chapter in `anarchists-cookbook-logic.md`.*

Six first-class logic systems: classical, Belnap paraconsistent (four-valued; no
explosion), fuzzy (Łukasiewicz/Gödel), intuitionistic (constructive; ¬¬P ≠ P), Bayesian
probabilistic, and defeasible (priority-weighted defeat). Logics are runtime values;
swap them with `with-logic`. Knowledge bases accumulate evidence under any logic with
`make-kb`/`kb-assert!`; contradictions are detected, not catastrophic. Connects to
the algebraic structure of the CAS — a logic is a Heyting algebra, just like a user
algebra defined with `define-algebra`.

### Recipe ideas

**Contradiction-tolerant log aggregator** — merge structured logs from multiple services
under `belnap-four`; contradictions in field values surface as `'B` rather than crashing.

**Constructive type checker** — use `intuitionistic-logic` to track proof obligations;
a term is well-typed only when its type proof is `'proved`, not merely "not refuted".

**Fuzzy access control** — `fuzzy-logic` for continuous trust scores; adjust the
entailment threshold per security zone.

**Penguins all the way down** — defeasible rule inheritance for a configuration system;
global defaults defeated by group policy defeated by user-specific overrides.

**Naive Bayes spam classifier** — `probabilistic-logic`; each word in the message
triggers a `kb-assert!`; the posterior after `kb-close!` is the spam probability.

**Multi-logic oracle** — a function that runs the same query under all six logics and
returns the ensemble verdict; useful for sanity-checking or building meta-classifiers.

---

## Chapter: The Boundaries Are Not What They Seem — Set Theory for the Disenchanted

*Full chapter in `anarchists-cookbook-sets.md`.*

Classical set membership is a *choice*, not a law of nature. This chapter unpacks the
three-layer set theory built into Curry and shows how each layer unlocks a different
class of problem that binary membership cannot express.

**Layer 1 — Core hash-sets**: the full SRFI-inspired API, now with all higher-order ops
(`set-map`, `set-filter`, `set-fold`, `set-any?`, `set-every?`, `set-count`, `set-find`),
`set-adjoin`/`set-adjoin!`, `set-delete`, `set-symmetric-difference`, `set-copy`.

**Layer 2 — Multisets**: elements with integer multiplicities; two union algebras (max
vs. +); word frequency, inventory diff, histogram normalization, monomial multiplication.

**Layer 3 — Logical sets** (`(curry sets)`): membership returns a truth value in any
`(curry logic)` logic. Classical → plain set. Fuzzy (Zadeh 1965) → graded membership
with alpha-cuts for re-entry to classical land. Belnap → paraconsistent; contradiction
contained, not catastrophic. Probabilistic → confidence-weighted membership. Rolling your
own: rough sets, interval sets, signed multisets — all from `make-logic`.

### Recipe ideas

**Fuzzy access control** — trust scores as membership degrees; one dataset, two alpha-cut
thresholds give you normal-access and critical-access lists without code duplication.

**Paraconsistent data warehouse** — ETL from inconsistent sources; contradictions surface
as `'B`, `belnap-set-contradictions` routes problem records to human review without
corrupting clean records.

**Constructive set of provable facts** — `intuitionistic-logic`; `'proved` / `'open` /
`'refuted` tracks the epistemic state of each proposition; Collatz demonstration.

**Histogram of histograms** — multiset → `histogram-of-histogram` → converges toward
Zipf distribution; recursive structure of natural language in six lines.

---

---

## Chapter: Watch a Cell Think — Stochastic Biochemistry with the Gillespie Algorithm

*Full module in `(curry gillespie)`, design doc in
`docs/thoughts/gillespie-cell-model.md`.*

The pitch: real gene expression is noisy. A cell doesn't have "5.3 copies of a
protein," it has 4, then 6, then 3 — a small integer jumping around at random,
because chemistry at low molecule counts genuinely is a stochastic process,
not a smooth curve with noise sprinkled on afterward. The Gillespie algorithm
simulates that directly: draw a random waiting time, pick which reaction
fires, repeat. Composable rate-law combinators (`mass-action`, `arrhenius`,
`michaelis-menten`, `hill`, all glued together with `rate*`) mean temperature,
pH, and nutrient sensitivity aren't special features — they're just more
functions being multiplied into a propensity, so a reader who understands
`rate*` can build essentially arbitrary biochemistry from four small pieces.

This is exactly the kind of thing an eager nerdy reader can just... play
with. Change one number, rerun, watch the population statistics shift. Cool
the environment down and watch a whole reaction network visibly slow to a
crawl. Starve a cell of glucose and watch it go quiescent mid-simulation with
no crash, no special-cased error path — just an ordinary `#f` from
`gillespie-step!` because every propensity in the network genuinely reached
zero.

### Recipe ideas

**The birth-death "hello world"** — one production reaction, one
degradation reaction, and a known analytical answer (`λ/μ`) to check your
simulation against. The right first thing to run, and a good sanity check
whenever you write a new reaction network: does the steady state land near
where the math says it should?

**A genetic toggle switch** — the classic synthetic-biology circuit: two
genes that mutually repress each other, so the system settles into one of
two stable states essentially at random depending on early noise. Build it
with four reactions (transcribe-A, transcribe-B, degrade-A, degrade-B, each
gated by the other's current level) and watch fifty independent cells split
roughly in half between the two states — a beautiful, tiny demonstration of
how stochastic noise can create genuine bistability from a symmetric system.

**A population under thermal stress** — spawn N cell-actors (see
`docs/reference/concurrency.md`), each running the same reaction network,
sharing one `<environment>` whose temperature you ramp up over the course
of the simulation via `set-environment-temperature!`. Watch the population's
average protein output curve bend as an Arrhenius-scaled reaction crosses
its effective activation threshold.

**Starve it and watch it stop** — a network with one nutrient-uptake
reaction (`michaelis-menten`) feeding everything else. Run to depletion and
plot the trajectory: watch every downstream reaction's activity taper off
in lockstep as the shared substrate runs out, then confirm the cell has
gone genuinely quiescent (`gillespie-run!`'s return value `<` the requested
`t-max`) rather than just "slow."

**Live in Qt** — wire `cell-trajectory`'s output (or a live per-tick
`gillespie-step!` loop) into a `(curry qt6)` canvas: cells as colored
circles, color channels driven by species counts, so gene-expression noise
is something you *watch flicker* rather than a number in a table.

### Connecting thread for the chapter

Every recipe above is the same four moving parts — a species table, a
reaction list built from `mass-action`/`arrhenius`/`michaelis-menten`/`hill`
composed with `rate*`, an environment, and either `gillespie-run!` for a
single endpoint or `cell-trajectory` for the full time series. A reader who
builds the birth-death toy and the toggle switch has already seen everything
needed for the population and starvation recipes; scaling up to many cells
is "wrap it in `spawn`," not a new concept.

---

## Other chapter seeds (unrelated to v1.4)

*(Drop ideas for other chapters here as they come up)*

- **Babylonian astronomy in Curry** — sexagesimal reader + the MUL.APIN
  tablet series; already have the cuneiform Unicode support
- **4D maze** — the raycaster series; maze4d roadmap is in thoughts/4d-maze.md
- **Physics on a Raspberry Pi** — GPIO + symbolic ODE solver; RPI.md has the plan
- **MCP servers as Scheme one-liners** — the mcp module makes this surprisingly terse
