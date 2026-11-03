# Modules: `(curry glsl)` and `(curry spacetime)`

Write a formula once with curry's symbolic CAS, and run it on the GPU.

- `(curry glsl)` turns CAS expressions into GLSL source.
- `(curry spacetime)` defines spacetime metrics symbolically, derives their
  partial derivatives with the CAS, and emits a GLSL `metric_eval`
  function plus a geodesic integrator, ready for a
  [`gpu-*`](module-qt6-gpu.md) compute shader.

Both are pure Scheme. `(curry spacetime)` needs no GPU to build metrics or
generate code; running the generated code needs the `gpu-*` layer.

## `(curry glsl)`

```scheme
(import (curry glsl))

(define r (sym-var 'r))
(define M (sym-var 'M))

(sym->glsl (/ (* 2 M) r) '((r . "r") (M . "u.mass")))
;; => "((2.0 * u.mass) / r)"

(glsl-assignments
  (list (cons "a" (* r (sqrt (/ (* 2 M) r))))
        (cons "b" (+ 1 (sqrt (/ (* 2 M) r)))))
  '((r . "r") (M . "mass")))
;; => "float cse0 = (2.0 * mass);
;;     float cse1 = (cse0 / r);
;;     float cse2 = sqrt(cse1);
;;     a = (r * cse2);
;;     b = (1.0 + cse2);"

(glsl-float 1/4)   ; => "0.25"
```

- **`(sym->glsl expr var-map)`** — one expression to one GLSL expression.
- **`(glsl-assignments targets var-map [prefix])`** — `targets` is a list of
  `(lvalue-string . expr)`. Every sub-expression that occurs more than once
  across all targets is computed once into a `float` temporary
  (`prefix`, default `"cse"`, followed by a number).
- **`var-map`** maps each `sym-var` *name* (a symbol) to the GLSL text that
  holds its value. A variable missing from it raises an error, so generated
  code never refers to an undeclared name. Functions made with `sym-fn` can
  be mapped to GLSL function names the same way.

Translation: integer powers up to 8 become multiplications (GLSL's `pow`
is undefined for a negative base); `x^(1/2)` → `sqrt`, `x^(-1/2)` →
`inversesqrt`; `cot`/`sec`/`csc` → `1.0 / tan`/`cos`/`sin`. Other
supported operations: `+ - * /`, `sqrt sin cos tan exp log abs sinh cosh
tanh asin acos atan asinh acosh atanh sign`. CAS operations with no GLSL
meaning (integrals, limits, transforms, comparisons) and complex numbers
raise an error.

A `var-map` value that is an expression rather than a name (`"a + b"`)
is put in parentheses. In `glsl-assignments`, the base of an expanded
integer power is computed once into a temporary; plain `sym->glsl`
repeats it.

The emitter walks expressions as trees. An expression with heavily shared
sub-expressions (built by repeated squaring, for example) is expanded as
if every shared part were a separate copy, so its size can grow
exponentially.

GPU arithmetic is 32-bit float (Metal has no 64-bit floats at all).

## `(curry spacetime)`

```scheme
(import (curry spacetime))

(define m (kerr-metric))                ; mass M and spin a stay symbolic
(metric-parameters m)                   ; => (M a)   (sym-vars)
(metric->glsl m '((M . "u.mass") (a . "u.spin")))   ; => GLSL source
(geodesic-glsl)                         ; => GLSL source using metric_eval
```

### Coordinates and conventions

Coordinates are Cartesian `(t x y z)`, from `(spacetime-coordinates)`.
Signature (− + + +), geometric units G = c = 1.

The black-hole metrics use **ingoing Kerr–Schild** coordinates,
g = η + f l⊗l. Unlike Schwarzschild or Boyer–Lindquist coordinates, they
are regular at the event horizon (a camera or a light ray passes through
it) and have no singularity at the poles. The radial function r is the
Schwarzschild/Boyer–Lindquist r, so horizon radii and orbit radii are the
usual ones; Kerr's r is defined by x²+y²+z² = r² + a²(1 − z²/r²).

### Metrics

