# Holo Explorer — Polynomial Fractal Guide

**Companion document for `examples/holo_explorer.scm`**

```
./build/curry examples/holo_explorer.scm
```

---

## What it does

The classical Mandelbrot set is built from one specific iteration rule:
`z → z² + c`.  Holo Explorer generalises this to **any polynomial over ℂ**,
letting you dial in arbitrary complex coefficients up to degree 8 and watch the
resulting fractal update in real time.

You can explore the parameter space (the Mandelbrot-analogue for your
polynomial), switch to the Julia set for any particular `c` value by clicking
a point, and colour orbits either by escape time or by how close they pass to a
geometric trap.

---

## The mathematics

### Polynomial iteration

Pick a polynomial with complex coefficients:

```
P(z) = a₀ zⁿ + a₁ zⁿ⁻¹ + … + aₙ₋₁ z + aₙ
```

Start at `z₀` and apply `P` repeatedly:

```
z₁ = P(z₀),  z₂ = P(z₁),  z₃ = P(z₂),  …
```

Two things can happen: the orbit `{zₖ}` either **escapes** — `|zₖ| → ∞` — or
stays bounded forever.  The boundary between those two outcomes, as you vary
the starting conditions, is where the interesting structure lives.

### Escape-time colouring

For any point where the orbit escapes, we know *how quickly* it escaped.
Instead of colouring just "escaped / didn't escape" as a binary, we assign a
smooth real value:

```
t = i + 1 − log₂(log₂|zᵢ|)
```

where `i` is the first iteration at which `|zᵢ| > 2` (the bailout radius).
The `log₂ log₂` correction removes the quantisation steps that would otherwise
appear at each integer iteration count.  The result is fed into a periodic
colour palette — each palette cycle covers about 67 iterations.

### Parameter space vs Julia sets

These are two different ways of using the same iteration:

