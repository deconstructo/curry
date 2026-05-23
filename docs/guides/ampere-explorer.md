# Ampere's Law Explorer — Student Guide

**Companion document for `examples/ampere-explorer.scm`**  
**Read `docs/faraday-explorer.md` first — this guide builds directly on it.**

Run the program:
```
./build/curry examples/ampere-explorer.scm
```

---

## The big picture: where this fits

You've now seen two of Maxwell's four equations.

| Equation | What it says |
|----------|-------------|
| ∇ × E = −∂B/∂t | Changing **B** creates circulating **E** (Faraday) |
| ∇ × B = μ₀J + μ₀ε₀ ∂E/∂t | Currents and changing **E** create circulating **B** (Ampere) |

These two are partners.  Faraday's law and Ampere's law together are what make
electromagnetic waves possible — and understanding why requires both.

---

## What you're looking at

The setup is a mirror image of the Faraday demo:

- **The central disk** (orange/purple) is the *source* — either an oscillating electric
  field E_z (displacement current mode) or an oscillating current density J_z
  (conduction mode).
- **The cyan arrows** are the induced or driven magnetic field B, circulating around
  the source region.
- **The white circle** is an Amperian loop at radius r_loop for measuring ∮ B·dl.

Everything is the same geometry as the solenoid in the Faraday demo.  The roles of
E and B are simply swapped.

---

## Ampere's Law from scratch

### The classical version (before Maxwell)

Ampere found, experimentally, that a wire carrying current I creates a magnetic field
that circulates around it.  The field strength at radius r from a long straight wire is:

```
B = μ₀ I / (2πr)
```

In differential form: ∇ × B = μ₀J, where J is the current density (A/m²).

This works beautifully for steady currents.

### Maxwell's problem with the capacitor

Consider a capacitor being charged by a current I.  The current flows through the
connecting wires, but *nothing* passes through the gap between the plates.

Now imagine two surfaces bounded by the same loop around the wire:

- **Surface 1**: a flat disk that the wire passes through.  It encloses current I.
  Ampere's law gives a non-zero B. ✓
  
- **Surface 2**: a balloon-shaped surface that bulges out between the capacitor plates,
  avoiding the wire entirely.  It encloses current 0.
  Ampere's law gives B = 0. ✗

Same loop, same law — two different answers.  Ampere's original law is *inconsistent*
for time-varying fields.

### Maxwell's fix: the displacement current

Between the capacitor plates, E is growing as the capacitor charges.
Maxwell noticed that ε₀ ∂E/∂t has the same units as current density (A/m²).

He added it to Ampere's law:

```
∇ × B  =  μ₀J  +  μ₀ε₀ ∂E/∂t
```

With this term, surface 2 now sees ε₀ ∂E/∂t (the displacement current) instead of
real current — and it gives the same B as surface 1.  The contradiction disappears.

More profoundly: *even in regions with no charges at all*, a changing E creates B.
This is symmetric with Faraday's law, which says a changing B creates E.

---

## Two modes: displacement vs conduction

### Displacement current mode (the Maxwell addition)

The orange disk represents E_z oscillating inside a cylindrical region:

```
E_z(t) = E₀ cos(ωt)
```

The displacement current density is:

```
J_d = ε₀ ∂E_z/∂t  =  −ε₀ E₀ ω sin(ωt)
```

This drives B exactly as a real current would.  The B solution is:

```
B_φ (r < R)  =  (μ₀/2) r × J_d        [linear in r]
B_φ (r ≥ R)  =  (μ₀/2) (R²/r) × J_d  [falls as 1/r]
```

**Phase**: J_d = −E₀ω ε₀ sin(ωt).  Since E ∝ cos(ωt) and J_d ∝ −sin(ωt), they
are 90° out of phase.  **B is maximum when E crosses zero, and zero when E is
at its peak** — exactly the same 90° relationship as in the Faraday demo.

### Conduction current mode (classical Ampere)

Now the source is a real current density oscillating in a wire:

```
J_z(t) = J₀ cos(ωt)
```

The B solution is the same formula, but with J_z instead of J_d:

