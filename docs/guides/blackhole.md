# Falling into a black hole

**Companion document for `examples/blackhole.scm` and `examples/blackhole-anaglyph.scm`**

```
./build/curry examples/blackhole.scm             # standard view
./build/curry examples/blackhole-anaglyph.scm    # red/cyan 3D (red over the LEFT eye)
```

Needs the `gpu-*` layer of `(curry qt6)` (Qt ≥ 6.7 with Qt Shader Tools; see
[INSTALL.md](INSTALL.md)). On a Mac it runs on Metal, on Linux on Vulkan.

---

## What you are looking at

You are a camera floating at rest 30 M from a rotating (Kerr) black hole,
25° above the plane of a thin, glowing accretion disk. M is the hole's mass
in geometric units: the event horizon of a non-rotating hole is at 2 M.
Press **Space** and you fall freely toward it, through the event horizon, until
the fall stops at the inner horizon.

Nothing in the picture is painted on. Every pixel is one ray of light, traced
backwards from the camera through curved spacetime on the GPU, until it
reaches the disk, the hole, or the distant sky.

### From far away

- **The shadow.** The black disk in the middle is the set of directions from
  which no light reaches you: those rays end in the hole. For a spinning hole
  it is not round. One side is flattened, on the side where light orbits
  with the spin and can come closer before being captured. Press **S** to go
  up to spin 0.998, where the flat side is clearest.
- **The disk is bent over the hole.** Light from the far side of the disk,
  behind the hole, is bent up over the top and under the bottom of the
  shadow, so you see the back of the disk as an arch.
- **One side is brighter and bluer.** The disk rotates. Its gas moving
  toward you looks brighter and bluer, and the receding side dimmer and
  redder (Doppler beaming). Near the hole, gravity also reddens all of it.
  For every point of the disk, the frequency ratio g between what you see
  and what the gas emits is computed exactly; brightness scales as g⁴ and
  colour temperature as g.
- **Thin rings.** Very close to the shadow's edge there are thin bright
  rings: light that circled the hole once or more before escaping.
- **The bent grid.** The faint grid on the sky is a latitude/longitude
  grid on the distant celestial sphere. Its distortion shows where and how
  strongly light is bent (gravitational lensing).

### While falling

- The shadow grows and spreads across the view as you approach.
- The sky and disk shift in colour and brightness because *you* are now
  moving fast: the same frequency calculation includes the camera's own
  velocity.
- The window title shows your distance r, your own elapsed time (proper
  time, in units of M), and `INSIDE THE HORIZON` once you have crossed it.
  Nothing special happens at the crossing itself. You don't feel it; it is
  a point of no return, not a wall.

### Inside the horizon

- The outside universe is squeezed into a band to one side, and the disk's
  image is a thin, strongly blue-shifted ribbon.
- **The large black region inside the horizon is partly a limit of this
  program, not something you would see.** In some directions you look back
  toward parts of spacetime that the coordinates used here (ingoing
  Kerr–Schild) do not cover. Rays traced into those parts stall, and are
  drawn black.
- The fall **stops at the inner horizon**. Beyond it the Kerr solution is
  not considered physically realistic, and these coordinates also break down
  near the ring-shaped singularity. The title says when you have reached it;
  press **R** to start again.

### In 3D (anaglyph)

Each eye is a separate camera, a little to the side in your own falling
frame, and each traces its own rays. So depth in the image comes from the
real light paths. Even distant stars show a small depth difference: the two
eyes look through slightly different parts of the hole's gravitational
lens, so the stars are shifted by different amounts.

---

## Controls

| Key | Action |
|-----|--------|
| **Space** | Start / pause the fall |
| **R** | Reset to the starting point |
| **S** | Cycle the spin a: 0, 0.5, 0.9, 0.998 (resets) |
| **A** | Anaglyph on / off |
| **D** | Disk on / off |
| **Arrows**, or drag with the mouse | Look around |
| **+ / -** | Fall faster / slower |
| **[ / ]** | Darker / brighter |
| **Q** or **Escape** | Quit |

Command-line options:

| Option | Meaning |
|--------|---------|
| `--anaglyph` | Start in red/cyan 3D (same as `blackhole-anaglyph.scm`) |
| `--start-r R` | Starting distance in M (default 30) |
| `--elevation DEG` | Starting height above the disk plane, in degrees (default 25; try 3 for the classic edge-on view) |
| `--spin a` | Spin a, at least 0 and less than 1 (default 0.9) |
| `--fall` | Start falling immediately |
| `--fall-speed S` | Proper time per second of real time, in M (default 5) |
| `--snapshot PATH` | Save a PNG and quit, after `--snapshot-after SECONDS` (default 1.5) |

---

## How it works

1. **The spacetime is written symbolically.** `(curry spacetime)` defines the
   Kerr metric in Cartesian Kerr–Schild coordinates, using curry's CAS. These
   coordinates are regular at the event horizon, which is why the camera can
   fall through it.
2. **The CAS differentiates it.** All 40 distinct partial derivatives of the
   metric are derived exactly and simplified.
3. **`(curry glsl)` turns the formulas into GPU code** (GLSL), with shared
   sub-expressions computed once.
4. **Light rays are traced on the GPU.** A compute shader integrates one
   backward null geodesic per pixel in Hamiltonian form, with an adaptive
   Runge–Kutta step sized by distance travelled.
5. **The camera falls on the GPU too.** A second, one-thread compute shader
   moves the camera along its own timelike geodesic with the same generated
   code, and builds its local frame (which way is forward, right and up for a
   falling observer).

The physics is checked by the `spacetime_gpu` test suite. Rays traced with
the generated code match the exact Schwarzschild bending to about one part
in ten million. The capture limit 3√3 M comes out right, and so do Kerr's
different capture limits for light moving with and against the spin. See
[module-spacetime.md](../reference/module-spacetime.md).

---

## Things to try

- Start at `--spin 0.998 --elevation 3` to see the flattened shadow and the
  disk wrapped over the top.
- Spin 0 (`--spin 0`) for a non-rotating hole: a round shadow, and a fall
  that stops just before the central singularity (a non-rotating hole has
  no inner horizon).
- While falling, look back toward where you came from (arrow keys).
- Hide the disk (**D**) to see only the lensed sky and grid.

## Performance

Each frame traces about one ray per logical pixel: a few hundred thousand
rays, each up to 1200 steps. On an Apple M1 Max this is smooth at the
default window size. If your machine struggles, make the window smaller
(the work grows with the number of pixels), or press **Q** to quit at once.
