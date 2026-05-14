# Akkadian Language Reference

*v0.7.6.1 — 2026-05-14*

Curry Scheme accepts three equivalent notations for every name. You can mix them freely within a single program.

| Notation | Example | Notes |
|----------|---------|-------|
| English | `(define x 42)` | Standard R7RS |
| Transliterated Akkadian | `(šakānum x 42)` | Standard Babylonian, Assyriological transliteration |
| Cuneiform | `(𒁹 x 42)` | Unicode Cuneiform block (U+12000–U+1247F) |

The cuneiform signs render in any font that includes the Unicode Cuneiform block (Noto Sans Cuneiform, Symbola, etc.).

---

## Special forms

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `define` | `šakānum` | `𒁹` | DIŠ — single vertical wedge, "to mark" |
| `lambda` | `epēšum` | `𒇽` | LU2 — person/agent |
| `if` | `šumma` | `𒋗𒈠` | ŠU.MA — phonetic; *šumma* is the actual Akkadian conditional! |
| `begin` | `ištartu` | `𒀸` | AŠ2 — "one/first" |
| `set!` | `šanûm` | `𒁀𒀀` | BA.A — to change |
| `let` | `leqûm` | `𒅁` | IB — to take/hold |
| `let*` | `leqûm-watrum` | `𒅁𒌋` | IB.U |
| `letrec` | `leqûm-tadārum` | `𒅁𒄀` | IB.GI |
| `quote` | `kīma` | `𒆠𒈠` | KI.MA — "as it is" |
| `quasiquote` | `kīma-libbi` | `𒆠𒈠𒅁` | |
| `unquote` | `pašārum` | `𒉡𒆠` | NU.KI |
| `unquote-splicing` | `pašārum-šapārum` | `𒉡𒆠𒊕` | |
| `and` | `u` | `𒌋` | U — *u* is the actual Akkadian conjunction "and"! |
| `or` | `lū` | `𒇻` | LU |
| `not` (special) | — | — | Use the procedure `lā` / `𒉡` |
| `cond` | `šumma-ribûm` | `𒋗` | ŠU — hand/choice |
| `case` | `ana` | `𒀀𒈾` | A.NA — "for/according to" |
| `when` | `inūma` | `𒌑` | UD — *inūma* is the actual Akkadian "when"! |
| `unless` | `lā-inūma` | `𒉡𒌑` | NU.UD — "not-when" |
| `do` | `alākum` | `𒄿` | I — to go/proceed |
| `define-syntax` | `šakānum-ṭupšarrim` | `𒁹𒌝` | DIŠ.UM — define-tablet |
| `syntax-rules` | `ṭupšarrūtum` | `𒌝𒌋` | UM.U |
| `define-values` | `šakānum-nikkassī` | `𒁹𒈷` | DIŠ.ME |
| `define-record-type` | `šakānum-ṣimtim` | `𒁹𒋻` | DIŠ.TAR |
| `values` | `nikkassū` | `𒈷` | ME — essence/values |
| `call/cc` | `riksum` | `𒇲𒁹` | LAL.DIŠ — the binding/bond (do not confuse with `-` = LAL.UD) |
| `import` | `erēbum` | `𒂗` | EN — to enter |
| `export` | `waṣûm` | `𒂗𒉡` | EN.NU — to exit |
| `define-library` | `bīt-ṭuppi` | `𒂍𒌝` | E2.UM — **house of tablets**! |
| `guard` | `naṣārum` | `𒆠𒂗` | KI.EN |
| `parameterize` | `šīmtum` | `𒁹𒆠` | |
| `delay` | `naṭālum-arkûm` | `𒌑𒀸` | UD.AŠ2 — deferred time |
| `spawn` | `wālādum` | `𒅁𒀀` | IB.A — to beget |
| `send!` | `šapārum` | `𒌝` | UM — *šapārum* is the standard Akkadian verb for sending a letter/tablet! |
| `receive` | `maḫārum` | `𒈠𒄭` | MA.ḪI — *maḫārum* is the standard Akkadian verb for receiving! |

---

## Core procedures