```
B_φ (r < R)  =  (μ₀/2) r × J_z
B_φ (r ≥ R)  =  (μ₀/2) (R²/r) × J_z
```

**Phase**: B ∝ J ∝ cos(ωt).  **B is in phase with J — no 90° lag.**  When the
current is maximum, the field is maximum.  When the current reverses, the field
reverses.  There is no derivative involved.

### The key difference, side by side

| | Displacement (∂E/∂t) | Conduction (J) |
|---|---|---|
| Source shown | E_z (orange disk) | J_z (orange disk) |
| Driver of B | ε₀ ∂E/∂t | J directly |
| Phase of B vs source | 90° lag | In phase |
| What crosses the gap | Nothing — field only | Actual charges |

Switch modes with [m] and compare.

---

## Derivation: the exact B field

The geometry is identical to the Faraday solenoid derivation — same cylindrical
symmetry, same technique.

**Integral form of Ampere's law** (from ∇×B = μ₀J_total via Stokes' theorem):

```
∮_C  B · dl  =  μ₀ ∫∫_S  J_total · dA
```

Where J_total = J + ε₀∂E/∂t combines both current types.

**Symmetry**: B can only be azimuthal and depend only on r.  So:

```
∮ B · dl  =  B_φ × 2πr
```

**Inside (r < R)**:
```
I_enclosed = J_eff × πr²

B_φ × 2πr  =  μ₀ × J_eff × πr²

B_φ  =  (μ₀/2) × r × J_eff       [grows linearly with r]
```

**Outside (r ≥ R)**:
```
I_enclosed = J_eff × πR²   (all the source is inside — doesn't grow with r)

B_φ × 2πr  =  μ₀ × J_eff × πR²

B_φ  =  (μ₀/2) × (R²/r) × J_eff  [falls as 1/r]
```

**Continuity at r = R**: (μ₀/2)R = (μ₀/2)(R²/R) ✓

**Direction (right-hand rule)**:  
J_eff > 0 (out of page) → B circulates counterclockwise.  
There is **no minus sign** here, unlike Faraday's law.  B obeys J directly.

---

## The 90° phase relationship: displacement mode

This is worth dwelling on because it's the key to electromagnetic waves.

Start at t = 0 (use Reset, then step with [→]):

**Phase 0 — E maximum**  
E_z = +E₀ (orange disk bright).  ∂E/∂t = 0 (E momentarily not changing).  
B = 0 everywhere.  Cyan arrows invisible.

*"Strong E, but it's not changing — so it drives nothing."*

**Phase 1 — E crossing zero (going negative)**  
E_z = 0 (disk dark).  ∂E/∂t = −E₀ω (maximum rate of decrease).  
B is at maximum, circulating clockwise (∂E/∂t < 0).

*"E is changing fastest — displacement current is maximum — B is maximum."*

**Phase 2 — E minimum**  
E_z = −E₀ (purple disk bright).  ∂E/∂t = 0 again.  
B = 0 again.

**Phase 3 — E crossing zero (going positive)**  
E_z = 0.  ∂E/∂t = +E₀ω.  B maximum, now counterclockwise.

The pattern: **E and B are 90° out of phase in time.**  This is identical to the
Faraday demo, where B and E were 90° out of phase.  The two laws have the same
mathematical structure — they are truly each other's mirror image.

---

## The integration loop

The white circle at r_loop lets you measure:

```
∮ B · dl  =  μ₀ × I_enclosed  =  μ₀ × J_eff × π × r_eff²
```

where r_eff = min(r_loop, R).

**Drag r_loop from small to large:**

- While r_loop < R: ∮B·dl grows as r_loop².  The loop captures more of the source.
- Once r_loop > R: ∮B·dl saturates.  All the source is enclosed; bigger loop adds nothing.

This is identical to the EMF saturation experiment in the Faraday demo.

The small arrow on the loop shows B's circulation direction (right-hand rule from J_eff).

---

## What the sliders teach

**E₀ or J₀ (peak amplitude)**  
Scales B proportionally.  Doubling E₀ doubles ∂E/∂t and therefore doubles B.

