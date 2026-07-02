# Language Packs

Curry Scheme ships with five language packs. Akkadian is built into the runtime
and always available. The other four are distributed as `.scm` files in the
`langs/` directory of the repository and via the online registry.

This document describes each pack, the translation philosophy behind it, and
selected vocabulary decisions. For API reference and installation instructions,
see [`module-lang.md`](module-lang.md).

---

## Akkadian (built-in)

**Id:** `akkadian`  
**Source:** compiled into the runtime (`src/akkadian_names.h`)

The Akkadian pack is registered at startup and cannot be unloaded. It covers all
303 canonical Curry Scheme names in both transliterated Standard Babylonian
Akkadian and authentic cuneiform Unicode. The cuneiform reader handles the
Unicode tokens natively, so `𒀭 ṭupšarru` and `ṭupšarru` are both valid.

```scheme
;; Cuneiform forms work directly
(𒀭ṭupšarru (x) (* x x))    ; lambda
```

The runtime error preamble — `𒀭 ḫiṭītu —` (*great fault*) — comes from this
pack and is used as the default when no other language is active.

For the complete listing of Akkadian synonyms, see
[`akkadian-reference.md`](akkadian-reference.md).

---

## Modern Irish / Gaeilge

**Id:** `irish`  
**File:** `langs/irish.scm`  
**Intro:** *Fáilte! Tá an Ghaeilge gníomhach anois.* (Welcome! Irish is now active.)  
**Error preamble:** *Earráid mhór:* (Great error:)

### Background

Irish is one of the oldest living written languages in Europe, with a continuous
literary tradition stretching back 1,500 years. Despite this antiquity, it
remains a living community language — spoken in Gaeltacht communities on the
Atlantic coast and taught as the medium of instruction in hundreds of
Gaelscoileanna (Irish-medium schools) throughout Ireland.

This pack was made for young people in those communities: children who might find
it more natural, more familiar, or simply more joyful to write their first
programs in Irish rather than English.

### Translation philosophy

The pack aims for idiomatic Modern Irish, not word-for-word translation from
English. Where Irish has a single word that captures the exact sense of a
programming concept, that word is used. Where no direct equivalent exists,
the most natural Irish phrasing takes priority over structural similarity to
the English original.

### Selected vocabulary

**`feidhm`** → `lambda`  
*Feidhm* means function, purpose, or use — the Irish word already carries the
mathematical sense of a function before computing existed. A lambda defines what
something does; *feidhm* is what a thing is for.

**`ceann` / `eireaball`** → `car` / `cdr`  
Head and tail. Natural bodily metaphors in Irish, used for the front and back of
animals and objects. A list has a head you encounter first and a tail that
follows — this is exactly what *ceann* and *eireaball* describe.

**`beir`** → `spawn`  
*Beir* means to give birth, to carry, to bring forth. Actors are born into
existence; they are not constructed. The word carries the sense of something
coming alive, which is the right frame for a new concurrent process.

**`scag`** → `filter`  
To sieve. The Irish sieve is an ancient domestic implement — grain is poured
through it and what passes is wanted, what stays is not. Filtering a list has
exactly this character: the predicate is the mesh of the sieve.

**`taispeáin`** → `display`  
To show, to make visible, to reveal. Stronger and more vivid than "print" or
"output." *Display* in the computing sense always meant making something
perceivable; *taispeáin* insists on that meaning.

**`ionann`** → `eq?`  
Same, identical. A word that points at sameness at a deep level, not just
equivalence but identity. The distinction between `eq?` (pointer identity) and
`equal?` (structural equivalence) is exactly the distinction *ionann* captures.

**`eas`** → `exp`  
Waterfall. An intentional pun: *eas* is the Irish word for a waterfall, and
exponential growth has that character — a cascade that accelerates as it falls.
The Euler number *e* is the base of natural exponential growth; a waterfall is
an image of that.

**`comhtháthú`** → `integrate`  
Integration in both the mathematical and the social/political sense. Irish
*comhtháthú* (literally: co-fusion, bringing together) is the word used for
social integration and for mathematical integration. Both are the gathering of
many small things into a whole.

**`agus` / `nó`** → `and` / `or`  
The most natural Irish conjunctions. No translation decision needed: these are
the words every Irish speaker learns as children. A program in Irish should use
the words a child knows.

### Quick example

```scheme
(import (curry lang))
(lang:load! "irish")
(set-active-language! "irish")

(sainmhínigh cearnach
  (feidhm (n) (* n n)))

(taispeáin (mapáil cearnach '(1 2 3 4 5)))
; (1 4 9 16 25)

(sainmhínigh cóimheas?
  (feidhm (n)
    (má (náid? (iarmhar n 2)) 'cothrom 'corr)))

(taispeáin (cóimheas? 7))
; corr
```

---

## Classical Greek / Ελληνική Κλασική

