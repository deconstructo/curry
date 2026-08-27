# `(curry evolution)` — a toy population-genetics model
## Composing with `(curry gillespie)` to get evolving digital cells

*Drafted 2026-08-27. Status: pre-implementation design sketch — parked for later,
written up so the idea (and especially the composition with Gillespie) doesn't
get lost.*

---

## The goal

A hyper-simplified genetic model with real inheritance, several distinct modes of
gene transfer, and mutation that visibly changes simple life forms — small enough
to build in an afternoon, not a research-grant-sized undertaking.

## Prior art worth knowing about, not worth porting

- **Avida** (Michigan State's Digital Evolution Lab) — self-replicating digital
  organisms with genuine mutation/selection dynamics. A full C++ research
  platform with its own virtual machine; not something to adapt into curry.
- **Tierra** (Tom Ray, 1990) — the historical precursor: self-replicating
  machine-code creatures competing for CPU time and memory. Conceptually
  simple, but the real implementation is a custom VM plus an instruction set
  designed to tolerate mutation without crashing.
- **Dawkins' Biomorphs / "Weasel" program** (*The Blind Watchmaker*, 1986) —
  the right ancestor to actually crib from. A genome is a short vector of
  numbers; a genotype→phenotype mapping turns it into something with a
  fitness value; selection picks who reproduces. This is the simplest
  *correct* toy model in the whole field.
- **Lenia** (Bert Chan) — beautiful continuous-cellular-automaton life, but
  it's a convolution-kernel PDE system, closer in spirit to the
  reaction-diffusion/Turing-pattern idea than to genetics.

Conclusion: build a small Dawkins-style toy, not a port of any of the above.

## Core data model

```scheme
;; A genome is just a vector of alleles -- numbers, symbols, whatever a given
;; experiment wants an allele to mean. No fixed interpretation baked in here;
;; genotype->phenotype mapping is always supplied by the caller.
(define-record-type <genome>
  (make-genome alleles)
  genome?
  (alleles genome-alleles))   ; a vector

(define-record-type <organism>
  (make-organism genome fitness-cache)
  organism?
  (genome        organism-genome)
  (fitness-cache organism-fitness-cache set-organism-fitness-cache!))
```

## Mutation operators — composable, like the Gillespie rate-law combinators

Each operator is `(genome) -> genome` (pure, returns a new genome rather than
mutating in place, so operators compose by simple procedure composition):

- `(point-mutate genome rate allele-generator)` — each allele independently
  has probability `rate` of being replaced by `(allele-generator)`
- `(insert-mutate genome rate allele-generator)` — probability `rate` per
  position of inserting a fresh allele
- `(delete-mutate genome rate)` — probability `rate` per position of deletion
  (genome shrinks — lets genome *length itself* be under selection, which is
  most of what makes indels interesting versus point mutation alone)
- `(duplicate-mutate genome rate)` — probability `rate` per position of
  duplicating a short run (the toy analogue of gene duplication, the actual
  mechanism believed to originate new gene families in real biology)

Composed the same way the Gillespie rate-law combinators are meant to be:

```scheme
(define (my-mutation-pipeline g)
  (delete-mutate (insert-mutate (point-mutate g 0.01 random-allele) 0.005 random-allele) 0.002))
```

## Modes of gene transfer — the part you specifically asked for

Three distinct mechanisms, each a separate procedure rather than a single
parameterized "reproduce" call, since biologically they really are different
things happening on different timescales:

1. **Vertical / asexual** — `(reproduce-asexual parent mutation-pipeline)`.
   One parent, copy the genome, run it through the mutation pipeline. This is
   what almost every existing "genetic algorithm" tutorial calls
   "reproduction" and stops there.

2. **Vertical / sexual (crossover)** — `(reproduce-sexual parent-a parent-b
   crossover-points mutation-pipeline)`. Splice both genomes at one or more
   crossover points (single-point, two-point, or uniform crossover — same
   three classic GA variants), then mutate the result. Requires the two
   genomes to be roughly comparable in structure to be meaningful, same as
   real meiosis requiring homologous chromosomes.

3. **Horizontal gene transfer** — `(transfer-genes! donor recipient segment)`.
   Splice a subsequence from one *unrelated, already-existing* organism's
   genome directly into another's, with no reproduction event and no
   generational relationship at all. This is the easiest of the three to
   implement (it's just "copy this slice from here to there") and the one
   that makes the model behave unlike a classic GA — a beneficial trait can
   jump sideways across the population in one step, the way antibiotic
   resistance genes actually spread across bacterial populations via
   plasmids, rather than only propagating down a lineage tree.

## Selection

Ordinary, well-trodden ground — roulette-wheel or tournament selection over a
user-supplied fitness function, `(fitness organism) -> nonneg-real`. Nothing
novel needed here; this is the one part of the model where reusing a standard
algorithm rather than inventing something is exactly right.

## Where this gets interesting: composing with `(curry gillespie)`

This is the part worth writing down carefully, since it's the actual reason
to build both modules rather than either alone.

A Gillespie `<cell>` (see the sibling design/implementation for the base
module) has a reaction network — a list of `<reaction>` records, each
carrying a *propensity function* closed over some rate constants. The
insight: **a genome can just be the vector of those rate constants.**

```scheme
;; genotype -> phenotype, in the most literal sense possible: each allele IS
;; a reaction's rate constant, in order.
(define (genome->reactions genome reaction-templates)
  (map (lambda (template rate-const)
         (make-reaction (reaction-template-name template)
                         (reaction-template-reactants template)
                         (reaction-template-products template)
                         (mass-action rate-const)))
       reaction-templates
       (vector->list (genome-alleles genome))))

;; A cell's fitness is defined by running its OWN Gillespie simulation and
;; reading off something that matters -- e.g. how much of some product
;; species it accumulates before nutrients run out.
(define (cell-fitness organism environment t-max)
  (let* ((reactions (genome->reactions (organism-genome organism) *reaction-templates*))
         (cell      (make-cell (initial-species) reactions environment 0)))
    (gillespie-run! cell t-max)
    (hash-table-ref (cell-species cell) 'product 0)))
```

Run this across a population, and "evolution" is no longer a separate toy
model bolted on next to the biochemistry — it *is* selection over which
reaction-rate-constant vectors survive and reproduce, with:

- **Mutation** perturbing individual rate constants (a point mutation
  changing one enzyme's `k` by a random multiplicative factor is a very
  natural allele-generator here — rates are positive reals, so a log-normal
  perturbation is the right shape, not a uniform one)
- **Sexual reproduction** mixing two cell lineages' rate-constant vectors —
  modeling, loosely, what happens when two strains' regulatory networks
  recombine
- **Horizontal gene transfer** splicing one cell's successful rate constant
  (say, a faster nutrient-uptake enzyme) directly into an unrelated cell's
  network mid-simulation — which is a genuinely reasonable toy model of
  plasmid-mediated antibiotic-resistance spread, not just a cute analogy

And because the shared `<environment>` (temperature, pH, nutrient pool) from
the Gillespie design already feeds into every cell's propensity functions,
you get environmental selection pressure for free: a population evolving
under one temperature/nutrient regime will drift toward different rate
constants than the same starting population evolving under another, without
writing a single extra line of "environment affects fitness" glue code — the
environment already *is* wired into fitness, since fitness is defined by
running the actual simulation.

## Rough sizing

Genome/mutation/selection: comparable in size to `(curry random)` (a few
hundred lines, all pure Scheme, record types + composable procedures, no new
C). The genome↔reaction-network bridge above is maybe 30–50 lines on top of
whatever `(curry gillespie)`'s own public API ends up being. Total: an
afternoon of focused work, same ballpark as the Gillespie module itself, not
a second research-grant-sized undertaking.

## Open questions to resolve before implementing

- Should `<organism>` carry a *lineage* (parent pointers) for later
  phylogenetic-tree visualization, or stay lineage-free to keep the base
  version minimal? (Lean toward lineage-free v1, add it later if the
  visualization work wants it.)
- Crossover on rate-constant genomes needs the two parents' reaction
  templates to line up positionally — fine for a fixed reaction network
  shared across the population, but breaks down if genome *length* itself
  varies (via indel mutations) once genomes start encoding variable-size
  reaction networks rather than fixed-size rate-constant vectors. Worth
  deciding whether v1 supports variable-topology reaction networks at all,
  or deliberately restricts evolution to rate-constant tuning over a fixed
  topology first, and treats topology evolution as a stretch goal.
- Population-level driver loop (spawn N cell-actors each running its own
  Gillespie simulation, collect fitness, apply selection, repeat) maps
  naturally onto curry's existing actor system the same way multi-cell
  Gillespie does — worth designing the two population loops (plain Gillespie
  multi-cell, and evolving multi-cell) so they share as much of that driver
  machinery as possible rather than duplicating it.

## See also

- [`gillespie-cell-model.md`](gillespie-cell-model.md) — the `(curry gillespie)` design/implementation this composes with
- `docs/thoughts/anarchists-cookbook.md` — this pairs naturally with a
  cookbook chapter once both modules exist: "grow a population of digital
  cells, watch a beneficial mutation sweep through it, then watch it jump
  sideways via horizontal transfer into an unrelated lineage"