**ω (angular frequency) — displacement mode**  
This is the important one.  The displacement current is ε₀ ∂E/∂t = −ε₀E₀ω sin(ωt).
Doubling ω doubles ∂E/∂t even though the peak E is unchanged.  The yellow row
(ε₀∂E/∂t) in the live panel grows while the orange row (E_z) stays the same size.

*This is why higher-frequency EM waves carry more energy — the fields change faster.*

**ω — conduction mode**  
Doubling ω also makes the current oscillate twice as fast, which doubles ∂J/∂t —
but B is just proportional to J itself (not ∂J/∂t), so changing ω only changes
*how fast* B oscillates, not its peak amplitude (assuming J₀ is fixed).

Compare the two modes at ω = 3 vs ω = 0.5 to see the difference.

**R (source radius)**  
Larger R: more source area, stronger displacement current total, stronger B outside.
The saturation of ∮B·dl moves to a larger r_loop value as R increases.

---

## The right-hand rule vs Lenz's law

In the Faraday demo you saw the minus sign in ∇×E = **−**∂B/∂t — Lenz's law, where
induced E *opposes* the change in B.

Ampere's law has **no minus sign** in ∇×B = μ₀J + μ₀ε₀∂E/∂t.  B obeys J (or ∂E/∂t)
directly, in the right-hand sense.

This asymmetry is real and important.  It means:

- Faraday: growing B into the page → E circulates CW (opposing the increase)
- Ampere: J out of the page → B circulates CCW (in the right-hand sense, period)

