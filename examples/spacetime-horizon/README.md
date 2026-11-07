# spacetime-horizon

What happens to time when you fall through a black hole's event horizon.

```
./build/curry examples/spacetime-horizon/main.scm
./build/curry examples/spacetime-horizon/main.scm --spin 0.9
```

Options: `--spin A`, `--L L`, `--tau T` (start paused at proper time T),
`--slices 1|3|5`, `--yaw`, `--pitch`, `--dist`, `--snapshot FILE.png`
(save one frame and exit).

Needs the qt6 module with the `gpu-*` layer (Qt 6.7 or later).

## What you see

Three panes: controls on the left, the picture in the middle, the
equations on the right. The equations are evaluated at the observer and
change as it falls.

The picture is the equatorial plane of the black hole as a 2+1 dimensional
spacetime. The horizontal plane is space (x and y). **Up is time.** The
cones are light cones: every place where light, or anything slower than
light, can go from that point.

- Far away the cones stand upright.
- Closer in, they lean toward the hole.
- At the event horizon the outer edge of the cone is vertical. Light aimed
  outward stays where it is. (With spin this is true at the static limit,
  r = 2, for light aimed straight outward; light with a sideways part can
  still escape between r = 2 and the event horizon.)
- Inside, every direction in the cone points inward. Staying at the same
  radius is as impossible as staying at the same time. The singularity at
  r = 0 is not a place you reach; it is where all the cones lead.
- With spin (`a` greater than 0) there are two horizons and a static limit
  outside the event horizon. The cones also lean sideways (frame dragging)
  before the horizon, so nothing can stand still inside the static limit.
  The singularity becomes a ring of radius a.

The lilac lines show frame dragging: at each radius, the path of a point
carried round at the angular velocity w(r) = 2a / (r³ + a²r + 2a²) at which
the spin drags space there. They are straight at a = 0 and twist more with
spin and nearer the hole. They are a picture of the dragging, not geodesics.

Where the worldline crosses the static limit (teal), the event horizon
(orange) and the inner horizon (violet), a ring of that colour is drawn
round it, and a caption above the picture says what has just changed. The
playback slows down from about 2.5 outside the event horizon to 12% of the
chosen speed at the horizon and below, so the crossings can be followed; the
equations pane shows the current rate. The "slow down near the horizons"
toggle turns this off.

The white cone and the white line belong to the falling observer. The
yellow crosses are one tick each of the observer's own clock. Their
spacing in the vertical direction, compared with the far time `t`, is the
time dilation.

## How it is made

- `kerr-slice.scm`: plain Scheme. The equatorial Kerr-Schild metric, its
  light cones, and the falling observer (energy E = 1, angular momentum L,
  integrated in ingoing Kerr coordinates). Tested by
  `tests/spacetime_horizon_tests.scm` against the CAS metric from
  `(curry spacetime)`, and against the conserved quantities.
- `main.scm`: the window. The light cones are **not** computed in Scheme.
  The vertex shader calls `metric_eval`, the GLSL that `(curry spacetime)`
  generates from the CAS Kerr metric, and builds an orthonormal frame at
  each cone's apex. The rest is lines drawn with `gpu-*`.

Time is the Kerr-Schild time t = v - r. It is regular at the horizons, and
far away it agrees with the usual far-away clock.

## Limits

- **Prograde angular momentum is capped.** Ingoing Kerr coordinates cover a
  worldline's crossing of the inner horizon only while 2 r₋ − a L > 0. The
  `L` slider can go higher; the program clamps it. Retrograde and radial
  fall are not limited.
- **The worldline ends** if the observer turns around inside the horizons
  and heads back out through r₋: that leads to a region these coordinates do
  not cover.
- **One space dimension is missing.** This is the equator only; the poles
  and the third space direction are not drawn.
- Dropping from r₀ = 4 with a = 0 and L = −4 puts the observer on an unstable
  circular orbit: it hovers at r = 4 for the full 400 of proper time.
- Lines are one pixel wide on Metal.

## As a macOS app

```
tools/make-macos-app.sh --script examples/spacetime-horizon/main.scm \
  --extra examples/spacetime-horizon/kerr-slice.scm --name "Spacetime Horizon" ~/Desktop
```

See `docs/guides/make-macos-app.md`.
