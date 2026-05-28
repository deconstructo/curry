# What It Means to Walk Through a 4D Maze

*Draft — 2026-05-25*

A design sketch for where `examples/maze4d.scm` is going, and why getting there properly should be nauseating.

---

## What the player is right now

The player is a 3D creature running around on a fixed **3D hyperplane** slicing through a 4D space. The maze has cells indexed by (x, z, w). Currently w is an integer and fixed. Q/E teleports the player to an adjacent hyperplane — like a Flatlander jumping from the z=0 page to the z=1 page. The player never *moves through* the 4th dimension; they snap between layers.

The 3D experience of the current slice is completely consistent and feels like a normal maze. That's the problem — it's too tame.

---

## The Flatland parallel

In Abbott's *Flatland*, Square lives in 2D. He can only perceive 1D cross-sections of whatever intersects his plane. When Sphere passes through, Square sees:

> A point → a growing circle → shrinking circle → gone.

Square has no concept of "above" or "below." He cannot perceive the sphere as a sphere — only as a circle that does impossible things: appears from nothing, changes size without cause, vanishes.

**The player is Square.** They live in 3D. When a 4D object intersects their hyperplane they see a **3D cross-section** that appears, morphs, and vanishes. A 4D sphere (a *glome*) looks like a sphere that swells up from a point and shrinks back to nothing as it passes through. A tesseract sliced at various angles shows a cube, then a cuboctahedron, then a rhombic dodecahedron, then a cube again — the same object, rotating in a dimension you can't look at.

---

## What "arbitrary hyperplane" means

Currently the slice has a fixed orientation: normal vector straight along w. The hyperplane is {w = constant}.

Tilting it — rotating the normal vector to mix w and x — makes the hyperplane `{αx + βw = constant}`. Then:

- A corridor running in the +w direction **appears in the player's world as a corridor running in the +x direction**, partially.
- A wall that was solid may become **transparent** (the passage in w cuts through it at the new angle).
- Two rooms with no connection in the w=0 slice may suddenly be connected in the tilted slice.

As the xw angle changes, the maze **continuously morphs**. Corridors grow out of walls, rooms merge and split, dead ends sprout exits. The player's spatial memory of "I was just in that room" becomes meaningless because the room's shape has changed.

---

## Why it's nauseating

The brain has some 400 million years of machinery for 3D spatial prediction. It constantly builds a model: *if I walk left, I'll be in that corridor; if I turn around, I'll see what I just passed.* This machinery runs below conscious thought.

In a 4D maze with a tilted hyperplane:

- **Corridors don't go where they appear to go.** Walking "forward" while tilted in xw means also drifting in w, so you arrive in a room you've never seen, from a direction that makes no geometric sense.
- **Objects appear and disappear.** Things not in the current hyperplane don't exist — then they do, as the w-position drifts through them.
- **Spatial loops that shouldn't exist.** Walking in an apparent straight line can return you to your starting point — not because the space is curved, but because the slice intersected the same 4D cell twice from different directions.
- **Landmarks are untrustworthy.** Turn around, walk back the way you came, and the room you left may look different because w drifted slightly during transit.

The vestibular system says "I moved forward 5 metres." The visual system says "I'm somewhere I've never been." There's no sensory conflict in 3D that reliably produces this — 4D is one of the few ways to achieve it systematically.

Key insight from Flatland: Abbott's Square doesn't find Sphere's visit beautiful or enlightening at first — he finds it **threatening and wrong**. His geometric intuitions are violated at a fundamental level. That's the target.

---

## Development phases

### M1 — Arbitrary hyperplane *(next)*

Add two new rotation angles — `pitch-xw` (mixing x and w) and `pitch-zw` (mixing z and w) — controlled by new keybindings or extended mouse-look. Player position becomes a continuous 4D point (px, py, pz, pw) with pw as a float. The raycaster must cast rays in the tilted hyperplane and intersect 4D cell boundaries.

*Effect:* corridors from adjacent w-slices bleed into view as you rotate. Rooms mutate while you watch.

### M2 — Continuous w movement

Remove Q/E discrete jump. Walking forward while tilted in xw causes a continuous drift in w. The player can "swim" into the 4th dimension without consciously choosing to.

*Effect:* spatial memory stops working. A room you just left looks different on the way back.

### M3 — 4D walls are 3D objects

In the current maze, walls are 2D planes. In true 4D, walls are 3D hypersurfaces. When the slice tilts, a wall face that was flat becomes a volumetric solid with apparent depth — a 2D flat wall seen at an angle in 4D appears as a parallelogram, then a rhombus, then a line as you rotate through 90°. This is the Flatland moment: Square watching a Sphere's cross-section grow and shrink.

### M4 — 4D objects in the maze

Place 4D objects (glomes, tesseracts, 4D tori) in the maze. A glome appears as a sphere that grows from a point, reaches maximum radius, and shrinks back as the player walks through it. A tesseract at various slice angles shows cubes, cuboctahedra, rhombic dodecahedra. Objects do things objects shouldn't do.

### M5 — Full 4D navigation *(goal)*

All of the above simultaneously. No landmark is stable. Rooms have windows into themselves from impossible angles. Corridors from three different w-slices converge in a single room. The geometry of the space visited two minutes ago is unrecoverable because the hyperplane has moved.

If the player isn't vomiting, either they've genuinely acclimatised to 4D — which is its own extraordinary achievement — or the implementation isn't trying hard enough.

---

## Mathematical notes

A 3D hyperplane in 4D space is defined by a point and a **single normal vector** (in 4D, one normal = codimension 1). The current slice has normal (0,0,0,1). 4D rotations have **6 degrees of freedom** (planes: xy, xz, xw, yz, yw, zw) versus 3D's 3. The player already uses the xz plane (yaw). New planes of interest:

- **xw** — tilts the slice so "east" and "w-positive" blur into each other
- **zw** — tilts so "north" and "w-positive" blur

A full 4D orientation is a 4×4 rotation matrix (or equivalently a rotor in Cl(4,0,0), which Curry's multivector module handles natively). The raycaster needs to transform ray directions through this matrix before intersecting grid boundaries.

The corridor-intersection problem when the slice is tilted: a 4D grid cell boundary is a 3D hyperplane in 4D space. The intersection of the player's tilted 3D hyperplane with such a cell boundary is a 2D plane (in general position) — which projects into the player's local 3D coordinate system as an ordinary 2D wall face. The raycaster still fires 2D rays (per screen column), but the world-space ray direction must be expressed in 4D and intersection tested against 4D cell geometry.

The implementation can remain a raycaster (column-based); the additional complexity lives entirely in the ray→world transform and the cell-boundary intersection test.