**Parameter space (Mandelbrot mode):** fix `z₀ = 0`, let `c` vary across the
viewport.  Each pixel *is* a different polynomial (the constant term takes the
pixel's value).  The dark region — where the orbit never escapes — is the
**Mandelbrot-analogue** for your polynomial.

**Julia mode:** fix `c` to a single complex number, let `z₀` vary across the
viewport.  Every pixel is a different starting point for the *same* polynomial.
The resulting set is the **Julia set** for that particular `c`.

The two pictures are intimately linked: a `c` value **inside** the parameter
space set always gives a **connected** Julia set.  A `c` value **outside** the
set gives a **totally disconnected** Julia set (Cantor dust).  The boundary of
the parameter space set is exactly the set of `c` where the Julia set
transitions from connected to dust.

To pick a `c` for Julia mode, just click anywhere on the parameter space
picture.  The Julia set for that point appears immediately.  Click near the
boundary of the dark region for the most elaborate Julia sets; click deep
inside for boring filled blobs; click far outside for fractal dust.

### Why the constant term is called the c-slot

The last coefficient `aₙ` plays a special role.  In the standard Mandelbrot
`z² + c`, the constant `c` is what varies across the viewport.  Holo Explorer
preserves this: by default the last coefficient is **replaced** by the current
`c` (either the pixel position in parameter space mode, or the fixed Julia
parameter).  You can switch this off with the **c-slot is fixed constant**
toggle, in which case the last coefficient stays at whatever you type.

### Horner's method

Evaluating `a₀ zⁿ + a₁ zⁿ⁻¹ + … + aₙ` naively requires `n` multiplications
and `n` additions just to compute the powers.  Horner's method rewrites the
same polynomial as:

```
(…((a₀ z + a₁) z + a₂) z + … + aₙ₋₁) z + aₙ
```

which needs only `n` complex multiplications total — a significant saving at
degree 7 or 8 — and is more numerically stable because intermediate values
never grow as large.  The GPU shader evaluates every pixel using this scheme.

### Orbit traps

Instead of asking *when* does the orbit escape, orbit trapping asks: *how close
does the orbit ever pass to a specific shape?*

For each iteration step, compute the distance from the current `z` to your
chosen trap primitive:

- **Point trap:** `dist(z) = |z − p|`
- **Circle trap:** `dist(z) = | |z − centre| − r |`  — minimum distance to the circle's perimeter
- **Line trap:** `dist(z) = |Im((z − p̄) · exp(−iθ))|` — perpendicular distance to an infinite line

Track the running minimum `m = min(dist(z₀), dist(z₁), …)` over all
iterations up to the limit.  Colour every pixel by `m`, regardless of whether
the orbit escaped.  The trap shape burns itself into the fractal — circles
appear as nested rings, lines as ruled bands, and points as flower-like
medallions wherever orbits cluster.

---

## Controls

| Action | Effect |
|--------|--------|
| Left-drag | Pan |
| Scroll | Zoom toward cursor |
| Double-click | Zoom 2× toward cursor |
| Click (param space) | Set Julia `c` to that point |
| `=` / `+` | Zoom in 1.5× |
| `-` | Zoom out 1.5× |
| `R` | Reset view |
| `Q` / Escape | Quit |

---

## Polynomial symmetry

The degree of the polynomial determines the **rotational symmetry** of its
parameter space set.  For the pure monomial `zⁿ + c`:

| Polynomial | Symmetry order | Why |
|-----------|---------------|-----|
| z² + c | 1 (left-right mirror only) | the Mandelbrot set |
| z³ + c | 2-fold (180° rotation) | two critical points, both at ±i |
| z⁴ + c | 3-fold (120° rotation) | three critical points |
| z⁵ + c | 4-fold (90° rotation) | four critical points |
| zⁿ + c | (n−1)-fold | n−1 critical points at the (n−1)-th roots of 0 |

Adding lower-degree terms (non-zero `a₁`, `a₂`, …) breaks this symmetry but
can create richer dynamics.

---

## A catalogue of things to look for

### The classical Mandelbrot — z² + c

The default.  The **main cardioid** (the large heart-shaped dark region) is the
set of `c` values for which the iteration has a stable fixed point.  The large
disc to its left is the **period-2 bulb**, where the orbit cycles between two
points.  Every bulb on the boundary encloses a different orbit period, and the
sequence of bulb sizes follows the **Farey sequence**.

The **Feigenbaum point** at `c ≈ −1.4012` is where the real-axis sequence of
period doublings converges.  Zoom in there and you will find smaller copies of
the whole Mandelbrot set appearing at every scale — true self-similarity.

Try clicking these coordinates and then switching to Julia mode:

| c value | What you see |
|---------|-------------|
| `−0.1 + 0.651i` | Douady rabbit — three spiralling lobes |
| `−0.7269 + 0.1889i` | Sea-horse valley Julia set |
| `0.285 + 0.013i` | Elaborate spiral arms |
| `−0.8 + 0.156i` | Dendrite — a tree-like Julia set on the boundary of connectedness |
| `−1.476` | Airplane Julia set |
| `−0.1565 + 1.0322i` | San Marco dragon — cauliflower-like |
| `−2.0` | Degenerate: a line segment on the real axis |

### The cubic — z³ + c

Select the **z³ + c** preset.  The parameter space picture has **two-fold
symmetry** (180° rotational).  There are two main bulbs rather than one cardioid,
and two "antenna" filaments extending in opposite directions.

Julia sets for the cubic are richer than quadratic ones.  Near the two
main bulbs the Julia sets have three-pronged spirals.  Deep in the filaments
they fragment into Cantor-like dust with three pieces at each level rather than two.

Try Julia mode at:

| c value | What you see |
|---------|-------------|
| `0.0 + 0.5i` | Three-armed snowflake |
| `−1.0 + 0.0i` | Filled Julia set with pronounced three-fold branching |
| `0.5 + 0.5i` | Rapidly fragmenting dust with triangular self-similarity |
| `0.0 + 1.0i` | Fractal near the top antenna — delicate wisps |

### The quartic — z⁴ + c

Three-fold symmetry, making the parameter space look vaguely like a **triforce**.
The three main bulbs correspond to period-1 fixed points at the cube roots of
unity; the regions between them contain period-3 behaviour.

### Breaking the symmetry — z² − z + c

This polynomial has a different critical point (at `z = ½`) and a shifted
parameter space picture.  The main structure migrates away from the origin.
The Julia sets tend towards more elongated, asymmetric shapes.

### The cubic with a linear term — z³ − 3z + c

The polynomial `T₃(z)/2 = z³ − 3z` is (up to scaling) the Chebyshev polynomial
of degree 3.  Its Julia sets for real `c` are related to the **Douady-Hubbard
rabbit** structure from the quadratic case but with three lobes.  Near `c ≈ 2`
on the real axis the iteration degenerates to something almost arithmetic.

### Orbit traps — things to try

**Point trap at the origin, z² + c:**
Set the trap type to Point, leave X and Y at 0.  The origin is a fixed
point-like attractor for orbits that just barely don't escape — the trap
colouring reveals the *density* of orbits near the origin, burning a bright
spot at the origin itself surrounded by concentric intensity rings.

**Circle trap, radius 1, centred at origin:**
The unit circle `|z| = 1` bisects the complex plane's interesting dynamics
region.  Orbits cross it repeatedly; the colouring reveals a family of rings
whose spacing encodes the escape rate.

**Circle trap with small radius around a periodic point:**
In parameter space mode, zoom into a bulb (say the period-3 bulb on the upper
left of the Mandelbrot set).  Switch to Julia mode for a `c` value inside that
bulb.  The Julia set will have a clear period-3 orbit.  Set a small circle trap
(radius ~0.05) and move the trap centre to one of the three periodic points.
The result is a dramatic "eye" structure where the trap catches orbits cycling
through the periodic point.

**Line trap through the origin:**
A horizontal line (`angle = 0°`) through the origin divides the plane into upper
and lower half-planes.  For the Mandelbrot set this produces a pattern of
horizontal banding overlaid on the usual escape-time structure — each band
corresponds to orbits that cross the real axis a different number of times
before escaping.

**Line trap at 45°:**
Rotating the line to 45° catches the same orbits at a different phase, producing
a diagonal striping that interacts with the fractal's symmetry to give a
herringbone texture.

### Deep zooms

The standard escape-time colouring (no orbit trap) is best for deep zooms.
Notable deep-zoom targets in the default `z² + c` picture:

- **Seahorse valley:** centre `−0.7454 + 0.1130i`, zoom until you see
  interlocking spirals of seahorse-like forms.
- **The Dragon:** `0.001643721971153 + 0.822467633298876i` — a dense,
  multi-armed fractal structure.
- **Deep spiral:** `−0.10109636384 + 0.95628651080i` — tight clockwise spirals
  at high zoom.

Note that holo_explorer uses single-precision float arithmetic, so you will hit
pixelation artefacts around zoom ×10⁶.  For deep zooms beyond that, use
`examples/mandelbrot.scm` which has double-double and perturbation-theory modes.

---

## Recipes

### Recreating the Mandelbrot set from scratch

1. Set degree 2.
2. Coefficients: `a₀ Re=1, Im=0` / `a₁ Re=0, Im=0` / `a₂ Re=0, Im=0`.
3. Ensure **c-slot is fixed constant** is off.
4. Mode: Parameter space.

### Exploring a Julia set family

1. Start in parameter space mode for any polynomial.
2. Pan and zoom to a region that interests you.
3. Click a point near the boundary of the dark set — Julia mode activates
   automatically with `c` set to that point.
4. Use the **Back to parameter space** button in the Mode tab to pick a
   different `c`.

### Orbit trap portrait photography

1. Choose `z² + c`, Julia mode, `c = −0.7 + 0.27i` (a well-connected Julia set).
2. Colour tab: switch to Orbit trap, type Point.
3. Move the trap to X=0.5, Y=0.5 — the trap burns a bright point into the
   fractal wherever orbits pass close to (0.5 + 0.5i).
4. Now switch trap to Circle with radius ~0.3.  The single point becomes a
   ring that the fractal geometry wraps around.
5. Try the Line trap at various angles — each angle picks out a different
   family of parallel streamlines in the dynamics.

### Asymmetric polynomials

Add a small `a₁` term to `z³ + c`:

1. Preset **z³ + c**.
2. Set `a₁ Re = 0.3` (leave Im = 0).
3. The two-fold symmetry of the cubic immediately breaks.
4. Increase `a₁` gradually — watch the two main bulbs drift apart and
   deform into each other.

---

## Further reading

- **Douady & Hubbard (1984):** The original paper on the topology of the
  Mandelbrot set.  Available from the Cornell archive.
- **Milnor, *Dynamics in One Complex Variable* (3rd ed.):** The standard
  graduate text; covers polynomial dynamics thoroughly including the
  connectedness locus for arbitrary degrees.
- **Branner & Hubbard (1988–1992):** Two-part paper on the cubic connectedness
  locus — the parameter space picture for `z³ + c`.
- **Peitgen & Richter, *The Beauty of Fractals* (1986):** Still the best
  coffee-table introduction, with orbit trap examples in the later chapters.
