# Simulating cell biochemistry with the Gillespie algorithm

This guide walks through `(curry gillespie)` by building up a real toy
example — a gene that flips on and off — from nothing, then makes it
sensitive to temperature, then runs several of them at once as competing
curry actors.

For the field-level reference — every procedure, every combinator — see
[`docs/reference/module-gillespie.md`](../reference/module-gillespie.md).
This guide is the narrative version.

---

## Why not just solve an ODE?

If you've done any chemical-kinetics modeling before, your instinct is
probably a system of ODEs: `d[A]/dt = k₁ − k₂[A]`. That's the right tool
when molecule counts are large enough that concentration is meaningfully
continuous. Inside a real cell, a gene often has a handful of mRNA copies,
sometimes zero, sometimes three — "concentration" isn't a continuous
quantity there, it's a small integer that jumps around randomly. The
Gillespie algorithm simulates those individual random jumps directly,
rather than averaging them into a smooth curve that hides the noise —
noise that's often the actual biologically interesting part (this is
literally why real gene expression looks "bursty" under a microscope).

## Part 1: A birth-death process

The simplest possible example: molecules of `A` are produced at a constant
rate, and each existing molecule of `A` independently degrades at some
rate. This is the "hello world" of stochastic simulation, and it has a
known analytical answer to check your simulation against: at steady state,
the expected count is `(production rate) / (degradation rate per molecule)`.

```scheme
(import (curry gillespie))

(define birth (make-reaction "birth" '() (list (cons 'A 1))
                              (mass-action 5.0 '())))
(define death (make-reaction "death" (list (cons 'A 1)) '()
                              (mass-action 0.1 (list (cons 'A 1)))))

(define env  (make-environment 310.0 7.0 0))
(define cell (make-cell (make-species (list (cons 'A 0))) (list birth death) env 0))

(gillespie-run! cell 200.0)
(species-count (cell-species cell) 'A)
; => somewhere around 50 (= 5.0 / 0.1), different every run
```

Run this a few times — you'll get a different number each time, scattered
around 50. That's the point: this is a genuine random process, not a
deterministic calculation with noise sprinkled on top.

`make-reaction` takes reactants and products as `(species . stoichiometry)`
alists — `'()` for "nothing consumed"/"nothing produced" (a zero-order
birth reaction, or a pure degradation with no product). `mass-action`
builds the standard rate law: `k` times the reactant count (or the
combinatorial term `C(n, stoichiometry)` for a reaction that needs more
than one molecule of the same species).

## Part 2: A gene that flips on and off

A slightly more interesting toy: a gene switches between an "off" state
and an "on" state, and only produces protein while on.

```scheme
(define switch-on  (make-reaction "switch-on"  (list (cons 'gene-off 1)) (list (cons 'gene-on 1))
                                   (mass-action 0.1 (list (cons 'gene-off 1)))))
(define switch-off (make-reaction "switch-off" (list (cons 'gene-on 1))  (list (cons 'gene-off 1))
                                   (mass-action 0.1 (list (cons 'gene-on 1)))))
(define transcribe  (make-reaction "transcribe" (list (cons 'gene-on 1)) (list (cons 'gene-on 1) (cons 'protein 1))
                                    (mass-action 2.0 (list (cons 'gene-on 1)))))
(define degrade      (make-reaction "degrade" (list (cons 'protein 1)) '()
                                     (mass-action 0.05 (list (cons 'protein 1)))))

(define species (make-species (list (cons 'gene-off 1) (cons 'gene-on 0) (cons 'protein 0))))
(define cell (make-cell species (list switch-on switch-off transcribe degrade)
                         (make-environment 310.0 7.0 0) 0))

(define trajectory (cell-trajectory cell 500.0 5.0))
```

`transcribe` is written so the gene isn't consumed by producing protein —
`gene-on` appears in both its reactants and its products (stoichiometry 1
on each side), which is exactly how you represent "this reaction needs `X`
to be present as a catalyst/enabling condition, but doesn't use it up."

`cell-trajectory` runs the simulation and records a full snapshot of every
species every 5 time units, returning a list of
`(time . ((species . count) ...))` — feed that straight to a plotting
routine, or (see Part 4) a live Qt canvas.

## Part 3: Making it temperature-sensitive

Real transcription rates depend on temperature. Wrap the `transcribe`
reaction's rate law with `arrhenius`, and compose it with `mass-action`
via `rate*`:

```scheme
(define transcribe-hot
  (make-reaction "transcribe" (list (cons 'gene-on 1)) (list (cons 'gene-on 1) (cons 'protein 1))
                 (rate* (mass-action 2.0 (list (cons 'gene-on 1)))
                        (arrhenius 1.0 5000.0))))
```

Now the exact same reaction slows down as you cool the environment:

```scheme
(define env (make-environment 310.0 7.0 0))
((reaction-propensity transcribe-hot) species env)          ; warmer
(set-environment-temperature! env 250.0)
((reaction-propensity transcribe-hot) species env)          ; noticeably slower
```

This is the whole composability story: `rate*` just multiplies whatever
propensity procedures you give it together. Want it pH-sensitive too? Add
`(hill environment-ph 7.0 1.0)` to the `rate*` call. Want it to saturate
as a shared nutrient pool depletes? Add `(michaelis-menten vmax km
'glucose)`. None of these interact specially with each other — they're
just numbers being multiplied.

## Part 4: Several cells at once, as actors

`(curry gillespie)` has no separate "multi-cell" API, because curry
already has one: actors.

```scheme
(define (run-one-cell reporter)
  (let* ((species (make-species (list (cons 'gene-off 1) (cons 'gene-on 0) (cons 'protein 0))))
         (cell (make-cell species (list switch-on switch-off transcribe degrade)
                           (make-environment 310.0 7.0 0) 0)))
    (gillespie-run! cell 500.0)
    (send! reporter (species-count (cell-species cell) 'protein))))

(define reporter (self))
(for-each (lambda (_) (spawn run-one-cell reporter)) (iota 20))

(let loop ((i 0) (results '()))
  (if (= i 20)
      results
      (loop (+ i 1) (cons (receive) results))))
```

Twenty independent cells, each running its own Gillespie simulation on its
own OS thread, reporting back through an ordinary actor mailbox — no new
concurrency primitive, no locking code you had to write yourself.

If those cells need to **share** something — one common nutrient pool they
all compete for — don't mutate a shared `<environment>` directly from
multiple actors; that's an unprotected data race the same as it would be
in any language. Wrap the shared mutable state in curry's software
transactional memory (`(curry stm)`) instead, so a cell's uptake reaction
decrements the pool atomically:

```scheme
(import (curry stm))
;; sketch -- see docs/reference/concurrency.md for the full STM API
(define shared-nutrients (make-tvar 1000))
```

## Where to go from here

- The [reference doc](../reference/module-gillespie.md) covers every
  procedure and combinator in full, including the exact combinatorial
  formula `mass-action` uses for multi-molecule reactions.
- [`docs/thoughts/gillespie-cell-model.md`](../thoughts/gillespie-cell-model.md)
  sketches importing real published models from
  [BioModels](https://www.ebi.ac.uk/biomodels/) via SBML, and composing
  this module with a toy population-genetics model where a cell's genome
  *is* its own vector of reaction rate constants — evolution as literally
  "which rate-constant vectors survive."
