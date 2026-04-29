# Akkadian Language Reference

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

### Actors

| English | Transliteration | Cuneiform | Notes |
|---------|-----------------|-----------|-------|
| `spawn` | `wālādum` | `𒅁𒀀` | IB.A — to beget |
| `send!` | `šapārum` | `𒌝𒂗` | UM.EN — send a tablet |
| `receive` | `maḫārum` | `𒌝𒈠` | UM.MA — receive a tablet |
| `self` | `ramānī` | `𒍪` | ZA — myself |

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

---

## Font support

To render cuneiform in a terminal or editor, install a font with Unicode Cuneiform coverage:

```bash
# Debian/Ubuntu
sudo apt install fonts-noto

# The Noto Sans Cuneiform font covers U+12000–U+1247F
```

On macOS, **Symbola** or **Noto Sans Cuneiform** work. Most modern terminals (kitty, iTerm2, WezTerm) render supplementary plane characters correctly.
