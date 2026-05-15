# Gauss's Law for B — Student Guide

**Companion to `examples/gauss-b-explorer.scm`**

```
./build/curry examples/gauss-b-explorer.scm
```

Read `docs/gauss-e-explorer.md` first — this guide builds on the contrast between
electric and magnetic fields.

---

## The complete Maxwell set

| Equation | Physical meaning |
|----------|-----------------|
| ∇·E = ρ/ε₀ | Charges are sources of E (Gauss-E demo) |
| **∇·B = 0** | ← **this demo** |
| ∇×E = −∂B/∂t | Changing B creates circulating E (Faraday demo) |
| ∇×B = μ₀J + μ₀ε₀∂E/∂t | Currents and changing E create circulating B (Ampere demo) |

You've now seen all four. This is the last piece.

---

## What you're looking at

- **Red dot N / blue dot S** — the "north" and "south" poles of a simulated 2D dipole.
- **Cyan arrows** — the magnetic field B, forming the characteristic dipole pattern:
  arcing from N to S in the exterior, returning through the "interior" of the magnet.
- **White circle** — a Gaussian surface you can position with the gy slider.
  The number next to it is ∮B·n̂dl around the surface.

---

## The law: ∇·B = 0

```
∇ · B  =  0
```

Divergence of B is zero everywhere, always.

Recall from the Gauss-E demo: ∇·E = ρ/ε₀, which is non-zero wherever there is charge.
Positive charge is a *source* of E (positive divergence), negative charge is a *sink*.

For B: the divergence is always zero. There are no sources, no sinks.
**B field lines never start and never end. They always form closed loops.**

This is exactly what you see in the simulation: follow any cyan arrow and it will
eventually curve back on itself, forming a closed circuit.

### Integral form

By the divergence theorem:

```
∮_S  B · dA  =  0   for any closed surface S
```

No matter what surface you draw, no matter where you put it, the total outward
magnetic flux through it is zero. As many field lines enter as leave.

---

## What a monopole would look like

In the Gauss-E demo, you could draw a surface around just the positive charge and get
∮E·dA = +Q/ε₀ ≠ 0.  The positive charge is a genuine source of E.

If magnetic monopoles existed — isolated north poles or south poles with no partner —
then ∮B·dA would be non-zero for a surface enclosing one. The law would be
∇·B = μ₀ρ_m (where ρ_m is magnetic charge density), exactly analogous to Gauss-E.

**Try this in the demo:**

1. Default state: gy = 0, rg = 1.0. The surface encloses both the N and S pole.
   ∮B·n̂dl ≈ 0. The north and south contributions cancel.

2. Move gy slider to +0.7 (≈ d). Shrink rg to 0.4. Now only the north pole is inside.
   ∮B·n̂dl ≈ +2πm. **This is what a magnetic monopole would give.**

3. Move gy to −0.7. Only the south pole inside.
   ∮B·n̂dl ≈ −2πm. The south monopole.

The demo shows what monopoles *would* look like. The empirical fact is: **this has
never been observed in any physical magnet, ever.**

---

## Why you can never isolate a pole

Take a bar magnet. Draw a Gaussian surface around the north end only. Does it give
∮B·dA ≠ 0?

No — because the B field lines that appear to emerge from the north end curve around
and re-enter the south end *through the magnet itself*. The field lines close *through
the material*. Any surface that cuts the magnet in two will have field lines passing
through the cut in both directions, and the net flux is zero.

**Cut the magnet in half** (shrink d in the demo). You don't get an isolated north
pole and an isolated south pole. You get two smaller dipoles, each with its own N and S.

Cut it again: four smaller dipoles. Cut to atomic scale: every magnetic domain is a
dipole. Cut to the electron: the electron's magnetic moment is a pure dipole, with
no monopole component. ∇·B = 0 holds at every scale.

---

## The mathematical reason

Why must ∇·B = 0? Because B always arises from currents and changing fields.
Ampere's law says ∇×B = μ₀J_total. In general, B = ∇×A where A is the *vector potential*.

And for any vector field A, a fundamental identity of calculus states:

```
∇ · (∇ × A)  =  0   identically
```

The divergence of any curl is zero. This is a mathematical theorem, not a physical
assumption. It follows from the commutativity of mixed partial derivatives.

The CAS sidebar proves this symbolically for A = (0, xy, xyz):