### Pairs and lists

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `cons` | `rakāsum` | `𒇲` | LAL — to bind together |
| `car` | `rēšum` | `𒊕` | SAG — *head* in Akkadian! |
| `cdr` | `zibbatum` | `𒆜` | KUN — *tail* in Akkadian! |
| `list` | `nindabûm` | `𒄿𒌝` | I.UM — proceeding tablets |
| `length` | `mīnum` | `𒈠𒈾` | MA.NA — weight/measure |
| `append` | `redûm` | `𒈠𒂗` | to follow/continue |
| `reverse` | `turrum` | `𒋻𒀀` | TAR.A — to turn back |
| `map` | `šutakūlum-nindabî` | `𒈷𒌝` | ME.UM |
| `for-each` | `ana-kālāma` | `𒀀𒈾𒆠` | A.NA.KI — for all |
| `filter` | `ṣêrum` | `𒋻` | TAR — to cut/select |
| `null?` | `šūnum?` | `𒉡𒁹` | NU.DIŠ — not-one = empty |
| `pair?` | `qitnūm?` | `𒇲𒇲` | LAL.LAL |
| `assoc` | `ṭuppum-maḫārum` | `𒌝𒈠` | |
| `member` | `libbum-maḫārum` | `𒌝𒊕𒊕` | |

### Arithmetic

These terms are attested in Old Babylonian mathematical tablets.

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `+` | `matāḫum` | `𒋻𒁹` | TAR.DIŠ — addition |
| `-` | `ḫarāṣum` | `𒇲𒌑` | LAL.UD — to reduce/cut |
| `*` | `šutakūlum` | `𒈧𒁹` | MAŠ.DIŠ — *šutakūlum* is the actual OB term for multiplication |
| `/` | `zâzum` | `𒈧` | MAŠ — *zâzum* is the actual OB term for division |
| `=` | `mitḫārum` | `𒈠𒋻` | MA.TAR — *mitḫāris* "equally" appears throughout Babylonian algebra |
| `<` | `ṣeḫērum` | `𒉡𒃲` | NU.GAL — not-great |
| `>` | `rabûm` | `𒃲` | GAL — great |
| `<=` | `ṣeḫērum-mitḫārum` | `𒉡𒃲𒁹` | |
| `>=` | `rabûm-mitḫārum` | `𒃲𒁹` | |
| `max` | `ašarēdum` | `𒃲𒃲` | GAL.GAL — greatest |
| `min` | `ṣiḫrum` | `𒉡𒉡` | NU.NU — smallest |
| `abs` | `kīttum` | `𒆠𒀸` | KI.AŠ2 |
| `zero?` | `ṣifrum?` | `𒉡𒉡𒁹` | *ṣifrum* = zero, the source of the word "cipher" |
| `positive?` | `damqum?` | `𒃲𒁹` | good/positive |
| `negative?` | `lemnûm?` | `𒉡𒁹` | evil/negative |
| `floor` | `šaplûm` | `𒆠` | KI — earth/ground = floor! |
| `ceiling` | `elûm` | `𒀭𒀸` | AN.AŠ2 — sky = ceiling! |
| `sqrt` | `ibum` | `𒅁𒁹` | IB.DIŠ — *ibum* ("the side") is the Babylonian term for square root |
| `expt` | `napḫarum` | `𒈷𒈷` | ME.ME — power |
| `gcd` | `kabrum` | `𒃲𒁹𒁹` | |
| `lcm` | `qallum` | `𒉡𒃲𒁹` | |
| `exact` | `kinattu` | `𒆠𒋻` | |
| `inexact` | `lā-kinattu` | `𒉡𒆠𒋻` | |

### I/O

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `display` | `naṭālum` | `𒅆` | IGI — *eye*, to look/show |
| `write` | `šaṭārum` | `𒌝𒁹` | UM.DIŠ — *šaṭārum* = to write on a clay tablet |
| `newline` | `pirištu` | `𒁹𒁹𒁹` | DIŠ.DIŠ.DIŠ |
| `read` | `šemûm` | `𒅆𒀸` | IGI.AŠ2 — to look-read |
| `read-line` | `šemûm-ašrum` | `𒅆𒌋` | |
| `open-input-file` | `petûm-ṭuppi-erēbim` | `𒂍𒂗` | E2.EN — open tablet-house enter |
| `open-output-file` | `petûm-ṭuppi-waṣîm` | `𒂍𒉡` | E2.NU |

