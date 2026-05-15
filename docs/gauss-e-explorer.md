# Gauss's Law for E — Student Guide

**Companion to `examples/gauss-e-explorer.scm`**

```
./build/curry examples/gauss-e-explorer.scm
```

---

## Where this fits in the Maxwell set

| Equation | Physical meaning |
|----------|-----------------|
| **∇·E = ρ/ε₀** | ← **this demo** |
| ∇·B = 0 | No magnetic monopoles |
| ∇×E = −∂B/∂t | Changing B creates circulating E (Faraday) |
| ∇×B = μ₀J + μ₀ε₀∂E/∂t | Currents and changing E create circulating B (Ampere) |

The two curl equations describe *circulation* (Faraday and Ampere demos).
The two divergence equations describe *sources and sinks* — this demo and the Gauss-B demo.

---

## What you're looking at

- **The red or blue disk** — cross-section of a uniformly charged sphere of radius R.
  Red = positive charge, blue = negative charge.
- **Green arrows** — the induced electric field E, always pointing radially (outward
  for positive charge, inward for negative).
- **White circle** — a Gaussian surface of radius r_gauss. The number next to it is
  the total outward flux ∮E·dA through the surface.

---

## Gauss's Law explained

### What is divergence?

The *divergence* of a vector field at a point measures the net outward flow per unit
volume at that point.

Imagine dropping a tiny ball into a flowing river.  If water flows away from the ball
in all directions (a source), the divergence is positive.  If water flows toward the
ball from all directions (a sink), the divergence is negative.  If as much flows in as
flows out, divergence is zero.

For the electric field:
- Positive charge: E radiates outward → positive divergence → source.
- Negative charge: E converges inward → negative divergence → sink.
- Empty space: field lines pass through without starting or ending → divergence zero.

### The equation

```
∇ · E  =  ρ / ε₀
```

In plain English: *"The net outward E field per unit volume at any point equals the
charge density at that point, divided by ε₀."*

Or even simpler: *"E field lines start on positive charges and end on negative charges.
Everywhere else, divergence is zero — the field just passes through."*

### The integral form

By the divergence theorem (∫∫∫ ∇·E dV = ∮∮ E·dA), integrating both sides over any volume:

```
∮_S  E · dA  =  Q_enclosed / ε₀
```

The *total outward flux* of E through any closed surface equals the total charge inside
divided by ε₀. This is Gauss's Law in its most useful form.

**Critical insight:** The flux depends *only* on the enclosed charge.
- The shape of the surface doesn't matter.
- The position of the charge inside doesn't matter.
- Charges *outside* the surface contribute zero net flux (field lines enter and leave).

---

## The sphere: exact solution

For a uniformly charged sphere of radius R with charge density ρ (total charge Q = 4πR³ρ/3):

Apply Gauss's Law to a spherical surface of radius r, using spherical symmetry to
argue that E must be purely radial and depend only on r:

**Inside (r < R):** The enclosed charge is ρ × (4πr³/3).

```
E_r × 4πr²  =  ρ × (4πr³/3) / ε₀

        E_r  =  ρr / (3ε₀)          [linear in r]
```

Every bit of extra radius encloses more charge, so E grows toward the surface.

**Outside (r ≥ R):** The enclosed charge is the full Q = ρ × (4πR³/3).

```
E_r × 4πr²  =  ρR³ / (3ε₀) × (4π)

        E_r  =  ρR³ / (3ε₀r²)  =  Q / (4πε₀r²)    [inverse square]
```

The outside field is exactly Coulomb's law for a point charge Q at the origin.
The entire spherical charge distribution is *invisible* from outside — it looks like
all the charge is concentrated at the centre.

**Continuity at r = R:** ρR/(3ε₀) = ρR³/(3ε₀R²) ✓

---

## Watching the demo

**Drag r_gauss from small to large:**

- While r_gauss < R: ∮E·dA grows as r_gauss³. You're enclosing more charge.
- When r_gauss = R: maximum rate of growth.
- Once r_gauss > R: ∮E·dA is flat. You've enclosed all the charge; making the
  surface bigger adds nothing.

