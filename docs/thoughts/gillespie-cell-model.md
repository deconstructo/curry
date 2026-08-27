# `(curry gillespie)` — stochastic simulation of cell biochemistry
## Composable rate laws, environment sensitivity, multi-cell via actors, and SBML import

*Drafted 2026-08-27. Status: pre-implementation design sketch (base module is
being implemented alongside this doc).*

---

## What it is

The Gillespie stochastic simulation algorithm (SSA) models a set of chemical
species undergoing reactions as a continuous-time Markov chain: instead of
solving smooth ODEs for concentrations, it simulates individual random
reaction events (production, degradation, binding), which matters
biologically because real gene expression is noisy at low molecule counts —
a handful of mRNA copies, not a continuous concentration.

The core mechanism: each reaction has a *propensity* (a probability-per-unit-
time that it fires next, given the current state). At each step, draw an
exponentially-distributed waiting time from the sum of all propensities,
pick which reaction fires weighted by its own share of that sum, update
species counts, repeat.

## Why environment sensitivity falls out for free

A propensity is just a function of current state, so temperature, pH, and
nutrient level aren't bolted on afterward — they're ordinary inputs to the
exact mechanism that already decides which reaction fires when:

- **Temperature** — Arrhenius scaling, `k(T) = A · exp(−Ea / (R·T))`
- **pH** — a bell-curve enzyme-activity multiplier evaluated at current pH
- **Nutrients** — modeled as just another species (external glucose,
  consumed by uptake reactions); propensities naturally drop to zero when
  it's depleted, and the cell's metabolism visibly stalls with no special
  casing needed

## Core data model

```scheme
;; A reaction: propensity is a function of (species-state environment)
(define-record-type <reaction>
  (make-reaction name reactants products propensity)
  reaction?
  (name        reaction-name)
  (reactants   reaction-reactants)   ; ((species . stoich) ...) consumed
  (products    reaction-products)    ; ((species . stoich) ...) produced
  (propensity  reaction-propensity)) ; (lambda (species env) -> nonneg-real)

(define-record-type <environment>
  (make-environment temperature ph nutrients)
  environment?
  (temperature environment-temperature set-environment-temperature!)
  (ph          environment-ph)
  (nutrients   environment-nutrients set-environment-nutrients!))

(define-record-type <cell>
  (make-cell species reactions environment time)
  cell?
  (species     cell-species)      ; hash-table: symbol -> count
  (reactions   cell-reactions)
  (environment cell-environment)
  (time        cell-time set-cell-time!))

(define (gillespie-step! cell)
  (let* ((props (map (lambda (r) ((reaction-propensity r)
                                   (cell-species cell) (cell-environment cell)))
                      (cell-reactions cell)))
         (a0 (apply + props)))
    (and (positive? a0)
         (let ((tau (/ (- (log (random-real))) a0))
               (r   (%pick-weighted (cell-reactions cell) props a0)))
           (set-cell-time! cell (+ (cell-time cell) tau))
           (%apply-reaction! cell r)
           #t))))
```

## Composable rate-law combinators

The point of making this composable: a propensity function is just a plain
closure `(species env) -> real`, so building richer behavior is ordinary
function composition, not a special mini-language:

```scheme
(define (mass-action k)
  (lambda (species env) (* k (%reactant-product species))))

(define (arrhenius base-rate-fn A Ea)
  (lambda (species env)
    (* (base-rate-fn species env)
       A (exp (- (/ Ea (* 8.314 (environment-temperature env))))))))

(define (michaelis-menten vmax km substrate-key)
  (lambda (species env)
    (let ((s (hash-table-ref species substrate-key 0)))
      (/ (* vmax s) (+ km s)))))

;; compose: a temperature-sensitive, nutrient-saturating uptake reaction
(define uptake-propensity
  (arrhenius (michaelis-menten 10.0 5.0 'glucose) 1e13 50000))
```

## Multi-cell: actors + STM, not new machinery

Curry already has real actor concurrency (`spawn`, `send!`, `receive`) and
software transactional memory (`(curry stm)`, `T_TVAR`). A "cell" maps onto
one actor running its own `gillespie-step!` loop; a shared `<environment>`
(when cells compete for a common nutrient pool) is protected by STM, so a
cell's nutrient-uptake reaction does an atomic transaction against the
shared pool instead of racing other cells. Intercellular signaling (quorum
sensing, diffusible ligands) is just `send!` between cell actors. No new
concurrency primitive needed — this is exactly what the existing actor/STM
system is for.

## Visualizing it in Qt

