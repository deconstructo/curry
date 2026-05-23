# Faraday's Law Explorer — Student Guide

**Companion document for `examples/faraday-explorer.scm`**

Run the program first:
```
./build/curry examples/faraday-explorer.scm
```

---

## What you're looking at

The coloured circle in the centre is the cross-section of a long cylindrical solenoid — like
looking straight down the barrel of an electromagnet.  The field inside it (B, pointing in or
out of the screen) oscillates sinusoidally: B = B₀ cos(ωt).

The green arrows filling the canvas are the **induced electric field E** caused by that
changing magnetic field.  They appear and disappear, grow and shrink, reverse direction —
and understanding *why* is the whole point of this exercise.

The white circle at a different radius (labelled rₗ) is an imaginary closed loop you're
using to measure the "voltage" the field would drive around it.

---

## Faraday's Law from scratch

### The everyday intuition

You've probably seen this: move a magnet near a loop of wire and a current flows.
The moving magnet *creates* an electric field that pushes charges around the wire.
Faraday's Law is the precise mathematical statement of exactly that phenomenon.

But here's the thing that trips up most students: **it isn't the magnetic field itself
that matters — it's the *rate of change* of the magnetic field.**

Hold a powerful magnet completely still next to a wire: no current flows, no matter how
strong the magnet is.  Move it and current flows.  The faster you move it — the faster B
changes — the more current flows.

This is the heart of the law, and the simulation is designed to make it viscerally obvious.

### The equation

Maxwell wrote it like this:

```
∇ × E = −∂B/∂t
```

Let's decode each part.

**∇ × E — the "curl" of E**

Imagine you're a tiny paddle wheel floating in the electric field.  If the field
circulates around you — going one way on your left, the other way on your right —
the wheel spins.  That spin is the *curl*.

If curl = 0 everywhere, the field points in neat parallel lines like the field of a
point charge.  You learned in electrostatics that curl(E) = 0 — electric fields from
charges don't circulate.

But when a magnetic field is changing, curl(E) ≠ 0.  The field *does* circulate.
That's new, and that's what this program shows you.

**−∂B/∂t — the time-rate-of-change of B**

∂B/∂t is just "how fast is the magnetic field changing right now?"
The minus sign is crucial — we'll come back to it.

**Putting it together**

> "Wherever a magnetic field is changing in time, an electric field curls around it."

The minus sign tells you which *way* it curls.  That's Lenz's law, coming up.

---

## Lenz's law: why the minus sign matters

The minus sign isn't just bookkeeping.  It encodes one of the deepest ideas in
classical physics: **induced fields always oppose the change that caused them.**

Here's how to read it:

- B is increasing (pointing out of the page and getting stronger):
  ∂B/∂t > 0, so −∂B/∂t < 0.
  The curl of E is negative — E circulates **clockwise**.
  If a current flowed clockwise, by the right-hand rule it would produce B pointing
  *into* the page — opposing the increase.  ✓

- B is decreasing:
  ∂B/∂t < 0, so −∂B/∂t > 0.
  E circulates **counterclockwise**.
  A counterclockwise current would produce B pointing *out* of the page —
  opposing the decrease.  ✓

**Watch for this in the simulation.**  When B (the blue fill) is getting stronger,
the circulation arrow on the white loop points clockwise.  When B is fading, it
reverses.  The field always fights back.

This opposition is why motors resist being driven and generators resist being turned.
It is the origin of electrical inductance.

---

## The setup: why this geometry is solvable exactly

Real-world fields are usually complicated and require numerical methods.  The
cylindrical solenoid is a rare case where Faraday's Law can be solved on paper.

The reason: **symmetry.**

The solenoid is infinitely long and perfectly circular.  No direction around the
axis is special; no direction along the axis is special.  Therefore:

- The induced E can only point in the **azimuthal** (tangential to circles) direction.
  If it pointed radially or along the axis, one side of the circle would differ from
  the other — but our setup is symmetric, so that can't happen.

- The magnitude of E can only depend on **r**, the distance from the axis.
  It can't depend on the angle φ or the height z (same symmetry argument).

This observation — that symmetry restricts the form of the answer *before we do any
calculation* — is one of the most powerful tools in physics.  We haven't computed
anything yet and already we know that E looks like concentric circles.

---

## Deriving the field: Faraday's Law in integral form