### Booleans and logic

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `not` | `lā` | `𒉡` | NU — the Sumerian/Akkadian negation particle |
| `#t` | — | `𒌋𒉡` | U.NU — truth |
| `#f` | — | `𒉡` | NU — falsehood (same sign as `not`) |
| `'()` | — | `𒊭` | ŠA3 — the empty interior |
| `0` | `ṣifrum` | — | zero, source of "cipher" |
| `pi` / `π` | — | `𒄿𒀭` | I.AN — going skyward |

### Actors

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `spawn` | `wālādum` | `𒅁𒀀` | IB.A — to beget |
| `send!` | `šapārum` | `𒌝𒂗` | UM.EN — send a tablet |
| `receive` | `maḫārum` | `𒌝𒈠` | UM.MA — receive a tablet |
| `self` | `ramānī` | `𒍪` | ZA — myself |

---

## Symbolic CAS

Babylonian scribes posed algebra problems as *"a thing I do not know; find it"* and
computed areas of fields exactly as we compute integrals.  The CAS vocabulary is
drawn directly from attested Old Babylonian mathematical tablet terminology.

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `sym-var` | `la-idûm` | `𒉡𒅆` | NU.IGI — "the not-known/not-seen"; the unknown quantity in O.B. algebraic problems |
| `sym-var?` | `la-idûm?` | `𒉡𒅆?` | |
| `sym-expr?` | `awât-la-idûm?` | `𒉡𒌝?` | NU.UM? — "unresolved utterance" |
| `symbolic?` | `la-idûm-šalim?` | `𒉡𒅆𒁹?` | either kind |
| `sym-var-name` | `šum-la-idûm` | `𒉡𒊕` | NU.SAG — "head of the unknown" = its name |
| `sym-diff` / `∂` | `māḫirum` | `𒄭𒊕` | ḪI.SAG — "the going rate"; attested on O.B. commercial tablets for the price of silver per unit; here: the instantaneous rate of change |
| `integrate` / `∫` | `eqlum` | `𒀭𒆠` | AN.KI — "field/area"; the canonical O.B. word for a measured field; mathematical tablets computed areas exactly as integrals |
| `frac-diff` | `māḫirum-ḫepûm` | `𒄭𒈠` | ḪI.MA — "halved rate" |
| `frac-int` | `eqlum-ḫepûm` | `𒀭𒆠𒈠` | AN.KI.MA — "halved field" |
| `wirtinger-d` | `māḫirum-išārum` | `𒄭𒁹` | ḪI.DIŠ — "rate-one"; the holomorphic ∂/∂z |
| `wirtinger-dbar` | `māḫirum-la` | `𒄭𒉡` | ḪI.NU — "rate-not"; the anti-holomorphic ∂/∂z̄ |
| `simplify` | `šuklulum` | `𒁹𒆠𒁹` | DIŠ.KI.DIŠ — "to bring to completion, to make whole" |
| `substitute` | `nukkurum` | `𒁀𒋻` | BA.TAR — "to alter, to exchange one thing for another" |
| `expand` | `rapāšum` | `𒃲𒀀` | GAL.A — "to broaden, to spread out" |
| `degree` | `elûm-ṣīrum` | `𒀭𒈷` | AN.ME — "the highest ascent"; the topmost exponent |
| `collect` | `kânum` | `𒆠𒁹𒁹` | KI.DIŠ.DIŠ — "to be firm, to establish"; gather like terms |
| `leading-coeff` | `rēšum-nikkassī` | `𒊕𒈷` | SAG.ME — "head of accounts"; the leading coefficient is the chief term |
| `conjugate` / `conj` | `tawirtum` | `𒅆𒋻` | IGI.TAR — "image, reflection, likeness"; the mirror across the real axis |
| `auto-diff` | `māḫirum-ramāni` | `𒄭𒍪` | ḪI.ZA — "rate-self"; forward-mode differentiation via dual surreals |
| `sym->string` | `ṭuppi-la-idûm` | `𒉡𒅆𒌝` | NU.IGI.UM — "unknown as tablet" |
| `sym->infix` | `ṭuppi-la-idûm-išārum` | `𒉡𒅆𒌝𒌋` | NU.IGI.UM.U |
| `sym->latex` | `ṭuppi-ṣīrum-la-idûm` | `𒉡𒅆𒌝𒁹` | NU.IGI.UM.DIŠ — "formal tablet of the unknown" |

---

## Surreal numbers