```
B = ∇×A = (xz, −yz, y)

∇·B = ∂(xz)/∂x + ∂(−yz)/∂y + ∂(y)/∂z
    = z + (−z) + 0
    = 0  ✓
```

The terms z and −z cancel non-trivially — they come from different components but
sum to zero. This is exactly what the CAS must track.

---

## Comparing the two Gauss laws side by side

Run both demos simultaneously and compare.

| | Gauss-E | Gauss-B |
|---|---|---|
| Source region | Red/blue charged sphere | Red N / blue S dipole poles |
| Arrow direction | Radial (outward/inward) | Closed loops (dipole pattern) |
| Surface encloses single "charge" | ∮·dA = Q/ε₀ ≠ 0 | ∮·dl ≈ ±2πm ≠ 0 (in simulation) |
| Surface encloses both | ∮·dA = (Q₊+Q₋)/ε₀ | ∮·dl ≈ 0 |
| In nature | Can isolate charge | Cannot isolate pole |
| Law | ∇·E = ρ/ε₀ | ∇·B = 0 |

The simulation uses mathematical pseudo-monopoles to model the exterior dipole field.
In reality, for a physical magnet, any Gaussian surface gives ∮B·dA = 0 because
the field lines close *through the magnet itself* — a path the simulation omits.

---

## Guided exercises

### Exercise 1 — Both poles enclosed

Set gy = 0, rg = 1.0 (default). Both poles are inside.

What is ∮B·n̂dl? Compare with the reference value 2πm shown in the live panel.

**Expected:** ≈ 0. The north contribution (+2πm) and south contribution (−2πm) cancel exactly.

### Exercise 2 — Isolate the north pole

Set gy = d (the pole separation), rg = 0.4.

- What is ∮B·n̂dl now?
- How does it compare to 2πm?

**Expected:** ≈ +2πm. This is the "monopole signal." Adjust m and see the reference value track it.

### Exercise 3 — Shrink the magnet

With north pole isolated (gy = d, rg = 0.4), reduce d toward zero.

- The poles move closer together.
- Does ∮B·n̂dl change when d shrinks?

**Expected:** As long as rg still encloses only the north pole, the flux stays ≈ +2πm regardless of d. The pole strength m matters; the separation d does not (as long as the south pole is outside the surface).

**Implication:** Even if you could somehow compress a magnet to a point, the monopole moment (if it had one) would still give non-zero flux. The fact that real magnets always give zero is evidence of no monopole moment at any scale.

### Exercise 4 — Compare with Gauss-E

Open the Gauss-E demo alongside this one. Place the Gauss-E surface outside the sphere (enclosing all charge Q) and the Gauss-B surface enclosing both poles.

- Gauss-E: ∮E·dA ≠ 0.
- Gauss-B: ∮B·n̂dl ≈ 0.

This is the fundamental asymmetry between E and B. Electric charges exist as isolated objects (proton, electron). Magnetic poles do not.

---

## The Cabrera event (1982)

On 14 February 1982, Blas Cabrera at Stanford recorded a sudden jump in a superconducting loop consistent with the passage of a single magnetic monopole. The event has never been repeated — by Cabrera or anyone else.

The sensitivity of modern monopole detectors: the flux through a superconducting loop changes by exactly one *Dirac unit* (= 2×10⁻¹⁵ Wb) when a monopole passes through. Detectors have been running continuously since the 1980s with no confirmed events.

Grand Unified Theories (GUTs) predict monopoles should have been created in the early universe, but at a density so low they may never pass near Earth. Their absence is consistent with cosmic inflation diluting them below detectable levels.

∇·B = 0 remains the best-tested of the four Maxwell equations.

---

## Quick reference

| Symbol | Meaning |
|--------|---------|
| B | Magnetic field (T) |
| A | Vector potential: B = ∇×A |
| m | Pole strength in simulation |
| ∮B·dA | Total outward magnetic flux through surface |
| 2πm | Flux from one 2D pseudo-monopole |

**Key facts:**

```
∇·B = 0                     [always, everywhere]
∮B·dA = 0                   [for any closed surface]
B = ∇×A  →  ∇·B = 0        [mathematical guarantee]
∇·(∇×A) = 0                [curl identity, proved by CAS]
```

**Contrast:**

```
Gauss-E:  ∮E·dA = Q_enclosed/ε₀  (non-zero when charge inside)
Gauss-B:  ∮B·dA = 0               (zero, always, no monopoles)
```