The differential form (∇ × E = −∂B/∂t) is elegant, but for calculating fields in
practice, physicists usually use the **integral form**, which follows from Stokes' theorem:

```
∮_C  E · dl  =  −d/dt ∫∫_S  B · dA
```

In plain English: **"The EMF around any closed loop equals minus the rate of change
of the magnetic flux through it."**

EMF is the "voltage" the field would drive around the loop.  Flux Φ is the total
amount of B passing through the surface the loop encloses.

Now we pick a circular loop of radius r centred on the axis.  Because of the symmetry,
E is constant in magnitude all around this loop and always tangential to it.
So the left side simplifies beautifully:

```
∮ E · dl  =  E_φ × (circumference)  =  E_φ × 2πr
```

We've reduced the line integral to simple multiplication.  Now we compute the right side.

### Case 1: the loop is inside the solenoid (r < R)

The loop encloses an area πr² of field B_z:

```
Φ = B_z × πr²
−dΦ/dt = −πr² × ∂B_z/∂t
```

Setting left = right:

```
E_φ × 2πr  =  −πr² × ∂B_z/∂t

                  ∂B_z
        E_φ  = −r/2 × ────
                   ∂t
```

Notice: **E_φ grows linearly with r inside the solenoid.**  Points further from the
axis are at the rim of a larger circle, which encloses more area, which encloses
more flux change, so they feel a stronger field.

In the simulation, watch how the green arrows get longer as you move outward from the
centre (while still inside the coloured circle).

### Case 2: the loop is outside the solenoid (r ≥ R)

B = 0 outside the solenoid.  All the flux through the loop comes from the solenoid
interior, which has area πR²:

```
Φ = B_z × πR²   (doesn't grow with r — the solenoid is fixed)
−dΦ/dt = −πR² × ∂B_z/∂t
```

Setting left = right:

```
E_φ × 2πr  =  −πR² × ∂B_z/∂t

                   R²   ∂B_z
        E_φ  =  − ─── × ────
                   2r    ∂t
```

Outside, **E_φ falls off as 1/r.**  The total EMF around any loop that encloses the
full solenoid is the same (it depends only on R and ∂B/∂t, not on r).  But that
same EMF is shared around a longer path as r grows, so the field at each point is weaker.

Watch the arrows in the outer region: they shorten as you move away from the solenoid.

### Continuity check

At r = R exactly, the two formulas must agree:

- Inside formula at r = R: E_φ = −R/2 × ∂B/∂t
- Outside formula at r = R: E_φ = −R²/(2R) × ∂B/∂t = −R/2 × ∂B/∂t  ✓

They match.  The field is continuous across the solenoid boundary — no sudden jump.

### Summary: the complete solution

```
         ⎧ −(r/2)   × ∂B_z/∂t    if r < R   (linear in r)
E_φ  =  ⎨
         ⎩ −(R²/2r) × ∂B_z/∂t   if r ≥ R   (1/r decay)
```

---

## Converting to arrows on screen

The formula gives E_φ — the tangential component.  To draw arrows, we need the
Cartesian components (E_x, E_y).

At a point (x, y), the azimuthal unit vector is:

```
ê_φ = (−y/r, x/r)
```

So:

```
E_x = E_φ × (−y/r)
E_y = E_φ × (x/r)
```

This is what the `E-at` function in the source code computes.

---

## The four key moments: walking through one full period

Use the **"→ +¼ period"** button (or the Right arrow key) to step through these.

**Phase 0 — t = 0**
B is at maximum (+B₀).  ∂B/∂t = 0.  E = 0 everywhere.
The coloured circle is brightest; the green arrows are invisible.
*The field is strong but not changing — so it induces nothing.*

**Phase 1 — t = T/4**
B is crossing zero (going from + to −).  |∂B/∂t| is at maximum.  E is at maximum.
The arrows are longest; the circle is dark.
*The field is changing fastest — that's why E is strongest.*

**Phase 2 — t = T/2**
B is at minimum (−B₀).  ∂B/∂t = 0 again.  E = 0 again.
But this time the solenoid glows red (field into the page).
Step here and notice: the situation is the same as Phase 0, but with B reversed.

**Phase 3 — t = 3T/4**
B is crossing zero (going from − to +).  E is at maximum again, but **reversed**.
The circulation arrows point clockwise instead of counterclockwise.
*Now B is increasing in the +z direction (recovering from −B₀ toward zero), so Lenz's law drives the induced E in the opposite sense to Phase 1.*

