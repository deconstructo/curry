# Tetrabrot Explorer

**Companion document for `examples/tetrabrot.scm`**

```
./build/curry examples/tetrabrot.scm
```

---

## What is the Tetrabrot?

The ordinary Mandelbrot set lives in ℂ — one complex number `c`, iterated as
`z → z² + c`.  The Tetrabrot extends this to **bicomplex numbers**, a
four-dimensional algebra written ℂ(i₁, i₂).

A bicomplex number has the form:

```
w = (a + b·i₁) + (c + d·i₁)·i₂
```

where i₁² = i₂² = −1 and, crucially, **i₁ and i₂ commute**: i₁i₂ = i₂i₁.
Commutativity makes bicomplex numbers very different from quaternions — there
are no cross-product effects, but there are **zero divisors** (non-zero numbers
whose product is zero).

Squaring a bicomplex number gives:

```
w² = (a²−b²−c²+d²)  +  2(ab−cd)·i₁  +  2(ac−bd)·i₂  +  2(ad+bc)·i₁i₂
```

The Tetrabrot is the set of `c ∈ ℝ⁴` for which the sequence `0, c, c²+c, …`
does not escape to infinity.  Because the parameter space is four-dimensional,
we explore it as **2D cross-sections** — the explorer lets you choose any axis
pair and sweep the remaining two coordinates with sliders.

---

## Controls

| Action | What it does |
|--------|-------------|
| Left-drag | Pan in the displayed plane |
| Scroll | Zoom toward cursor |
| Double-click | Zoom 2× toward cursor |
| `=` / `+` | Zoom in 1.5× |
| `-` | Zoom out 1.5× |
| `R` | Reset to default view |
| `Q` / Escape | Quit |

---

## The six cross-sections

The four ℝ⁴ components are:

| Name | Meaning |
|------|---------|
| x₀  Re(z₁) | Real part of the first complex coordinate |
| x₁  Im(z₁) | Imaginary part of the first complex coordinate |
| x₂  Re(z₂) | Real part of the second complex coordinate |
| x₃  Im(z₂) | Imaginary part of the second complex coordinate |

Choosing any two of these four axes defines a 2D plane through ℝ⁴.  There are
C(4,2) = 6 such planes:

| Axis pair | What you see |
|-----------|-------------|
| **x₀ × x₁** (standard ℂ) | The ordinary Mandelbrot set — bicomplex reduces to ℂ when z₂ = 0 |
| **x₀ × x₂** (Re diagonal) | Symmetric four-bulb shape; the set is connected but not simply connected |
| **x₀ × x₃** (anti-diagonal) | Rotated variant of the Re diagonal |
| **x₁ × x₂** (Im-Re cross) | A diamond (45°-rotated square) with fractal boundary |
| **x₁ × x₃** (Im diagonal) | Another four-fold symmetric shape |
| **x₂ × x₃** (z₂ plane) | Identical to the standard Mandelbrot by symmetry |

### Why does x₁×x₂ look like a square?

The set in the (x₁, x₂) plane has an exact description.  With x₀ = x₃ = 0,
the bicomplex parameter `c = 0 + x₁·i₁ + x₂·i₂ + 0` and the iteration
simplifies so that the escape condition becomes `|x₁| + |x₂| ≤ 2`.  The
boundary `|x₁| + |x₂| = 2` is the unit ball of the L¹ norm — a square rotated
45°.  The fractal structure appears along the edges when you zoom in.

---

## Sweeping the fixed axes

The two sliders in the **Navigate** tab control the values of the two
coordinates *not* currently displayed.  Sweeping them moves the 2D slice
through the four-dimensional solid.

Some things to try:

- With **x₀ × x₂** displayed, slowly move the x₁ slider away from 0.  Watch
  the connected set break into four disconnected islands and then dissolve
  entirely.

- With **x₁ × x₂** displayed, move the x₀ slider: the square deforms and
  develops the Mandelbrot cardioid as the first complex coordinate re-enters
  the picture.

- With **x₀ × x₁** displayed, a non-zero x₂ or x₃ slider "inflates" the set,
  rounding the cusp and hiding fine filaments behind fatter blobs.

---

## Bookmarks

The explorer ships with one bookmark per axis pair at the origin, so you can
quickly compare all six cross-sections at default zoom.  Save your own views
with the **Save bookmark** button in the Navigate tab; bookmarks persist across
sessions in `~/.tetrabrot_bookmarks.scm`.

---

## Relationship to the quaternionic Mandelbrot

The Mandelbrot explorer (`examples/mandelbrot.scm`) also supports a
quaternionic mode.  The key differences:

| | Quaternion (ℍ) | Bicomplex (ℂ²) |
|--|--------------|--------------|
| Algebra | Non-commutative | Commutative |
| Zero divisors | None | Yes |
| Parameter space | ℝ⁴ | ℝ⁴ |
| Shape | Roughly spherical | Four-fold symmetric, tetrahedral cross-sections |
| GPU cost | Same | Same |

The bicomplex set has sharper corners and stronger four-fold symmetry precisely
*because* of commutativity — there is no quaternionic skewing.

---

## Implementation notes

The GPU shader computes bicomplex squaring as a pure `vec4` operation:

```glsl
vec4 bc_sq(vec4 w) {
    float a=w.x, b=w.y, c=w.z, d=w.w;
    return vec4(a*a-b*b-c*c+d*d,
                2.*(a*b-c*d),
                2.*(a*c-b*d),
                2.*(a*d+b*c));
}
```

Dynamic axis selection is handled with `get4`/`set4` helpers (GLSL lacks
variable indexing on `vec4`):

```glsl
vec4 c = set4(set4(u_c4, u_axis_h, get4(u_c4, u_axis_h) + wx),
                         u_axis_v, get4(u_c4, u_axis_v) + wy);
```

This injects the screen-pixel offsets into the two displayed axes while holding
the fixed axes at their slider values — allowing any 2D cross-section to be
rendered by the same shader with just two uniform integers.

The escape criterion is the Euclidean norm `dot(z,z) > 4`, which is not the
bicomplex modulus (which involves the idempotent decomposition) but gives
correct and aesthetically pleasing results.

---

## What's next — 3D

The `x₁×x₂` diamond is the 2D cross-section of a 3D solid (the full
`x₃ = 0` slice of ℝ⁴).  That solid — the "Tetrabrot" in the strict sense — can
be rendered as a 3D volume by raymarching with a distance estimator.  See the
discussion at the bottom of this file for the planned 3D viewer that builds on
this explorer.