```scheme
(minkowski-metric)
(schwarzschild-metric [M])
(reissner-nordstrom-metric [M [Q]])
(kerr-metric [M [a]])                     ; spin about +z, J = aM
(kerr-newman-metric [M [a [Q]]])
(kerr-schild-metric f l params)           ; your own: f scalar, l a 4-list covector
(make-metric components params)           ; any 4x4 symmetric metric
```

Each parameter can be a number or left out; a left-out parameter stays a
symbolic `sym-var` and becomes a GLSL name you choose in `metric->glsl`.
`make-metric` checks symmetry numerically (the CAS does not always cancel
`a·b − b·a`) and raises on an asymmetric metric.

### Accessors

```scheme
(metric-components m)       ; 4-vector of 4-vectors of expressions
(metric-component m i j)
(metric-parameters m)       ; list of sym-vars
(metric-derivatives m)      ; (vector d0 d1 d2 d3), dk[i][j] = ∂g_ij/∂X^k, simplified
```

### Generated GLSL

`(metric->glsl m param-map)` returns

```glsl
void metric_eval(vec4 X, out mat4 g, out mat4 d0, out mat4 d1, out mat4 d2, out mat4 d3)
```

with `X = (t, x, y, z)`, `g` = g_ij and `dk` = ∂g_ij/∂X^k, shared terms
computed once. `param-map` must name every parameter.

`(geodesic-glsl)` returns functions that use `metric_eval`:

```glsl
void  geodesic_rhs(vec4 X, vec4 P, out vec4 dX, out vec4 dP);
void  geodesic_rk4(inout vec4 X, inout vec4 P, float h);
float geodesic_hamiltonian(vec4 X, vec4 P);   // ½ g^ab P_a P_b: 0 for light
vec4  raise_index(vec4 X, vec4 P);            // g^ab P_b
vec4  lower_index(vec4 X, vec4 U);            // g_ab U^b
```

The integrator uses the Hamiltonian form of the geodesic equation, with
position X^m and covariant momentum P_m:

  dX^m/dλ = g^mn P_n,  dP_m/dλ = ½ u^a u^b ∂_m g_ab, where u = g⁻¹P

so it needs only the metric, its first derivatives, and a numeric 4×4
inverse: no Christoffel symbols and no symbolic matrix inverse. For a
time-independent metric, P_t (minus the energy at infinity) is conserved
exactly by this form. With 32-bit floats, keep the step `h` proportional
to the distance from the singularity.

### How it is tested

`tests/spacetime_tests.scm` compares the CAS's Kerr derivatives with
central finite differences. `tests/spacetime_gpu_tests.scm` traces light
rays on the GPU with the generated code: for Schwarzschild, the total
angle a ray sweeps agrees with the exact integral to about 1e-7, and the
capture limit is 3√3 M; for Kerr with a = 0.9 M, the capture limits for
light moving with the spin (2.844 M) and against it (6.834 M) come out
right; energy and the Hamiltonian are conserved along every ray.

## Demo: falling into a Kerr black hole

[`examples/blackhole.scm`](../../examples/blackhole.scm) (and
[`examples/blackhole-anaglyph.scm`](../../examples/blackhole-anaglyph.scm),
the same in red/cyan 3D) traces one light ray per pixel through the Kerr
metric generated by this module, with a thin accretion disk and a starry
sky, while the camera falls freely along its own geodesic, through the
event horizon, until it reaches the inner horizon. Each disk point's light
is shifted by g = (p·u_observer)/(p·u_emitter) (gravitational, Doppler and
the camera's own motion together), so brightness scales as g⁴ and colour
temperature as g.

What it does not show: inside the event horizon, some directions look back
toward regions of spacetime that ingoing Kerr–Schild coordinates do not
cover; rays traced into them stall and are drawn black. The fall stops at
the inner (Cauchy) horizon r₋, beyond which the Kerr solution is not
considered physical (and these Cartesian coordinates are discontinuous
across the disk spanned by the ring singularity).

In the anaglyph view, distant stars show a small parallax. That is real:
the two eyes look through different parts of the black hole's
gravitational lens.