**Id:** `greek`  
**File:** `langs/greek.scm`  
**Intro:** *Χαῖρε! Ἡ Ἑλληνικὴ γλῶσσα ἐνεργός ἐστι.* (Greetings! The Greek language is now active.)  
**Error preamble:** *Ἁμαρτία μεγάλη:* (Great fault:)

### Background

Ancient Greek is the language of Euclid's *Elements*, Aristotle's *Organon*,
and Archimedes' mechanical proofs. The vocabulary of modern mathematics and
computing is saturated with Greek: *algorithm*, *arithmetic* (ἀριθμός),
*analysis*, *synthesis*, *logic* (λόγος), *theory* (θεωρία), *atom* (ἄτομον).

This pack uses authentic Classical Greek forms throughout. Polytonic Unicode
(accents and breathings) is used as it would appear in a critical edition.
The Curry reader handles UTF-8 natively, so polytonic forms are valid identifiers
without any configuration.

### Translation philosophy

The goal is to reveal that computing's vocabulary is Greek in disguise. Many
of the most important ideas in Curry Scheme — types, forms, atoms, theories,
powers — come from words that already existed in Greek for related but distinct
purposes. The pack makes that lineage visible by using the original terms.

### Selected vocabulary

**`ὁρίζω`** → `define`  
I define, I determine — from *ὅρος*, boundary, horizon. To define something is
to set its horizon: to establish the boundary of what a concept includes and
excludes. Euclid's definitions are *ὁρισμοί*. The connection to horizon is
exact and not coincidental.

**`ποιεῖ`** → `lambda`  
It makes, it creates — from *ποιέω*, the source of *poetry*. A poet makes
something that did not previously exist; a lambda makes a function. Third-person
singular present, because a lambda-form *makes* its closure when evaluated.

**`δύναμις`** → `expt`  
Power, potency, capacity — used by the Pythagoreans specifically for the
mathematical power of a number. The English word *dynamic* comes from here.
When we write `(δύναμις 2 10)` we are using the exact word Pythagoras used.

**`ῥίζα`** → `sqrt`  
Root of a plant. The mathematical metaphor for square root — the root from which
the square grows — is itself Greek. When we say a number has a "root" we are
translating *ῥίζα*.

**`ῥητός`** → `rational?`  
What can be spoken, what can be expressed. Irrational numbers were *ἄρρητον* —
the unspeakable, the inexpressible. A rational number is one that can be stated
exactly as a ratio of whole numbers; the Greek word captures precisely that
property.

**`θεωρέω`** → `observe`  
To contemplate, to be a spectator, to look at with understanding — the source
of *theory*. When a physicist observes a quantum system, what they do is in a
deep sense theoretical: they bring a prepared understanding to what they see.
The collapse of superposition under observation is, in Greek, an act of
*θεωρία*.

**`ἄτομος`** / `atomically` → `atomically`  
The indivisible (ἄτομος = un-cuttable). Democritus proposed that matter was
composed of indivisible units; Curry's `atomically` ensures that a transaction
is indivisible. The connection Democritus drew between indivisibility and
physical reality is the same connection we draw between indivisibility and
transactional integrity.

**`κατάλογος`** → `list`  
Catalogue, roll-call, enumeration — the word for a list in military and civic
Greek, as in a muster-roll of soldiers or a register of citizens. A Scheme list
is an enumerated sequence: a *κατάλογος*.

**`ἁμαρτία`** → `error`  
Missing the mark. The word Aristotle used in the *Poetics* for the tragic hero's
fatal flaw — the misjudgement that brings them down. Errors in programs often
have this character: a small misjudgement with catastrophic consequences.

### Quick example

```scheme
(import (curry lang))
(lang:load! "greek")
(ὁρίζω (ἔστω ((ν 10))
  (δύναμις ν 2)))
```

---

## Classical Latin / Latina Classica

**Id:** `latin`  
**File:** `langs/latin.scm`  
**Intro:** *Salve! Lingua Latina nunc activa est.* (Greetings! The Latin language is now active.)  
**Error preamble:** *Error magnus:* (Great error:)

### Background

Latin is the direct ancestor of an extraordinary fraction of the words
programmers use every day — often without realising it. This pack operates on
a simple premise: many Curry Scheme built-in names *are already Latin words*,
or transparent Latin derivatives. Writing in Latin makes the etymology visible.

The pack uses Classical Republican and Augustan Latin forms throughout.
Latin is written without diacritics in standard orthography, so all identifiers
are ASCII — the only language pack that is.

### Translation philosophy

The theme is *Latin in disguise*. The exercise of writing in this pack is partly
etymological: discovering that `integer`, `vector`, `error`, `quantum`, and
`momentum` are already Latin. The pack extends that recognition to the parts of
Scheme that do not yet show their Latin roots.

### Selected vocabulary