`(curry qt6)`'s canvas + timer already covers what's needed: each simulation
tick runs a batch of `gillespie-step!` calls per cell, then redraws — cells
as circles colored/sized from their own species counts (protein-A → red
channel, protein-B → green channel, so gene-expression noise is visible as
flickering color), the shared nutrient pool as a background heatmap, and
temperature/pH as live slider widgets mutating the `<environment>` record in
real time.

---

## SBML interoperability

[SBML](https://sbml.org) (Systems Biology Markup Language) is the standard
XML exchange format for exactly this kind of model — species, compartments,
reactions with stoichiometry, and kinetic laws — used by essentially every
systems-biology tool (COPASI, VCell, libSBML-based pipelines) and by
[BioModels](https://www.ebi.ac.uk/biomodels/), a public repository of
several thousand curated, published, peer-reviewed model definitions
(glycolysis, the cell cycle, MAPK signaling cascades, circadian clocks, and
so on).

The reason this is worth a paragraph rather than dismissing it as "too big":
**curry already has the two pieces an SBML importer would actually need.**

1. **XML parsing** — `(curry xml)` already exists (`xml-parse`, `xml-find`,
   `xml-find-all`, `xml-attr`), used today by the atom/rss/s3/naips modules.
   SBML's core structure (`<model>` → `<listOfSpecies>`/`<listOfReactions>`,
   each `<species>`/`<reaction>` carrying attributes and nested
   `<listOfReactants>`/`<listOfProducts>` elements) is a plain nested-element
   tree with attributes — exactly the shape `xml-find-all`/`xml-attr` are
   already built to walk. No new XML infrastructure needed.

2. **Symbolic math** — SBML kinetic laws embed the actual rate expression as
   **MathML**, a nested-element XML encoding of an expression tree (`<apply>
   <times/> <ci>k</ci> <ci>S</ci> </apply>` for `k*S`, etc.). Curry's own
   `(curry symbolic)` CAS already has a full expression-tree representation
   (`sym-var`, `symbolic`, `simplify`, and general expression construction)
   plus the evaluator to turn a symbolic expression into a numeric result
   given variable bindings. A MathML→symbolic-expression translator is a
   structural walk (MathML's operator tags map close to 1:1 onto CAS node
   constructors) — small, and it means an imported kinetic law becomes a
   genuine curry symbolic expression, not an opaque string, so it can be
   *differentiated, simplified, or inspected* the same as anything else built
   with `∂`/`simplify`, not just evaluated as a black box.

Sketch of the resulting pipeline:

```scheme
(import (curry xml) (curry symbolic) (curry gillespie))

(define (sbml->cell path environment)
  (let* ((doc     (xml-load-file path))
         (model   (xml-find doc "model"))
         (species (sbml-parse-species model))       ; -> hash-table
         (reactions (map sbml-reaction->reaction
                         (xml-find-all model "reaction"))))
    (make-cell species reactions environment 0)))

;; each <reaction>'s <kineticLaw><math>...</math></kineticLaw> becomes a
;; real symbolic expression, then a propensity closure over it
(define (sbml-reaction->reaction el)
  (make-reaction (xml-attr el "id")
                 (sbml-parse-species-refs (xml-find el "listOfReactants"))
                 (sbml-parse-species-refs (xml-find el "listOfProducts"))
                 (mathml->propensity (xml-find (xml-find el "kineticLaw") "math"))))
```

This would let a curry script load a real, published cell-cycle or
glycolysis model straight from BioModels and run it under the stochastic
Gillespie engine with zero manual re-entry of the reaction network — turning
`(curry gillespie)` from "a toy you hand-build small networks for" into
something that can also reproduce (and then perturb, re-parameterize, or
feed into the evolution model) genuine published systems-biology results.

Scope note: a full SBML importer (all of SBML core, plus the optional
`comp`/`fbc`/etc. packages) is a real undertaking. A *useful* first cut —
species, reactions, stoichiometry, and mass-action/Michaelis-Menten kinetic
laws specifically (the common case in the simpler BioModels entries) — is a
much smaller, bounded piece of work, comparable in size to the base
Gillespie module itself. Full MathML coverage (every operator, piecewise
functions, etc.) is where the scope could balloon, so worth explicitly
capping to a useful operator subset rather than aiming for completeness.

## See also

- `docs/thoughts/toy-evolution-model.md` — the population-genetics model
  that composes with this one (genome = a cell's own rate-constant vector)
- `docs/reference/module-random.md` — the distribution/sampling primitives
  the Gillespie waiting-time draw and mutation-operator allele generators
  both build on