Then the cycle repeats.

**The key insight repeated:** E and B are 90° out of phase in time.
E is maximum when B is zero (changing fastest), and E is zero when B is maximum
(not changing at all).

---

## The integration loop: EMF in practice

The white circle is an imaginary "Faraday loop" — a tool for applying the integral
form of Faraday's Law.

The EMF readout next to it shows:

```
EMF = −π × r_eff² × ∂B_z/∂t
```

where r_eff = min(r_loop, R).

**Try this:** Drag the rₗ slider slowly from small to large.

- While rₗ < R: EMF grows as r_loop².  The loop encloses more of the solenoid's
  field as it expands, capturing more flux change.

- Once rₗ > R: EMF stops growing.  The loop already encloses *all* of the solenoid;
  making it bigger adds no more flux.

This saturation is a direct consequence of B = 0 outside the solenoid.

---

## What the sliders teach

**B₀ (peak field strength)**
Increasing B₀ scales E proportionally.  ∂B/∂t = −B₀ω sin(ωt), so doubling B₀
doubles the peak ∂B/∂t and therefore doubles E.

**ω (angular frequency)**
This is the surprising one.  ∂B/∂t = −B₀ω sin(ωt).  Doubling ω doubles the
rate of change even though the peak B value is the same.  Try it: increase ω and
watch the arrows grow longer *without* the coloured circle getting brighter.

This is why power transformers must operate at a fixed frequency: the induced
voltage scales directly with ω.  The 50 Hz or 60 Hz of your mains supply is
carefully controlled partly for this reason.

**R (solenoid radius)**
Making R larger makes the solenoid cross-section bigger.  Inside, E_φ = −(r/2)∂B/∂t
is unchanged in magnitude.  Outside, |E_φ| = (R²/2r)|∂B/∂t| grows — a larger solenoid produces a
stronger field in the exterior region.  Also watch how the EMF saturates at a larger
loop radius as R grows.

---

## Guided exercises

*(These match the exercises listed in the sidebar.)*

### Exercise 1 — The E = 0 moment

Pause the simulation and use the +¼ button to reach a moment when all the green
arrows disappear.

- What is the value of B_z in the live panel?
- What is the value of ∂B/∂t?

**Expected:** |B_z| is at its maximum; ∂B/∂t ≈ 0.
**Why:** Faraday's Law says E is driven by ∂B/∂t.  When ∂B/∂t = 0, there is no
driving — so E = 0 no matter how strong B is.

### Exercise 2 — The E maximum

Step one more quarter period forward.  Now E is at maximum.

- What is the value of B_z now?
- Is it large or small?

**Expected:** |B_z| ≈ 0; ∂B/∂t is at its maximum magnitude.
**Why:** This is the moment B is crossing zero, which is also the moment it is
changing *fastest*.  Fast-changing B → strong induced E.

Summarise: **E and B are 90° out of phase in time.** When one is maximum, the other
is zero.

### Exercise 3 — Flux saturation

Unpause and pause at a moment when E is large (B near zero).  Now drag the rₗ
slider from 0.3 outward.

- Watch the EMF value.  At what rₗ does it stop increasing?
- What determines that saturation radius?

**Expected:** EMF saturates when rₗ = R.  This is the solenoid radius.

**Why:** Once the loop is larger than the solenoid, it encloses all the flux.
Making the loop bigger adds no extra flux, so the EMF is fixed.  The saturation
radius is a direct readout of R.  This is actually how you'd measure R in an
experiment — vary the loop size and find where EMF saturates.

### Exercise 4 — ω versus B₀

Start with default settings.  Note the E arrow lengths.

Step 1: Double ω (move the ω slider from 1.0 to 2.0).  Note the arrow lengths.

Step 2: Return ω to 1.0.  Now double B₀ (move from 1.0 to 2.0).  Note the arrow lengths.

**Expected:** Both changes roughly double the arrow lengths at maximum E.

**Why:** ∂B/∂t = −B₀ω sin(ωt).  Both B₀ and ω appear as direct multiplicative
factors.  Doubling either one doubles the peak ∂B/∂t and therefore doubles E.

**The subtle difference:** Doubling ω also doubles the *frequency* of oscillation —
the arrows oscillate twice as fast.  Doubling B₀ doesn't change the speed at all.
So the two parameters affect amplitude and frequency independently.

