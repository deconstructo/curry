# Philosophy

Most languages pick one truth domain, one numeric tower, one concurrency
model, one way of being right, and then spend the rest of their lives
defending the choice. Curry doesn't make that trade. Where a mainstream
language ships a load-bearing assumption, Curry tries to ship a parameter.

This isn't neutrality. Refusing to accept "there is one true logic" or
"there is one true way to represent a number" or "there is one true way
to run concurrent code" is a position, not an absence of one — and it's
the position this project actually takes:

- **There is no one true logic.** Classical two-valued logic is the
  default because it's convenient, not because it's correct. The moment
  your data contradicts itself — multiple sources, partial proof,
  degrees of belief — classical logic's principle of explosion turns a
  local disagreement into total collapse. Curry treats logics as
  first-class, swappable values instead of a hardwired assumption:
  Belnap's four-valued paraconsistent logic sits next to intuitionistic,
  fuzzy, and probabilistic logics, and you can write your own. See
  `docs/thoughts/anarchists-cookbook-logic.md` and `(curry logic)`.
- **There is no one true number.** The numeric tower doesn't stop at
  floats. It runs through bignums, rationals, complex, quaternions,
  octonions, Clifford multivectors, surreal (Hahn-series) numbers, and
  symbolic expressions — because "a number" turns out to be a much
  larger idea than IEEE 754 admits, and picking one representation as
  canonical is itself a choice that forecloses the others.
- **There is no one true concurrency model.** Actors (real OS threads),
  STM, and CSP channels coexist rather than one being crowned the
  "right" way to write concurrent code.
- **There is no one true CAS.** `define-rule`/`define-algebra` let you
  declare a new algebra — tropical, Weyl, q-deformed, Grassmann, GF(p),
  whatever — at the Scheme level and get simplification, differentiation,
  and integration for it without touching C. The computer algebra system
  isn't a fixed set of rules the implementers decided were sufficient;
  it's a substrate you can extend on your own terms.
- **Even error messages refuse the default.** Every runtime error carries
  a Standard Babylonian Akkadian preamble in cuneiform, and every special
  form has Akkadian/cuneiform synonyms that `eval` translates
  transparently — because "error messages are in English" and "code is
  written in ASCII keywords" are conventions, not laws.

None of this is decoration. Each of these is a place where a mainstream
language made a load-bearing decision on your behalf and stopped asking
questions — and where Curry instead asks "why this one, and what would it
cost to let you pick." The `docs/thoughts/anarchists-cookbook*.md` series
exists to explore that idea further: not a tutorial on features that
exist, but provocations about what becomes possible once an assumption
you'd stopped noticing is made optional.

This is also why the project doesn't treat "done" as a virtue in itself.
A language that has settled every question has stopped being able to
surprise the person using it. Curry would rather stay slightly
unsettled — pluggable, contestable, rewritable at the level most
languages consider bedrock — than be finished.
