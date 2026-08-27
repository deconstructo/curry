# Module: (curry gillespie)

*unreleased*

Stochastic simulation of cell biochemistry via the Gillespie algorithm (SSA):
a set of chemical species undergoing reactions, modeled as a continuous-time
Markov chain rather than a system of smooth ODEs. This matters because real
gene expression is noisy at low molecule counts — a handful of mRNA copies,
not a continuous concentration — and the Gillespie algorithm simulates
individual random reaction events instead of averaging them away.

Pure Scheme, no C extension. See
[`docs/thoughts/gillespie-cell-model.md`](../thoughts/gillespie-cell-model.md)
for the design rationale, an SBML-import idea, and how this composes with a
(currently unimplemented) toy population-genetics model.

## Import

```scheme
(import (curry gillespie))
```

## The core mechanism

Each reaction has a *propensity*: a probability-per-unit-time that it fires
next, given the current state. At each simulation step:

1. Sum every reaction's current propensity → `a0`
2. Draw an exponentially-distributed waiting time, `τ = -ln(random-real)/a0`
3. Pick which reaction fires, weighted by its own share of `a0`
4. Apply it (decrement reactant counts, increment product counts), advance
   time by `τ`, repeat

A propensity is just an ordinary procedure `(species environment) ->
nonnegative-real` — there's no separate mini-language for reaction kinetics.
That's also the entire mechanism behind environment sensitivity: temperature,
pH, and nutrient level aren't bolted on afterward, they're ordinary inputs a
propensity procedure is free to read.

## Reactions

### `(make-reaction name reactants products propensity)` → reaction

- `reactants`, `products` — alists of `(species-symbol . stoichiometry)`,
  e.g. `(list (cons 'A 2))` for two molecules of `A`
- `propensity` — a procedure `(species environment) -> nonnegative-real`,
  typically built from the combinators below rather than written by hand

### `(reaction? v)`, `(reaction-name r)`, `(reaction-reactants r)`, `(reaction-products r)`, `(reaction-propensity r)`

Accessors.

## Environment

### `(make-environment temperature ph nutrients)` → environment

Three fields: `temperature` (Kelvin), `ph` (dimensionless), `nutrients` (a
plain number — meant to double as an extra pool a reaction's propensity
reads and an uptake reaction depletes, the same way any other species would
be, just kept separate from the species table for convenience). Want more
environment axes? Fold them into the species table itself instead (e.g.
"oxygen" as an ordinary species) rather than extending this record.

### `(environment-temperature e)`, `(set-environment-temperature! e v)`
### `(environment-ph e)`, `(set-environment-ph! e v)`
### `(environment-nutrients e)`, `(set-environment-nutrients! e v)`