---

## The CAS verification in the sidebar

The sidebar shows a computer algebra check of Faraday's Law for a completely different
field: a plane electromagnetic wave where E_y = cos(x − t) and B_z = cos(x − t).

The relevant component of ∇ × E = −∂B/∂t reduces to:

```
∂E_y/∂x = −∂B_z/∂t
```

The Curry symbolic algebra system computes both sides:

- ∂/∂x [cos(x − t)] = −sin(x − t)
- −∂/∂t [cos(x − t)] = −sin(x − t)

They're equal.  The residual (LHS − RHS) = 0 — meaning Faraday's Law is satisfied
**identically** for every (x, t), not just at one particular point or moment.

This is different from a numerical check (which only verifies the law at specific
values).  The CAS is doing symbolic mathematics: it proved the equality for all x and t
simultaneously.

---

## Real-world connections

**Transformer** (the most direct application)
An AC current in the primary coil creates a time-varying B.  That B induces E in the
secondary coil (via Faraday's Law), which drives a current through the secondary circuit.
The ratio of turns determines the voltage ratio: each secondary turn samples the same
EMF, so more turns = more voltage.

The simulation is essentially one cross-section of a transformer's core.  The solenoid
is the primary; anything wrapped around the outside is the secondary.

**AC generator**
A coil rotates in a steady magnetic field.  From the coil's perspective, B is
oscillating — just like in our simulation.  Faraday's Law then implies an oscillating
EMF in the coil, which is the AC voltage your outlets produce.

**Induction cooktop**
A coil under the glass carries high-frequency AC current, creating a rapidly
oscillating B.  That B induces E (and hence current) in the conductive pot sitting
on top, and the resistance of the pot converts that current to heat.  Non-conductive
cookware doesn't work because without free charges, E can't drive a current.

**Electromagnetic braking**
Drop a strong magnet through a copper tube.  The falling magnet creates a changing B
in the tube walls; Faraday's Law drives eddy currents; Lenz's law says those currents
create a B that opposes the motion (slowing the magnet).  No friction, no contact —
the magnet falls in slow motion.

---

## Common confusions

**"If B is strong, E should be strong."**
No.  Only ∂B/∂t matters.  A static field, no matter how powerful, induces no E field.
This is one of the most important and counterintuitive things Faraday's Law says.

**"The induced field is inside the solenoid."**
No.  The induced E exists *everywhere* — inside and outside.  Outside the solenoid,
B = 0, but ∂B/∂t is still non-zero (the field is still changing), and E is still
induced.  It just falls off as 1/r rather than growing linearly.

**"The loop has to be a real wire to have an EMF."**
No.  The integral form of Faraday's Law holds for any *imaginary* closed path in
space.  The EMF is the work per unit charge that E would do on a charge moved around
that path.  Whether there's an actual wire there is irrelevant to the field.

**"Lenz's law means the induced field cancels the original."**
Not quite.  Lenz's law says the induced current (if it flows) would *oppose* the change
in flux.  It opposes the *change*, not the field itself.  And in our simulation, the
induced E doesn't have a source that could respond back — we're treating B as externally
imposed.  In a real inductor, the induced current does create a back-EMF, and the
interplay between that and the original source is the physics of inductance.

---

## Quick reference

| Symbol | Meaning | Units |
|--------|---------|-------|
| B_z    | Magnetic field component (z-direction, in/out of screen) | T (Tesla) |
| ∂B/∂t  | Rate of change of B | T/s |
| E_φ    | Azimuthal electric field (tangential to circles) | V/m |
| r      | Distance from the axis | m (world units) |
| R      | Solenoid radius | m (world units) |
| EMF    | Electromotive force around the loop (∮ E · dl) | V (Volts) |
| Φ      | Magnetic flux through the loop (∫∫ B · dA) | Wb (Webers) |
| ω      | Angular frequency of B oscillation | rad/s |

**Key formulas:**

```
B_z(t)       =  B₀ cos(ωt)
∂B_z/∂t      =  −B₀ ω sin(ωt)

E_φ (r < R)  =  −(r/2)   × ∂B/∂t       [inside solenoid]
E_φ (r ≥ R)  =  −(R²/2r) × ∂B/∂t       [outside solenoid]

EMF          =  −π r_eff² × ∂B/∂t       where r_eff = min(r_loop, R)
```