The surreal numbers extend the real line into the transfinite (ω) and the
infinitesimal (ε = 1/ω).  The Akkadian vocabulary draws on *dāriš* — "forever,
for eternity" — a word appearing in royal inscriptions in the phrase *ana dāriš*
("for ever and ever").

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `surreal?` | `ša-dāriš?` | `𒀭𒁹?` | AN.DIŠ? — "is it eternal?" |
| `surreal-infinite?` | `dāriš?` | `𒀭𒀭?` | AN.AN? — "doubly eternal" |
| `surreal-finite?` | `la-dāriš?` | `𒉡𒀭?` | NU.AN? — "not eternal" |
| `surreal-infinitesimal?` | `ṣiḫrum-ṣīrum?` | `𒉡𒉡𒀀?` | NU.NU.A? — "supremely tiny" |
| `surreal-real-part` | `ṣīrum-ša-dāriš` | `𒀭𒄿` | AN.I — the standard going part |
| `surreal-omega-part` | `ša-dāriš-kīnum` | `𒀭𒀭𒁹` | AN.AN.DIŠ — the ω-coefficient |
| `surreal-epsilon-part` | `ša-ṣiḫrim` | `𒉡𒉡𒈷` | NU.NU.ME — the ε-essence |
| `surreal-birthday` | `ūm-wulludim` | `𒌑𒅁` | UD.IB — "day-hold" = birth-day |
| `surreal-nterms` | `mīnum-ša-dāriš` | `𒀭𒈠` | AN.MA — count of the eternal |
| `surreal->number` | `ša-dāriš-ana-nikkassim` | `𒀭𒌑` | AN.UD — "eternal to temporal" |
| `make-surreal` | `epēšum-ša-dāriš` | `𒀭𒇽` | AN.LU2 — "make the eternal" |
| `surreal-terms` | `nindabûm-ša-dāriš` | `𒀭𒌝` | AN.UM — "the eternal's tablets" |

### Surreal constants

| English | Transliteration | Cuneiform | Value |
|---------|-----------------|-----------|-------|
| `omega` | `dāriš` | `𒀭𒀭` | ω — the first infinite surreal |
| `epsilon` | `ṣiḫrum-ṣīrum` | `𒉡𒉡𒉡` | ε = 1/ω — the first infinitesimal surreal |

---

## Quantum superposition

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `superpose` | `kalāma` | `𒊕𒊕𒊕` | SAG.SAG.SAG — "all things at once"; a quantum state holds all branches simultaneously |
| `quantum-uniform` | `kalāma-mitḫārum` | `𒊕𒊕𒁹` | SAG.SAG.DIŠ — "all-equal heads" |
| `observe` | `amārum` | `𒅆𒄿` | IGI.I — "to see, to look upon"; observation collapses the superposition |
| `quantum?` | `kalāma?` | `𒊕𒊕?` | SAG.SAG? |
| `quantum-states` | `kalāma-nindabûm` | `𒊕𒊕𒌝` | SAG.SAG.UM — all-states-as-tablet |
| `quantum-n` | `mīnum-kalāma` | `𒊕𒊕𒄿` | SAG.SAG.I — count-of-all |

---

## Multivectors / Clifford algebra