**`integer`** → `integer?`  
Whole, untouched, undiminished — *integer* IS Latin. The mathematical concept
of an integer as a complete whole number comes directly from the Latin adjective.
Using `integer` as the predicate makes the identifier self-documenting.

**`vector`** → `vector?`  
Carrier, conveyor — *vector* IS Latin (from *vehere*, to carry). Curry's vector
data structure is, etymologically, a carrier of values. The word needs no
translation.

**`error`** → `error`  
Wandering, going astray — *error* IS Latin (from *errare*, to wander). A
runtime error is a program that has wandered from the correct path. The word
arrived in English from Latin without change.

**`modulus`** → `modulo`  
Small measure, unit — the Latin word that gave computing both *module* (a
self-contained unit) and *modulo* (the remainder after division by a unit).
Using *modulus* connects the arithmetic operation to its siblings.

**`sinus`** → `sin`  
The fold or bay of a robe. How trigonometry got its name is one of the more
remarkable stories in the history of mathematics: the Sanskrit geometric term
*jīvā* (bowstring) was transliterated into Arabic as *jiba*, which was later
misread as *jaib* (bay, fold of a garment), which was then translated into Latin
as *sinus*. We say "sine" because of a misreading of a transliteration of a
Sanskrit word. Using `sinus` restores the Latin that English abbreviates.

**`quantum`** → `quantum?`  
How much? (neuter singular of *quantus*) — *quantum* IS Latin. Curry's quantum
type, representing states in superposition, is named in Latin already. The
predicate `quantum?` is more transparently self-descriptive than `quantum?` in
English because it reads as a question: *how much? is it a quantum of something?*

**`momentum`** → `current-jiffy`  
Movement, importance, weight — *momentum* IS Latin (from *movere*, to move).
The present instant, the current moment, is a *momentum* — a unit of movement
through time. The mapping to `current-jiffy` brings out that the jiffy is not
just a technical measurement but a moment of motion.

**`limes`** → `limit`  
Boundary, path, the Roman frontier fortification (*limes Germanicus*) —
source of English *limit*. A mathematical limit is the boundary that a sequence
approaches without necessarily reaching. The Roman *limes* was a boundary that
defined what was inside the Empire and what was beyond it: exactly the role a
limit plays.

**`radix`** → `sqrt`  
Root — source of *radical*, *radish*. A square root is the root from which the
square grows. *Radix* also gives us "radix" in the sense of numerical base
(decimal = base-10 = *radix decem*), and the radical sign √ in mathematics.

**`facio`** → `lambda`  
I make, I do, I construct — source of *factory*, *manufacture*, *fact* (a
thing made/done). A lambda constructs a function; *facio* is the act of making.

### Quick example

```scheme
(import (curry lang))
(lang:load! "latin")
(set-active-language! "latin")

(definio (esto ((n 5))
  (* n n)))

;; The predicate reads as a question in Latin:
(integer 42)   ; => #t  ("is 42 a whole number?")
(vector '#(1 2 3))   ; => #t  ("is this a carrier?")
(error "oops")   ; Error magnus: oops
```

---

## Warlpiri

**Id:** `warlpiri`  
**File:** `langs/warlpiri.scm`  
**Intro:** *Yapa yimi Warlpiri kurlangu — ngajuju karlipa yimi!*  
(This speech belongs to Warlpiri people — together we speak it!)  
**Error preamble:** *Ngurra-kurlu karlipa yimi:* (We speak from the camp:)

### Background

Warlpiri is spoken by approximately 2,500 people in the Northern Territory of
Australia, centred on the communities of Yuendumu, Lajamanu, Willowra, and
Nyirrpi. It is an ergative-absolutive language with a rich system of spatial
reference and one of the most studied Australian languages in linguistics.

Warlpiri thought organises the world through relationships — kinship,
custodianship (*kirda* and *kurdungurlu*), and the Dreaming (*Jukurrpa*), the
creative law that underlies all pattern and existence. These concepts shape
the translation decisions below.

### A beginning, not a finished thing

This pack is an experiment and an offer. The Curry project built a system that
lets programs be written in any language. When thinking about who that was for,
Warlpiri kids at Yuendumu and Lajamanu came to mind — young people who might
find it more natural or more joyful to write code in their own language.

A start has been made. Published dictionaries and learner materials were used to
assemble the mappings. Some will be right. Some will be wrong. Some may use a
word in a context it does not belong in. This file does not know what it does
not know.

**This file belongs to Warlpiri people, not to the Curry project.**

If it is useful — take it, fix it, make it yours. If the approach is wrong, if
the words do not sit right, if there is a better way — the maintainers would
rather be corrected and do nothing than persist with something that does harm.

To contribute: file a GitHub issue, or email the maintainer. Pull requests are
particularly welcome from community members and educators.