This is the **flux saturation** experiment. You've already done the same thing in
the Faraday and Ampere demos (EMF saturation when the loop exceeds the solenoid radius).
The mathematical structure is identical.

**Toggle the charge sign:**

The arrows flip direction (outward ↔ inward) and the flux changes sign.
The *magnitude* of the flux is the same for equal |ρ|.
This symmetry between positive and negative charge is a deep property of
electrostatics.

---

## CAS verification

Inside the sphere with ρ = ε₀ = 1, the field is E = r⃗/3, giving:

```
E_x = x/3,   E_y = y/3,   E_z = z/3
```

The Curry CAS computes:

```
∇·E = ∂(x/3)/∂x + ∂(y/3)/∂y + ∂(z/3)/∂z
    = 1/3 + 1/3 + 1/3
    = 1  =  ρ/ε₀  ✓
```

For the outside field E_x = x/r³ (Coulomb, with normalisation), the divergence is also
zero for r ≠ 0 — field lines pass straight through without being created or destroyed.

---

## Guided exercises

### Exercise 1 — Flux saturation

Drag r_gauss from 0.3 to 2.8. Note the ∮E·dA value.

- At what r_gauss does the growth stop?
- How does that radius relate to R?

**Expected:** Saturation at r_gauss = R. Final value = Q_total/ε₀ = ρ × (4π/3) × R³.

### Exercise 2 — Sign flip

Toggle the charge sign.

- Do the arrow directions change?
- Does |∮E·dA| change?

**Expected:** Arrows flip, flux changes sign, magnitude unchanged.
Gauss's law is symmetric: ∇·E = ρ/ε₀ handles both signs equally.

### Exercise 3 — R³ scaling

Fix r_gauss outside the sphere. Double R.

- How much does ∮E·dA change?

**Expected:** Increases by 8× (2³). Q_enclosed = ρ × (4π/3) × R³, cubic in R.

### Exercise 4 — ρ vs R

Compare doubling ρ vs doubling R (with r_gauss fixed outside).

- Doubling ρ: Q doubles → ∮E·dA doubles.
- Doubling R: Q increases 8× → ∮E·dA increases 8×.

But what about E_r *at the sphere surface* (r = R)?

- E_r(R) = ρR/(3ε₀). Doubling ρ doubles E_r(R). Doubling R also doubles E_r(R).
  Same effect on the surface field — but very different effect on the total flux.

---

## Real-world connections

**Coulomb's law from Gauss's law**  
A point charge Q surrounded by a spherical surface: E_r × 4πr² = Q/ε₀,
so E_r = Q/(4πε₀r²) — Coulomb's law derived from the more fundamental Gauss's law.

**Faraday cage (electrostatic shielding)**  
Inside a hollow conductor: any Gaussian surface inside encloses zero charge,
so ∮E·dA = 0. By symmetry and charge redistribution, E = 0 everywhere inside.
This is why microwave ovens don't leak and why your car protects you from lightning.

**Capacitor field**  
A parallel-plate capacitor with surface charge density σ: draw a pillbox Gaussian
surface half inside, half outside the plate. ∮E·dA = σA/ε₀, so E = σ/ε₀.
Gauss's law gives the capacitor field directly.

---

## Quick reference

| Symbol | Meaning | Units |
|--------|---------|-------|
| E | Electric field | V/m |
| ρ | Charge density | C/m³ |
| ε₀ | Permittivity of free space = 8.85×10⁻¹² F/m | F/m |
| Q | Total charge = ρ × (4πR³/3) | C |
| ∮E·dA | Total outward electric flux through surface | V·m |

**Key formulas:**

```
∇·E = ρ/ε₀                           [Gauss's law, differential form]
∮E·dA = Q_enclosed/ε₀                 [integral form]

E_r (r < R) = ρr/(3ε₀)               [inside sphere, linear]
E_r (r ≥ R) = ρR³/(3ε₀r²) = Q/(4πε₀r²)  [outside, Coulomb]
```