Accessors/mutators. Mutating an environment shared by multiple cells (see
[Notes](#notes)) affects every cell reading it on their next propensity
evaluation — there's no snapshot/versioning, it's a plain mutable record.

## Species tables

A species table is a plain hash-table (`symbol -> count`); these helpers
just wrap `hash-table-ref`/`hash-table-set!` with a `0` default so a species
that's never been touched reads as absent-but-zero rather than an error.

### `(make-species alist)` → species table

```scheme
(make-species (list (cons 'A 10) (cons 'B 0)))
```

### `(species-count species key)` → integer

Defaults to `0` for a key that was never set.

### `(set-species-count! species key n)`
### `(adjust-species! species key delta)`

`adjust-species!` is `set-species-count!` composed with a `+`; used
internally by reaction application, exported since it's equally useful for
setting up a simulation's initial perturbations by hand.

## Cells

### `(make-cell species reactions environment time)` → cell

`species` a species table, `reactions` a list of `<reaction>`, `environment`
an `<environment>`, `time` the starting simulation time (almost always `0`).

### `(cell? v)`, `(cell-species c)`, `(cell-reactions c)`, `(cell-environment c)`, `(cell-time c)`, `(set-cell-time! c t)`

Accessors. `cell-species` returns the live, mutable hash-table — reactions
mutate it in place as the simulation runs.

## Composable rate-law combinators

Every combinator returns a propensity procedure `(species environment) ->
nonnegative-real`. There's no combinator algebra beyond "these are plain
procedures" — build a richer propensity by composing smaller ones with
`rate*`, or by writing an ordinary `lambda` that calls a few of them itself.

### `(mass-action k reactants)` → propensity procedure

The standard elementary-reaction rate law: `k` times the combinatorial term
for each reactant. A reactant with stoichiometry 1 contributes its raw
count; stoichiometry `n` contributes `C(count, n)` (the binomial
coefficient) — e.g. `2A → B` has propensity `k·C(nₐ,2) = k·nₐ(nₐ−1)/2`, not
`k·nₐ²`, since two molecules of the same species must be drawn without
replacement. Correctly evaluates to `0` (not an error, and not a nonzero
value from an unmultiplied accumulator — see the note in the source on the
bug this used to have) whenever any reactant's count is below its required
stoichiometry.

```scheme
(mass-action 5.0 '())                    ; zero-order: propensity is a flat 5.0
(mass-action 2.0 (list (cons 'A 1)))     ; first-order: 2.0 * n_A
(mass-action 1.0 (list (cons 'A 2)))     ; second-order, same species: C(n_A, 2)
```

### `(arrhenius A Ea)` → propensity procedure

Temperature-dependent rate constant, `k(T) = A · exp(−Ea / (R·T))` (R =
8.314 J/(mol·K)). Returns `0` for `temperature = 0` rather than dividing by
zero — the physically correct limit as `T → 0⁺` for `Ea > 0`, not an
arbitrary guard value. Meant to be combined with `mass-action` via `rate*`
to make a reaction temperature-sensitive:

```scheme
(rate* (mass-action 1.0 reactants) (arrhenius 1e13 50000.0))
```

### `(michaelis-menten vmax km substrate-key)` → propensity procedure

Saturating enzyme/uptake kinetics: `vmax·S / (km+S)`, where `S` is
`(species-count species substrate-key)`. Approaches `vmax` as substrate
grows large, approaches `0` as it's depleted — half of `vmax` exactly when
`S = km`.

### `(hill env-reader optimal width)` → propensity procedure

A bell-curve multiplier peaking at `1.0` when `(env-reader environment)`
equals `optimal`, falling off symmetrically over `width`. `width = 0` returns
the curve's own limit — a delta function, `1.0` exactly at `optimal` and `0`
everywhere else — rather than dividing by zero. The general shape behind
"this reaction has an optimal pH" (or any other environment axis with an
optimum rather than a monotonic scaling):

```scheme
(hill environment-ph 7.0 1.0)          ; peaks at pH 7, roughly e^-2 by pH 9
(hill environment-temperature 310.0 5.0) ; an enzyme with an optimal temperature
```

### `(rate* proc ...)` → propensity procedure

Multiplies any number of propensity procedures' outputs together — the
actual composability mechanism. A temperature-sensitive, pH-sensitive,
nutrient-saturating reaction is:

```scheme
(rate* (mass-action k reactants)
       (arrhenius A Ea)
       (hill environment-ph 7.0 1.0)
       (michaelis-menten vmax km 'glucose))
```

## Simulation

### `(gillespie-step! cell)` → boolean

Runs one step of the algorithm above, mutating `cell` in place. Returns `#f`
(and changes nothing at all — not even the time) when every reaction's
propensity is currently `0`, e.g. a nutrient pool has run out and nothing
can react any more. A caller can treat "the cell went quiescent" as an
ordinary checkable outcome rather than an error or an infinite loop.

### `(gillespie-run! cell t-max)` → real (the final time reached)

Steps until `(cell-time cell)` reaches `t-max` or the cell goes quiescent.
The return value is `< t-max` exactly when the run ended by quiescence
rather than reaching `t-max` — check it if you need to distinguish the two.

### `(cell-trajectory cell t-max dt)` → list of `(time . ((species . count) ...))`

Runs the simulation to `t-max`, recording a full species snapshot every `dt`
time units — not every reaction event, which for a fast network could be
thousands of points per unit of biological time. `dt` must be positive; a
zero or negative `dt` raises immediately rather than looping forever.
Sample times are computed as `i·dt` for an integer step count, not by
repeatedly adding `dt` to itself, so the sample count is exact even for a
`dt` that isn't an exact binary fraction (e.g. `0.1`). Returned oldest-first,
directly usable as a plotting data series (a Qt canvas timer callback, a CSV
writer, whatever). Mutates `cell` in place, same as `gillespie-run!`. Once
the cell goes quiescent, remaining samples reuse one cached snapshot rather
than recomputing every reaction's propensity for no new information.

## Notes

- **Multi-cell simulation** isn't a separate API here — spawn one curry actor
  per cell, each running its own `gillespie-step!`/`gillespie-run!` loop, and
  use `send!`/`receive` for intercellular signaling. If cells share a single
  `<environment>` (competing for one nutrient pool), protect concurrent
  mutation of it with `(curry stm)` rather than mutating it unprotected from
  multiple actor threads.
- **This is a real stochastic process** — two calls to `gillespie-run!` with
  identical starting conditions will not produce identical trajectories
  unless you explicitly reseed with `random-source-pseudo-randomize!`
  beforehand (see `(curry random)`'s underlying SRFI-27 primitives).
  Statistical properties (steady-state means, variance) are reproducible
  across many replicates; individual trajectories are not, by design.
- **No built-in unit conversion or validation** — species counts, rate
  constants, and environment values are plain numbers with whatever units
  you decide on; nothing here checks that an `Ea` is in the right unit
  system for the `R` used internally, for instance. Get your units
  consistent before wiring reactions together.

## See also

- [`gillespie-cell-model.md`](../thoughts/gillespie-cell-model.md) — design
  rationale, the SBML-import idea, and how this composes with a
  population-genetics model
- [`module-random.md`](module-random.md) — the SRFI-27 primitives
  (`random-real`, `random-source-pseudo-randomize!`) this module's waiting-time
  draw builds on directly
- [`concurrency.md`](concurrency.md) — actors, STM, and `(curry stm)`'s
  `or-else`/`select` sugar, for protecting a shared `<environment>` across
  multiple cell-actors