*kibrātum* (𒆠𒃲) — "the four quarters of the world" — is the Babylonian name for
the totality of three-dimensional space.  The royal title *šar kibrāt arba'im*
("king of the four quarters") claimed dominion over all of it.  A multivector lives
in the full Clifford algebra Cl(p,q,r) over that space.

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `make-mv` | `epēšum-kibrātim` | `𒆠𒃲𒇽` | KI.GAL.LU2 — "make the great space" |
| `mv?` | `kibrātim?` | `𒆠𒃲?` | KI.GAL? — "is it a great space?" |
| `mv-signature` | `ṣimdat-kibrātim` | `𒆠𒃲𒋻` | KI.GAL.TAR — space-signature |
| `mv-ref` | `maḫārum-kibrātim` | `𒆠𒃲𒊕` | KI.GAL.SAG — space-head |
| `mv-set!` | `šakānum-kibrātim` | `𒆠𒃲𒁹` | KI.GAL.DIŠ — place-in-space |
| `mv+` | `matāḫum-kibrātim` | `𒆠𒃲𒋻𒁹` | space-add |
| `mv-` | `ḫarāṣum-kibrātim` | `𒆠𒃲𒇲𒌑` | space-subtract |
| `mv*` | `šutakūlum-kibrātim` | `𒆠𒃲𒈧` | KI.GAL.MAŠ — geometric product |
| `mv-scale` | `zâzum-kibrātim` | `𒆠𒃲𒈧𒁹` | space-scale |
| `mv-wedge` | `ṣilippum-kibrātim` | `𒆠𒃲𒌋` | KI.GAL.U — "the diagonal"; outer/wedge product |
| `mv-lcontract` | `ṣibûm-kibrātim` | `𒆠𒃲𒅁` | KI.GAL.IB — left contraction |
| `mv-reverse` | `turrum-kibrātim` | `𒆠𒃲𒄀𒁹` | KI.GAL.GI.DIŠ — reverse |
| `mv-involute` | `nakārum-kibrātim` | `𒆠𒃲𒉡𒄿` | KI.GAL.NU.I — grade involution |
| `mv-conjugate` | `mitḫurtum-kibrātim` | `𒆠𒃲𒈠𒋻` | KI.GAL.MA.TAR — Clifford conjugate |
| `mv-dual` | `šanûm-kibrātim` | `𒆠𒃲𒁀𒀀` | KI.GAL.BA.A — "the other of the space" |
| `mv-grade` | `šinīpat-kibrātim` | `𒆠𒃲𒀸` | KI.GAL.AŠ2 — grade projection |
| `mv-scalar` | `ṣifrum-kibrātim` | `𒆠𒃲𒉡𒉡` | KI.GAL.NU.NU — grade-0 = scalar part |
| `mv-norm2` | `napḫarum-kibrātim` | `𒆠𒃲𒈷𒈷` | KI.GAL.ME.ME — squared norm |
| `mv-norm` | `ibum-kibrātim` | `𒆠𒃲𒅁𒁹` | KI.GAL.IB.DIŠ — "side of the space" (*ibum* = square root) |
| `mv-normalize` | `ibum-ṣīrum-kibrātim` | `𒆠𒃲𒅁𒌑` | KI.GAL.IB.UD — "supreme side" |
| `mv-e` | `pānum-kibrātim` | `𒆠𒃲𒅆` | KI.GAL.IGI — "face/eye of the space" = basis blade |
| `mv-from-list` | `kibrātim-maḫārum` | `𒆠𒃲𒌝` | KI.GAL.UM — "space from tablet" |
| `quaternion->mv` | `rebûm-ana-kibrātim` | `𒆠𒃲𒂗` | KI.GAL.EN — enter the great space |
| `mv->quaternion` | `kibrātum-ana-rebîm` | `𒆠𒃲𒂗𒉡` | KI.GAL.EN.NU — exit space to fourfold |

---

## Quaternions and Octonions

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `make-quaternion` | `epēšum-rebûm` | `𒅁𒈷` | IB.ME — "make fourfold"; *rebûm* = fourth/fourfold in Akkadian |
| `quaternion?` | `rebûm?` | `𒅁𒈷?` | IB.ME? |
| `make-octonion` | `epēšum-samānûm` | `𒅁𒈷𒈷` | IB.ME.ME — "make eightfold"; *samānûm* = eighth/eightfold |
| `octonion?` | `samānûm?` | `𒅁𒈷𒈷?` | IB.ME.ME? |
| `octonion-ref` | `maḫārum-samānûm` | `𒅁𒈷𒊕` | IB.ME.SAG — eightfold-head |

---

## Example programs

### English
```scheme
(define (factorial n)
  (if (= n 0)
      1
      (* n (factorial (- n 1)))))
(display (factorial 10))
(newline)
```

### Transliterated Akkadian
```scheme
(šakānum (napḫarum-nikkassim n)
  (šumma (mitḫārum n ṣifrum)
    𒀸
    (šutakūlum n (napḫarum-nikkassim (ḫarāṣum n 𒀸)))))
(naṭālum (napḫarum-nikkassim 10))
(pirištu)
```

### Cuneiform
```scheme
(𒁹 (napḫarum-nikkassim n)
  (𒋗𒈠 (𒈠𒋻 n ṣifrum)
    𒀸
    (𒈧𒁹 n (napḫarum-nikkassim (𒇲𒌑 n 𒀸)))))
(𒅆 (napḫarum-nikkassim 10))
(𒁹𒁹𒁹)
```

