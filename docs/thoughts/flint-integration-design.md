# FLINT integration: scope and design

*Design doc, 2026-08-15 — not yet implemented. Written to be decided on, not acted on.*

## 1. Where curry stands today

Facts checked against `main`, since the whole point of this doc is not proposing FLINT for things curry already does well.

| Capability | Current state |
|---|---|
| Exact integers/rationals | GMP-backed bignum/rational, core numeric tower (`src/numeric.c`) |
| Arbitrary-precision floats | **Already implemented**: `(curry mpfr)`-adjacent core support, `src/mpfr_num.c`/`.h` (597+120 lines) — construction, full arithmetic, transcendentals (trig/hyperbolic/exp/log), special functions (Γ, ζ, erf, Bessel J₀/J₁), named constants (π, e, φ, γ, Catalan, Apéry) at arbitrary precision, a thread-local precision/rounding context (`with-precision`), and directed-rounding interval arithmetic. Optional (`-DBUILD_MPFR=ON`, off by default), fully wired into `numeric.c`'s dispatch (arithmetic, comparison, predicates all check `vis_mpfr` first). Documented in full at `docs/reference/numeric-precision.md`.
| Number theory | **Already implemented, always-on** (GMP only, no MPFR needed): primality (Miller–Rabin), factoring (trial division + Pollard ρ), totient/Carmichael/Möbius/divisor functions, modular arithmetic (mod-expt/mod-inverse/Jacobi/Kronecker/Legendre/extended-gcd/CRT), sequences (Fibonacci via fast doubling, Lucas, binomial, Bell, Stirling, partition count via Euler's pentagonal recurrence), continued fractions, squarefree/perfect-power/smooth predicates. All exact, all in `docs/reference/numeric-precision.md`.
| Polynomials | curry's CAS (`src/sx_poly.c`) already has univariate polynomial GCD (subresultant PRS) and factorization (`sx_poly_gcd`/`sx_poly_factor`), plus `expand`/`collect`/`degree` in the general symbolic layer (`src/symbolic.h`). **Naive/textbook algorithms, not FFT- or modular-arithmetic-accelerated** — fine for the CAS's actual use case (small-to-medium symbolic expressions a human wrote), not competitive with FLINT's algorithms at scale (degree in the thousands, or coefficients with thousands of digits).
| Matrices | `src/matrix.c`/`.h`: dense matrix type over curry's numeric tower (any of fixnum/bignum/rational/flonum/complex/quaternion/etc as entries), `mat_mul` and friends. General-purpose, tower-typed, not specialized for exact linear algebra over Z/Q/finite fields the way FLINT's matrix module is.
| Multivariate polynomials, finite fields, p-adics, algebraic number fields, certified ball arithmetic, LLL lattice reduction | **Nothing** — genuinely absent from curry today, in any form. |

The upshot: FLINT is not filling an empty slot. It would sit *alongside* an already-substantial numeric/CAS stack, and its real value is narrower than "number theory library curry doesn't have" — it's specifically: **speed at scale** for things curry does the simple way already (bignum arithmetic, polynomial GCD/factorization), plus a **small number of genuinely new capabilities** (multivariate polynomials, finite fields, p-adics, algebraic number fields, certified/rigorous ball arithmetic, LLL).

## 2. What FLINT actually is (checked against flintlib.org, current release)

FLINT 3.x is not the narrow "fast integer arithmetic" library its name suggests anymore. As of FLINT 3.0, it absorbed three previously-standalone projects:

- **Arb** → certified ball arithmetic: every real/complex value carries a rigorous error bound that propagates through computation, so results come with a mathematically guaranteed accuracy, not just "however MPFR happened to round." This is a genuinely different guarantee than curry's existing MPFR-backed interval type (directed-rounding endpoints, no automatic error propagation through arbitrary function composition).
- **Antic** → algebraic number fields (exact arithmetic on roots of polynomials — √2, cube roots of 5, etc., as first-class exact values, not floating-point approximations).
- **Calcium** → exact computation mixing algebraic and transcendental numbers (a `π + √2` kind of value, kept exact through a computation, not immediately forced to a float).

Plus FLINT's own long-standing core: fast arbitrary-precision integer/rational arithmetic, univariate and multivariate polynomials over Z/Q/finite fields (with FFT/Karatsuba-accelerated multiplication and modular-arithmetic-accelerated GCD/factorization), power series, matrices over Z/Q/finite fields with fast linear algebra, primality testing and integer factorization (faster algorithms than curry's own Pollard ρ at large sizes), LLL lattice reduction, and finite field embeddings. ~8,000 documented functions.

