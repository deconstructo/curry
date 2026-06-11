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

## Other chapter seeds (unrelated to v1.4)

*(Drop ideas for other chapters here as they come up)*

- **Babylonian astronomy in Curry** — sexagesimal reader + the MUL.APIN
  tablet series; already have the cuneiform Unicode support
- **4D maze** — the raycaster series; maze4d roadmap is in thoughts/4d-maze.md
- **Physics on a Raspberry Pi** — GPIO + symbolic ODE solver; RPI.md has the plan
- **MCP servers as Scheme one-liners** — the mcp module makes this surprisingly terse