### Mixed (perfectly valid)
```scheme
(𒁹 (factorial n)
  (šumma (= n 0)
    1
    (* n (factorial (- n 1)))))
(display (factorial 10))
(𒁹𒁹𒁹)
```

### Symbolic CAS in Akkadian

Differentiating a polynomial, integrating, simplifying — in the language of the scribes.

```scheme
; Bind unknowns
(symbolic x)

; ∂(x³ + 2x)/∂x  →  3x² + 2
(naṭālum (māḫirum (matāḫum (napḫarum x 3) (šutakūlum 2 x)) x))
(pirištu)

; ∫ x² dx  →  x³/3
(naṭālum (eqlum (napḫarum x 2) x))
(pirištu)

; Simplify (x + 0)
(naṭālum (šuklulum (matāḫum x ṣifrum)))
(pirištu)

; Substitute x = 5 in (x² + 1)
(naṭālum (nukkurum (matāḫum (napḫarum x 2) 𒀸) x 5))
(pirištu)
```

### Surreal arithmetic

```scheme
; ω and ε are bound by default
(naṭālum dāriš)          ; ω
(pirištu)
(naṭālum 𒉡𒉡𒉡)          ; ε
(pirištu)
(naṭālum (matāḫum dāriš 𒀸))   ; ω + 1
(pirištu)
```

---

## Notes on the signs

- **𒌋 (U)** as `and` is exact: *u* is the standard Akkadian copulative conjunction.
- **𒈷 (ME)** as `values` refers to the Sumerian concept of *me* — the divine laws or essences that govern all things. Appropriate.
- **𒂍𒌝 (E2.UM)** as `define-library` means "house of tablets" (*bīt ṭuppi*) — the actual Akkadian word for a scribal library or archive.
- **𒌝 (UM)** as `send!` is because *šapārum* (to send) is the standard opening verb of Akkadian letters, which were written on clay tablets (*ṭuppu*). You are sending a tablet.
- **𒊕 (SAG)** and **𒆜 (KUN)** for `car`/`cdr` are exact: SAG means *head* and KUN means *tail* in Akkadian.
- **𒅁𒁹 (IB.DIŠ)** for `sqrt`: *ibum* ("the side") is how Babylonian mathematicians referred to the square root — the side of a square of given area.
- **𒈧 (MAŠ)** for `/`: *zâzum* (to divide) and its related forms appear in Old Babylonian mathematical tablets in division problems.
- **ṣifrum** for zero is historically significant: the Akkadian word *ṣifrum* (emptiness/void) is the source of the Arabic *ṣifr*, thence the Latin *zephirum*, and ultimately the English words "zero" and "cipher".
- **māḫirum** (𒄭𒊕) for the derivative: the *māḫirum* price appears on hundreds of O.B. tablets — "1 shekel of silver buys X litres of barley at today's *māḫirum*." The derivative is the *māḫirum* of a function: how much the output changes per unit of input, at this moment.
- **eqlum** (𒀭𒆠) for the integral: the word appears in virtually every Babylonian mathematical tablet involving area — *eqlam aṣbat* "I seized the field." The computation of areas under geometric figures is precisely integration.
- **la-idûm** (𒉡𒅆) for a symbolic variable: the O.B. problem texts phrase their unknowns as *ša lā idûm* — "that which I do not know." Every algebraic tablet is a search for *la-idûm*.
- **kibrātum** (𒆠𒃲) for multivectors: the royal epithet *šar kibrāt arba'im* — "king of the four quarters" — meant dominion over all three-dimensional space. A Clifford multivector element is precisely an element of that full geometric space.
- **dāriš** (𒀭𒀭) for ω: the adverb *dāriš* means "forever, for eternity." Surreal ω is the simplest number greater than all naturals — the first eternity past the finite.
- **NU.NU.NU** (𒉡𒉡𒉡) for ε: three negations — not, not, not — for a number that is not zero, not negative, and not anything you can reach from zero by finite steps. The infinitesimal that defies all ordinary measure.

---

## Font support

To render cuneiform in a terminal or editor, install a font with Unicode Cuneiform coverage:

```bash
# Debian/Ubuntu
sudo apt install fonts-noto

# The Noto Sans Cuneiform font covers U+12000–U+1247F
```