**License**: LGPL-3.0-or-later (compatible with curry's own GPL-3.0 — worth double-checking against curry's actual license file, but LGPL linked into a GPL project is the normal, permitted direction).

**Packaging**: `brew install flint` (macOS, confirmed via Homebrew formula — depends on `gmp` and `mpfr`, both already curry dependencies, `gmp` unconditionally so). `libflint-dev` on Debian/Ubuntu (standard naming convention, not independently re-verified here — confirm at implementation time).

**Dependency implication**: FLINT depends on MPFR internally. `-DBUILD_FLINT=ON` would need `libmpfr` present *regardless* of whether curry's own `-DBUILD_MPFR=ON` is separately set — these are two independent CMake options today (curry's MPFR module is optional/off-by-default) but FLINT can't function without MPFR underneath it either way. **Open question, not decided here**: should `BUILD_FLINT` imply/force `BUILD_MPFR=ON` (since the runtime dependency exists either way and it'd be strange to have FLINT without curry's own MPFR-backed arbitrary-precision float type also available), or should they stay fully independent with a clear configure-time error if FLINT is requested without MPFR?

**API style**: "developer-friendly GMP-like C API" — manual memory management (init/clear pairs per object, matching how curry already wraps GMP's `mpz_t`/`mpq_t` and MPFR's `mpfr_t`), not a single monolithic object — consistent with how curry's existing GMP/MPFR wrapping already works, so the *style* of integration work is well-precedented in this codebase even though the *scope* (8,000 functions) obviously isn't something to wrap exhaustively.

## 3. Candidate architectures

### Candidate A — Isolated optional module: `(curry flint)` (recommended starting point)

FLINT types (polynomials, matrices, finite field elements, p-adics, algebraic numbers, ball-arithmetic reals) live as their own new heap types, exposed only through an explicit `(import (curry flint))`, with their own predicates/constructors/operations — `flint-poly?`, `make-flint-poly`, `flint-poly-gcd`, etc. **Not** wired into curry's core numeric tower's automatic promotion rules (no `+`/`*`/`sqrt` dispatch to FLINT types transparently).

- **Pros**: Low risk. Doesn't touch `src/numeric.c`'s already-delicate promotion chain (fixnum → bignum → rational → flonum → complex → quaternion → octonion → multivector → surreal → symbolic) at all — no risk of destabilizing arithmetic that already works. Matches curry's own established module-boundary convention (optional C module, `BUILD_MODULE_X` flag, `modules/flint/flint.c`, gated behind explicit import) rather than a core-tower change. Ships incrementally — a first pass could expose just fast polynomial GCD/factorization and matrices, the two areas with the most obvious "curry already does this, FLINT does it faster at scale" payoff, without committing to wrapping all 8,000 functions or deciding where certified ball arithmetic sits in the tower.
- **Cons**: FLINT polynomials/matrices become a *second*, disconnected type from curry's existing symbolic-CAS polynomials and `src/matrix.c` matrices — no automatic conversion, a user has to explicitly choose which system to use and convert between them by hand. Doesn't give curry's core arithmetic (`+`, `*`, `sqrt` on ordinary numbers) any FLINT speedup even where FLINT's underlying bignum routines are faster than GMP's for pure integer/rational work (FLINT builds on GMP rather than replacing it, so this is a narrower gap than it might sound, but still real for e.g. very-high-degree polynomial evaluation).

### Candidate B — Deep tower integration

New `val_t` types for FLINT polynomials/matrices/finite-field elements/algebraic numbers, wired into `src/numeric.c`'s promotion chain the way MPFR was (every arithmetic primitive checks for the new type and dispatches). Ball arithmetic could replace or extend curry's existing MPFR-interval type outright.

