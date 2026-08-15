# `solve` / variable isolation / elimination: scope and design

*Design doc, 2026-08-15 — not yet implemented. Written to be decided on, not acted on.*

Motivated by [issue #10](https://github.com/deconstructo/curry/issues/10) (`dharmatech`): interactive
variable isolation/elimination over sets of equations, demoed via a Sympy-based UI, with a
follow-on question about whether curry's CAS and Qt6 graphics could support the same style of
interaction. **This doc covers only the CAS primitives** (`solve`, `isolate`, `eliminate`) —
the interactive graphical UI is a separate, later feature explicitly deferred by the repo owner
("when objects and packages/libraries/modules are supported") and is out of scope here.

## 1. Where curry's CAS stands today

Facts checked against `src/symbolic.h`/`src/symbolic.c`/`src/sx_poly.c` and
`docs/reference/symbolic.md` on `main`.

| Capability | Current state |
|---|---|
| Expression tree | `T_SYMEXPR` (op + args) / `T_SYMVAR`, full numeric-tower-aware arithmetic, `simplify`/`expand`/`collect` |
| Differentiation/integration | Extensive rule tables (`sx_diff`/`sx_integrate`), see `symbolic.h`'s own doc comment |
| Polynomial ops | `sx_degree`/`sx_leading_coeff`/`sx_collect` (general symbolic layer, `symbolic.c`); `sx_poly_gcd`/`sx_poly_resultant`/`sx_poly_squarefree`/`sx_poly_factor` (univariate, `sx_poly.c`, subresultant PRS — naive, not FFT-accelerated, fine at curry's actual scale) |
| Rewrite rules | `define-rule`/`define-ruleset` — user-declared pattern → replacement rewriting, already used internally for `trigsimp` |
| Comparisons | `sx_lt`/`sx_le`/`sx_gt`/`sx_ge` return a decided boolean or an unevaluated symbolic node when undecidable (sign-flagged vars vs. numeric only — see "Symbolic inequalities" in `symbolic.md`) |
| Substitution | `sx_substitute(expr, var, val)` — already exists, is exactly the primitive `eliminate` needs |
| Linear algebra | `src/matrix.c` — dense matrices over the numeric tower (fixnum/bignum/rational/flonum/complex/quaternion/etc.). **No symbolic entries** — confirmed by grep, matrix.c never touches `T_SYMEXPR`/`T_SYMVAR`. |
| **Equations as first-class objects** | **Absent.** There is no `=` symbolic node (`sx_lt`/`sx_le`/`sx_gt`/`sx_ge` exist; `sx_eq` does not) and no equation type. `simplify`/`substitute`/etc. all operate on bare expressions, not on `lhs = rhs` pairs. |
| **Solving** | **Absent.** No `solve`, no `isolate`, no `eliminate` anywhere in the codebase. |

The gap is real and total — this would be new CAS surface, not an extension of something partial.

## 2. What "solve" actually needs to mean here (scoping the ambition down)

A general symbolic equation solver (Sympy's `solve`, Mathematica's `Solve`) handles arbitrary
nonlinear systems via Gröbner bases, resultant elimination, case-splitting on branch cuts, etc.
That is a multi-year CAS research project, not a feature to bolt onto curry. The issue itself
doesn't ask for that — `dharmatech`'s demo is specifically **interactive, single-step variable
isolation**: click an operation, it peels off one layer, repeat until the target variable stands
alone. That's a *much* smaller, well-defined problem: symbolic **inverse function application**
along a single "spine" where the target variable appears exactly once.

This doc proposes three tiers, in increasing order of ambition, each usable and shippable on
its own:

### Tier 1 — `isolate`: single-occurrence peeling (directly matches the demo)

```scheme
(isolate '(= (+ (* 2 x) 3) 7) 'x)   ; => (= x (/ (- 7 3) 2))   i.e. x = 2
```

Algorithm: given `(= lhs rhs)` and target variable `var`, confirm `var` occurs exactly once in
`lhs` (reject and raise otherwise — this is the tier's whole restriction, and it's the same
restriction the demoed UI has, since "peel one operation" only makes sense with one occurrence).
Then walk from the root of `lhs` down to `var`, and at each step apply the inverse of the
operator encountered to *both sides* of the equation, matching `sx_diff`'s existing pattern of
one dispatch arm per operator:

| Encountered on the `var`-side | Inverse applied to the other side |
|---|---|
| `(+ a b)`, `var` in `a` | subtract `b` |
| `(- a b)`, `var` in `a` | add `b`; `var` in `b` → negate, then subtract from `a` |
| `(* a b)`, `var` in `a` | divide by `b` (raise if `b` can't be shown nonzero — reuse the existing sign-assumption machinery `sym_is_positive`/`sym_is_negative`/`SYM_ASSUME_NONZERO`, don't silently divide by a possibly-zero expression) |
| `(/ a b)`, `var` in `a` | multiply by `b`; `var` in `b` → invert both sides, multiply |
| `(expt a n)`, `var` in `a`, `n` numeric | take the `n`-th root (odd `n`: unique real root; even `n`: **two roots**, see below) |
| `(sin a)` / `(cos a)` / etc. | apply inverse trig — **multi-valued**, see below |
| `(sqrt a)` | square both sides (introduces the extraneous-root problem in reverse — squaring can add solutions, not lose them; still needs care, see below) |

**Multi-valued inverses are the real design decision here, not the peeling mechanics.**
`x² = 4` has two solutions; `sin(x) = 0` has infinitely many. Options:

- **(a)** Return a single principal-value solution (matching `sqrt`/`asin`/etc.'s existing
  single-valued behavior elsewhere in the CAS — `sx_sqrt`, `sx_asin` already only ever return one
  value). Simplest, consistent with existing conventions, but silently drops solutions — a user
  solving `x² = 4` for a real physical quantity expecting `x = ±2` gets only `x = 2`.
- **(b)** Return a list of solutions where the inverse is genuinely finite-multivalued (even
  roots, `n`-th roots), and refuse (raise) on infinite-multivalued cases (general `sin`/`cos`)
  rather than trying to represent `x = arcsin(0) + 2πk`. This matches what a numeric/algebraic
  solver can honestly promise and stays finite.
- **(c)** Take an assumption argument (reusing the existing `SYM_ASSUME_POSITIVE` mechanism) so
  `(isolate eq x)` on a `positive`-flagged `x` deterministically picks the positive root.

**Recommendation: (b) as the default return shape (a list, even when singleton), with (c) as a
convenience filter on top when the variable already carries a sign assumption** — this reuses
`sym_is_positive`/`sym_is_negative` that already exist for exactly this purpose elsewhere in the
CAS (see "Symbolic inequalities"), rather than inventing a second assumption-passing convention.

### Tier 2 — `solve`: closed-form single-variable polynomial solving

Building on `sx_degree`/`sx_collect` (already exist): given `expr` (implicitly `expr = 0`) and a
target variable, if `expr` is polynomial in that variable of degree ≤ 2, use the linear/quadratic
formula directly (coefficients extracted via `sx_collect`, matching the pattern `sx_poly_gcd`
already uses for polynomial coefficient extraction). Degree ≥ 3 is explicitly **out of scope**
for closed form (cubic/quartic formulas are large, rarely what anyone actually wants symbolically,
and easy to add later as its own follow-up if requested) — `solve` on a higher-degree poly should
raise a clear "no closed-form solver for degree N" error rather than silently failing or timing
out attempting something unbounded.

This tier subsumes Tier 1 for the specific case of a single polynomial equation and produces the
same multi-valued-result question (quadratics have ≤2 roots) — same recommendation, return a list.

### Tier 3 — `eliminate`: variable elimination across a small system

This is the part of the issue title ("sets of equations... variable elimination") Tier 1/2 alone
don't cover. Given two equations and a variable to eliminate:

```scheme
(eliminate '((= x (+ y 1)) (= (* 2 x) (- 10 y))) 'x)
; => solve the first for x, substitute into the second:
;    (= (* 2 (+ y 1)) (- 10 y))
```

This is genuinely simple **given Tier 1/2 already exist**: `isolate`/`solve` the first equation
for the target variable, then `sx_substitute` (already exists, unchanged) the result into every
other equation in the list. No new core machinery beyond composing two things this doc already
proposes plus one thing that already ships. For a full linear system (N equations, N unknowns)
Gaussian elimination is the natural generalization, but doing that *symbolically* (not just over
`matrix.c`'s numeric-tower entries) means either (a) extending `matrix.c` to accept `T_SYMEXPR`
entries — a real, separate scope decision, since every existing `mat_*` operation would need to
route through `sx_add`/`sx_mul` instead of raw numeric-tower arithmetic — or (b) a dedicated small
symbolic Gaussian-elimination routine in the CAS layer that doesn't touch `matrix.c` at all.
**Recommendation: (b) for now** — narrower, doesn't risk `matrix.c`'s existing numeric-only
callers, and can be upgraded to reuse a symbolic-capable `matrix.c` later if that ever gets built
for its own reasons. Full N-variable elimination is explicitly a stretch goal, not part of the
initial scope; pairwise elimination (the example above) is what actually unblocks the issue's
"sets of equations" framing without committing to a general linear-system solver up front.

## 3. Equation representation — the one real prerequisite decision

Nothing above works without deciding how an equation is represented, since none exists today.
Two candidates:

### Candidate A — plain `(= lhs rhs)` as an ordinary `T_SYMEXPR` with a new `SX_EQ` operator

Mirrors exactly how `sx_lt`/`sx_le`/`sx_gt`/`sx_ge` already work (`SX_LT` etc. interned operator
symbols, `symbolic.h` line ~314). `simplify`/`substitute`/`sx_write` already have a dispatch
pattern (op → handler) that a new `SX_EQ` slots into with minimal new code — `substitute` on an
equation is just "substitute into both `lhs` and `rhs`," `sx_write` is "write lhs, write `=`,
write rhs." Cheapest to build, most consistent with existing code.

### Candidate B — a dedicated record type (`T_SYMEQ` or similar), separate from the general expression tree

More explicit ("this is not just another operator, it's a different *kind* of thing"), but adds a
new heap object type, a new set of predicates (`equation?`), and duplicate dispatch logic in every
place that currently switches on `T_SYMEXPR`'s op field — `simplify`, `sx_write`/`sx_write_infix`/
`sx_write_latex`/`sx_write_cuneiform`, `sx_substitute`, would all need an equation-aware branch
*before* falling into their existing expression-only logic, rather than getting it for free via
one new op case.

**Recommendation: Candidate A.** It's the smaller, more consistent-with-existing-conventions
choice, and the pattern is already proven working for `<`/`<=`/`>`/`>=`. Nothing about "sets of
equations" needs equations to be a fundamentally different *kind* of value — a plain Scheme list
of `(= lhs rhs)` expressions already gives Tier 3 everything it needs.

## 4. What this doc does *not* propose

- **No general nonlinear multi-equation solver** (Gröbner bases, resultant-based elimination for
  arbitrary systems). Tier 3's pairwise `eliminate` is a deliberately narrower, composable
  primitive, not a step toward this.
- **No interactive UI** (colored click-to-isolate, Jupyter-lite-style notebook). That's the
  second half of issue #10 and is explicitly deferred by the repo owner pending
  objects/packages/module work; this doc's `solve`/`isolate`/`eliminate` are exactly the CAS
  primitives such a UI would eventually call, built independently of when/whether the UI happens.
- **No numeric/iterative root-finding** (Newton's method, bisection) for equations with no
  closed form. A separate, much easier addition if ever wanted, but not part of "isolation" as
  the issue describes it.

## 5. Open questions (not decided here)

1. Multi-valued result shape — list-always (recommended above) vs. principal-value-only vs.
   assumption-gated single value. This is the one genuinely user-facing API design choice.
2. Where does this code live — a new `src/sx_solve.c` (mirroring `sx_poly.c`'s own file split) or
   folded into `symbolic.c` directly? Given `sx_poly.c` already established the precedent of
   splitting a CAS sub-area into its own file once it's non-trivial, `src/sx_solve.c` seems
   consistent, but this is a small enough call to make at implementation time rather than here.
3. Should Tier 2's degree-≤2 restriction extend to cubic (via Cardano's formula) as a fast
   follow-up, or stay capped at quadratic indefinitely? No signal from the issue either way.
4. Does `isolate`/`solve` get an Akkadian/cuneiform name alias like the rest of the CAS surface
   (`∂`, `∫`, etc.)? Consistent with existing convention if so; not decided here.