Vocabulary was drawn from the Warlpiri–English Dictionary (Laughren, Hoogenraad,
Hale, Granites, 1996), AIATSIS Warlpiri materials, NTDE learner resources, and
Jukurrpa Media Warlpiri curriculum materials. Some entries are compounds not in
standard dictionaries, invented to fill gaps. These are marked in the source
file. Tonal and register nuance is not fully captured, and some words have
restricted or ceremonial uses that a dictionary cannot convey. Please review with
a fluent speaker before using in educational settings.

### Translation philosophy

Warlpiri thought is spatial and relational. People, concepts, and events are
understood through where they are, who they travel with, and what pattern
(*Jukurrpa*) underlies their existence. The translations attempt to use these
frames where they genuinely fit.

### Selected vocabulary

**`ngurlu`** → `list`  
A travelling group, a mob moving together through country. In Warlpiri spatial
thinking, a group is understood through its movement — it has a direction and a
path. A list is traversed; it goes somewhere. *Ngurlu* is the word for a group
that moves.

**`kurdiji`** → `filter`  
Shield. In Warlpiri culture the shield selects what passes and what does not; it
is protective by nature. A filter decides what is kept and what is turned away.
The shield image makes the semantics of filtering concrete and physical.

**`jukurrpa-yirdi`** → `sym-var`  
Dreaming-pattern. *Jukurrpa* is the creative law underlying all pattern and
existence; *yirdi* is pattern or way of doing. A symbolic variable is a named
pattern that has not yet received its value — like a Dreaming element that is
present as law and form but not yet manifest in a particular instance.

**`jukurrpa-panu`** → `superpose`  
Many Dreamings. In Warlpiri cosmology multiple realities — Dreaming-time and
present-time — coexist simultaneously. Quantum superposition, in which all
possible states are simultaneously present until observation, maps naturally to
this concept. *Jukurrpa-panu* means many Dreamings at once.

**`marlpa-nyina`** → `spawn`  
Companion-sit. *Marlpa* is companion or friend; *nyina* is to sit, to be, to
live. Spawning an actor brings a new companion into existence alongside you — it
is born and then it sits, living its own life in parallel with yours.

**`kuruwarri`** → `number->string`  
Sacred design, mark. *Kuruwarri* are the designs that encode meaning — used in
ceremony, body painting, sand drawing. Numbers are marks that encode quantity;
`kuruwarri` for `number->string` describes the act of making that mark visible.
Number predicates in the pack also use *kuruwarri* because numbers, like sacred
designs, are marks that carry meaning.

**`lawa`** → `not`  
No, none, lacking, absent. The Warlpiri word for negation is more
philosophically complete than English "not" — it points not just to logical
inversion but to the absence of something that might otherwise be there. *Lawa*
as `not` names the void rather than the operation.

**`pina`** / `manu-kari`** → `car` / `cdr`  
Ear / the rest that comes after. *Pina* (ear) is what you hear first — the head
of the mob, the first in the line. *Manu-kari* is the rest that follows. The
kinship-based Warlpiri sense of "who comes first" and "who comes after" maps
naturally to the list structure.

**`nyanyi-juku`** → `observe`  
Look carefully. *Nyanyi* is to see or hear; *-juku* is an emphatic suffix meaning
*just, right, exactly*. Observing a quantum system collapses it — you look
carefully and the possibility becomes actual.

**`ngajuju`** → `self`  
I, myself — the one speaking. An actor's reference to itself uses the first
person singular, the most natural way in any language to say "this one, the
speaker."

### The community invitation

The Warlpiri pack ends with two lines that state its orientation:

> *Ngalikirlangu — this belongs to all of us together.*  
> *Yapa yimi Warlpiri kurlangu — this speech belongs to Warlpiri people.*

The first says the project is collective; the second says that whatever value
this file has belongs to the community whose language it uses. The Curry project
holds it in trust, not in ownership.

### Quick example

```scheme
(import (curry lang))
(lang:load-file! "langs/warlpiri.scm")
(set-active-language! "warlpiri")

;; A list is a travelling group
(nyinaja mob (ngurlu 1 2 3 4 5))

;; Filter (shield) for only the big ones
(nyinaja wiri-ngurlu (kurdiji (feidhm (x) (wiri-kari x)) mob))

;; Many Dreamings: quantum superposition
(nyinaja psi (jukurrpa-panu '((0.6 . "ngurra") (0.4 . "walya"))))
(nyanyi-juku psi)   ; collapse to one

;; Spawn a companion
(marlpa-nyina (yirdi () (yimi-nyanyi)))
```

---

## See Also

- [`module-lang.md`](module-lang.md) — API reference: `register-language!`,
  `set-active-language!`, `lang:install!`, `lang:load!`, registry, thread safety
- [`akkadian-reference.md`](akkadian-reference.md) — complete Akkadian synonym listing