- **Pros**: Transparent — existing code using `+`/`*`/`sqrt`/etc. on the right operand types would automatically get FLINT's speed/rigor for free, no new API surface to learn for basic use.
- **Cons**: Large surface area, real risk to a numeric tower that's already ten types deep and already documented as delicate (see `docs/reference/numeric-precision.md`'s own careful precision-context threading). Ambiguous benefit for the *matrix* and *polynomial* cases specifically, since curry already has first-class, tower-integrated matrix and symbolic-polynomial types with their own established semantics — a FLINT matrix silently promoting into/out of `src/matrix.c`'s matrix type, or a FLINT polynomial silently interacting with `sx_poly.c`'s symbolic polynomials, is a real design problem (which one wins? do they need a conversion/coercion layer regardless, defeating some of the "transparent" benefit?) that Candidate A sidesteps entirely by keeping them separate on purpose.

### Candidate C — Staged: A first, promote selectively later

Start with Candidate A (isolated module). If and when a specific FLINT capability proves clearly better *as the default* for something curry's tower already handles (the strongest case is probably ball arithmetic replacing/extending the MPFR-interval type, since that's a genuine correctness upgrade — rigorous error bounds instead of directed rounding — with no competing "curry already has a different first-class version of this" problem the way polynomials/matrices do), promote *that one thing* into the tower as a deliberate, separately-designed follow-up. Everything else (multivariate polynomials, finite fields, p-adics, algebraic numbers) has no existing curry equivalent to conflict with, so it naturally stays in the isolated module as its own vocabulary rather than needing tower promotion at all.

**This is the recommended path** — it's Candidate A with an explicit acknowledgment that ball arithmetic is the one piece worth reconsidering for tower integration later, once real usage shows whether it's worth the promotion-chain risk.

## 4. Scope for a first phase (if this moves forward)

Given the "why, not just what" from §1 — the clearest, least-overlapping, most load-bearing value FLINT adds — a first phase should probably prioritize, in roughly this order:

1. **Fast polynomial GCD/factorization** over Z/Q — directly comparable to and faster than curry's existing `sx_poly_gcd`/`sx_poly_factor` at scale, easy to demonstrate the win, low conceptual surface (a handful of functions).
2. **Multivariate polynomials** — genuinely new capability, no existing curry equivalent.
3. **Fast exact matrix operations over Z/Q** (determinant, rank, solve, LLL) — complements, doesn't compete with, `src/matrix.c`'s general tower-typed dense matrix (that one's for "matrix of any curry number," this would be for "fast *exact* linear algebra," a different use case).
4. **Finite fields** — genuinely new, needed as a building block for anything cryptography/coding-theory-flavored.
5. Certified ball arithmetic, p-adics, algebraic number fields — real capabilities, but lower priority for a first phase; ball arithmetic specifically is the strongest candidate for eventual tower promotion (§3, Candidate C) once the isolated module has proven itself.

Not proposing a concrete function-by-function API here — that's its own design pass once/if a phase-1 scope is actually approved, following the same pattern `docs/reference/writing-a-module.md` already establishes for any new `(curry X)` module.

## 5. Open questions for explicit decision (not silently picked)

1. **Module scope for phase 1** — the five-item list in §4 above, or a smaller/different subset?
2. **`BUILD_FLINT` and `BUILD_MPFR` CMake option relationship** — independent (with a hard configure-time error if FLINT is on and MPFR is off), or does enabling FLINT imply/force MPFR on too?
3. **Naming** — `(curry flint)` (matches the library's own name, discoverable) vs. something more description-first like `(curry fast-number-theory)` or `(curry algebra)` (curry's existing module names lean toward what-it-does over what-it's-built-on, e.g. `(curry sicm)` is the one real exception, named after the textbook it implements)?
4. **License compatibility** — checked: curry's own `LICENSE` file is GPL-3.0-**or-later** (confirmed via its own text, not assumed). LGPL-3.0-or-later linked into a GPL-3.0-or-later project is standard and permitted — no open question here after all, resolved during this doc's own writing.
5. **Relationship to the existing `(curry ffi)` general C-FFI module** — should this be a dedicated hand-written C module (`modules/flint/flint.c`, matching every other optional module in this codebase) the normal way, or could/should a first cut instead be built via `(curry ffi)`'s `define-foreign` against FLINT's C API directly from Scheme, avoiding a new C module at all? The FFI route is lower engineering cost for a first exploratory pass but loses the tight, checked-type integration (predicates backed by real heap-type tags, GC-visible objects, `.scc`-cache-safe identity — the same concerns `record_type.c`'s own header comments document at length for `define-record-type`) that a real C module gives; likely worth prototyping via FFI first and only committing to a proper C module once the API shape is validated.