Watch the circulation arrow on the white loop.  In the Faraday demo it switched
direction based on whether B was growing or shrinking (Lenz's law sign).  Here in
displacement mode it switches based on the sign of ∂E/∂t — and **the direction when
∂E/∂t > 0 is counterclockwise** (the natural right-hand direction for positive source).

---

## How this leads to electromagnetic waves

Take Faraday and Ampere together in free space (no J, no charges):

```
∇ × E  =  −∂B/∂t
∇ × B  =   μ₀ε₀ ∂E/∂t
```

Take the curl of the first equation:

```
∇ × (∇ × E)  =  −∂(∇×B)/∂t  =  −μ₀ε₀ ∂²E/∂t²
```

Using the vector identity ∇×(∇×E) = ∇(∇·E) − ∇²E, and ∇·E = 0 in free space:

```
∇²E  =  μ₀ε₀ ∂²E/∂t²
```

This is the wave equation!  The wave speed is:

```
c  =  1/√(μ₀ε₀)  =  3 × 10⁸ m/s  =  speed of light
```

Maxwell derived this in 1865 — a theoretical prediction that light is an
electromagnetic wave, a decade before it was confirmed experimentally.  The
displacement current (the ∂E/∂t term) was absolutely essential: without it, the
wave equation doesn't exist.

---

## Guided exercises

*(These match the exercises in the sidebar.)*

### Exercise 1 — B = 0 in displacement mode

Pause when the cyan arrows disappear.

- What is E_z in the live panel?
- What is ε₀∂E/∂t?

**Expected:** E_z is at maximum; ε₀∂E/∂t ≈ 0.  
**Why:** Ampere's law says ∇×B = μ₀ε₀∂E/∂t.  When ∂E/∂t = 0, the right-hand side
is zero, so B = 0 no matter how strong E is.  *It is the changing E, not E itself,
that creates B.*

### Exercise 2 — In-phase vs 90° out-of-phase

Switch to conduction mode [m].  Pause when the cyan arrows are longest (B maximum).

- What is J_z in the live panel?
- Is it near maximum or near zero?

**Expected:** J_z is also at maximum.  
**Contrast:** In displacement mode, B is maximum when E = 0 (90° lag).  
In conduction mode, B is maximum when J is maximum (in phase).

**Why the difference?**  The displacement current depends on *∂E/∂t*, not E itself —
a derivative introduces a 90° phase shift.  The conduction current is J directly —
no derivative, no phase shift.

### Exercise 3 — Circulation saturation

Drag r_loop slowly from 0.3 to 2.5 while the simulation runs.

- When does ∮B·dl stop growing?
- What is its final value?

**Expected:** Saturation when r_loop = R.  
Final value ≈ μ₀ × J_eff × πR² (total enclosed "current").  
**This is the same experiment as Exercise 3 in the Faraday demo** — the
mathematical structure is identical in both laws.

### Exercise 4 — ω in displacement mode

Start with ω = 1.0 and note the arrow lengths.

Increase ω to 3.0 (max).  The arrows grow much longer.

- E₀ hasn't changed — why is B larger?
- Check the ε₀∂E/∂t row (yellow) vs the E_z row (orange) as you increase ω.

**Expected:** ε₀∂E/∂t = −ε₀E₀ω sin(ωt).  Peak amplitude = ε₀E₀ω.  Tripling ω
triples the displacement current, tripling B.  The orange row (E_z amplitude) stays
the same; only the yellow row grows.

**Now switch to conduction mode and repeat:** ω has almost no effect on peak B.
B_φ ∝ J₀ in conduction mode; ω only affects the oscillation rate, not amplitude.

---

## CAS verification in the sidebar

The Curry CAS computes both sides of Ampere's law for the plane wave
E_y = cos(x−t), B_z = cos(x−t) (c = 1, J = 0):

```
(∇×B)_y  =  −∂B_z/∂x  =  sin(x−t)
μ₀ε₀ ∂E_y/∂t           =  sin(x−t)
```

Residual = 0.  The law holds for every x and t simultaneously — a symbolic proof,
not just a numerical check at specific values.

Compare with the Faraday CAS check in the other program.  Same wave, same technique,
but verifying the other Maxwell equation.  Together they confirm that the plane wave
satisfies *both* equations simultaneously — which is why it can propagate.

---

## Common confusions

**"The displacement current isn't real — it's just a trick."**  
Maxwell introduced the term by analogy with the polarisation current in a dielectric,
but the physics is unambiguous: a changing E field in vacuum creates a circulating
B field.  This has been confirmed experimentally many times.  It is not a bookkeeping
trick — it is a physical effect.

**"∂E/∂t drives B, so shouldn't B also create more E, which creates more B, exploding?"**  
This is exactly what happens in an electromagnetic wave, but the fields don't explode
because the coupled equations are balanced.  Ampere says ∂E/∂t drives ∇×B, and
Faraday says ∂B/∂t drives ∇×E.  The two feed each other and the solution is a stable
travelling wave, not a runaway.

**"In conduction mode, B is in phase with J.  But in the Faraday demo, E and B were
90° out of phase.  Are these contradictions?"**  
No.  In the Faraday demo, the *induced* E was 90° behind the *externally imposed* B
(because Faraday's law involves ∂B/∂t, a derivative).  Here in conduction mode,
B is driven directly by J with no derivative — so no phase shift.  The displacement
current case does have a derivative (∂E/∂t), giving the same 90° lag.

---

## Quick reference

| Symbol | Meaning | Units |
|--------|---------|-------|
| B_φ | Azimuthal magnetic field | T (Tesla) |
| E_z | Electric field (z-direction) | V/m |
| J_z | Conduction current density (z-direction) | A/m² |
| J_d = ε₀∂E/∂t | Displacement current density | A/m² |
| μ₀ | Permeability of free space = 4π×10⁻⁷ H/m | H/m |
| ε₀ | Permittivity of free space = 8.85×10⁻¹² F/m | F/m |
| c = 1/√(μ₀ε₀) | Speed of light | m/s |

**Key formulas:**

```
J_eff (displacement):  ε₀ ∂E_z/∂t  =  −ε₀ E₀ ω sin(ωt)
J_eff (conduction):    J_z           =   J₀ cos(ωt)

B_φ (r < R):  (μ₀/2) × r × J_eff            [linear — more area, more source]
B_φ (r ≥ R):  (μ₀/2) × (R²/r) × J_eff       [1/r — total source fixed]

∮ B·dl  =  μ₀ × J_eff × π × r_eff²          where r_eff = min(r_loop, R)

Wave speed:  c = 1/√(μ₀ε₀)
```
