# Design spec: Babylonian/Sumerian number system — v1.2.5

*Drafted 2026-06-06.*

---

## Motivation

Curry already translates Akkadian and cuneiform operator names transparently in
the evaluator. Numbers are the other half of a written language. Babylonian
mathematics is the origin of the sexagesimal system we still use for time and
angles — 60 seconds, 60 minutes, 360 degrees — and produced results like the
YBC 7289 tablet approximation of √2 to four sexagesimal places, accurate to
six decimal digits, in roughly 1800 BCE. The number system belongs in Curry.

This is platform 9¾: invisible inside a mundane runtime, entirely functional if
you know to walk through the wall.

---

## The number system

Babylonian is **base 60, fully positional** (like our base-10). Within each
sexagesimal "digit" (0–59), numbers were written additively using two
cuneiform wedge signs:

| Glyph | Unicode | Value |
|-------|---------|-------|
| 𒁹 | U+12079 CUNEIFORM SIGN ASH | 1 |
| 𒌋 | U+1230B CUNEIFORM SIGN U | 10 |

So 23 = two 𒌋 + three 𒁹 = 𒌋𒌋𒁹𒁹𒁹. For zero, early Babylonian left a
gap (context-dependent magnitude); the Seleucid period introduced 𒑊
(U+12469 CUNEIFORM NUMERIC SIGN SHAR2) as an explicit placeholder.

Fractional positions work the same way — the sexagesimal point is positional
and was originally inferred from context. Modern transcription uses a semicolon
to make it explicit.

---

## Modern representations

### 1. Neugebauer notation — the scholarly standard

Introduced by Otto Neugebauer in *Mathematische Keilschrift-Texte* (1935–37).
Used in all modern Assyriology and history-of-mathematics literature, including
Eleanor Robson's *Mathematics in Ancient Iraq*.

- **Comma** separates sexagesimal digits
- **Semicolon** is the sexagesimal point (integer ↔ fractional boundary)
- Each digit is a decimal integer 0–59

```
1,24,51,10          →  √2 (YBC 7289 tablet — integer part of 60×√2 position)
1;24,51,10          →  √2 ≈ 1.41421296...  (1 + 24/60 + 51/3600 + 10/216000)
1,0,0               →  3600
0;0,0,1             →  1/216000
45                  →  45  (single digit, no comma needed)
```

### 2. Cuneiform Unicode

Render and read using the actual cuneiform glyph sequences. Each digit 0–59 is
written as a sequence of 𒌋 (tens) followed by 𒁹 (ones), with 𒑊 for zero.
Digits within a number are separated by a space (as on tablets where adjacent
digit groups are spaced).

```
𒁹                  →  1
𒌋                  →  10
𒌋𒁹𒁹𒁹            →  13
𒁹 𒌋𒁹             →  1×60 + 11 = 71   (two sexagesimal digits)
𒑊 𒁹               →  0×60 + 1 = 1    (with explicit zero)
```

### 3. Degree-minute-second (reference)

The survival of sexagesimal in modern culture. Not a separate format to
implement — it is Neugebauer notation with different punctuation
(`°`, `′`, `″`). Document the equivalence; offer a conversion helper.

```
1°30′0″  =  1;30,0 in Neugebauer  =  1.5 in decimal
```

---

## Scheme API

### Reader extensions

```scheme
; Neugebauer literal — #s prefix
#s1,24,51,10         ; → exact rational 30547/21600  (= 1 + 24/60 + 51/3600 + 10/216000)
#s45                 ; → 45 (fixnum — single digit, no radix ambiguity)
#s1,0,0              ; → 3600

; Cuneiform literal — recognised by the presence of ASH/U/SHAR2 characters
𒁹𒌋𒌋𒁹             ; → 23  (reader detects cuneiform glyph sequence)
𒁹 𒌋𒁹              ; → 71  (space-separated sexagesimal digits)
```

Cuneiform recognition: if the first character of a token is in the Cuneiform
block (U+12000–U+123FF) or the Cuneiform Numbers block (U+12400–U+1247F), the
reader switches to cuneiform mode. ASH (𒁹) contributes 1 each, U (𒌋)
contributes 10 each, SHAR2 (𒑊) is zero, spaces delimit sexagesimal digit
groups. The result is always an exact integer or rational.

### Output

```scheme
(number->string n 'cuneiform)        ; → cuneiform glyph string
(number->string n 'neugebauer)       ; → Neugebauer comma/semicolon string
(number->string n 60)                ; → Neugebauer integer form (commas, no semicolon)
                                     ;   same as 'neugebauer for integers

(number->string 71 'cuneiform)       ; → "𒁹 𒌋𒁹"
(number->string 71 'neugebauer)      ; → "1,11"
(number->string 3/2 'neugebauer)     ; → "1;30"
(number->string 3/2 'cuneiform)      ; → "𒁹 𒌋𒌋𒌋"   (1 ; 30)

; MPFR/flonum → Neugebauer truncates to a configurable number of places
(number->string 1.41421356 'neugebauer #:places 4)  ; → "1;24,51,10"
```

### Parsing

```scheme
(string->number "1,24,51,10" 'neugebauer)   ; → 30547/21600 (rational)
(string->number "1;30" 'neugebauer)         ; → 3/2
(string->number "𒁹𒌋𒌋𒁹" 'cuneiform)      ; → 23
(string->number "𒁹 𒌋𒁹" 'cuneiform)       ; → 71
```

### Conversion helpers

```scheme
; Sexagesimal ↔ rational
(sexagesimal->rational '(1 24 51 10))       ; → 30547/21600  (integer part = first element)
(sexagesimal->rational '(1 24 51 10) #:fractional? #t)   ; same — unambiguous with keyword
(rational->sexagesimal 3/2 #:places 2)     ; → (1 30)
(rational->sexagesimal (sqrt 2) #:places 4); → (1 24 51 10)  (YBC 7289)

; Time/angle — convenience wrappers
(hms->seconds '(1 30 0))                    ; → 5400  (1h 30m 0s)
(seconds->hms 5400)                         ; → (1 30 0)
(dms->degrees '(23 27 0))                   ; → 23.45  (Earth's axial tilt)
(degrees->dms 23.45)                        ; → (23 27 0)

; Cross-format
(cuneiform->neugebauer "𒁹 𒌋𒁹")           ; → "1,11"
(neugebauer->cuneiform "1,11")              ; → "𒁹 𒌋𒁹"
```

### REPL display

When `current-number-notation` is `'cuneiform` or `'neugebauer`, the REPL and
`write`/`display` render all numbers (including intermediate results) in that
notation. Default remains decimal.

```scheme
(current-number-notation 'neugebauer)
(+ 1/2 1/3)        ; displays: 0;50   (= 50/60)
(* 60 60)          ; displays: 1,0,0
```

---

## Zero

The historical absence of zero is documented, not papered over:

- Cuneiform output uses 𒑊 for zero digits in a multi-digit number (Seleucid convention)
- `(number->string 0 'cuneiform)` → `"𒑊"` with a footnote in the docs that this is
  a Seleucid-period convention; early Babylonian had no zero glyph
- Neugebauer uses `0` for zero digits: `(number->string 3600 'neugebauer)` → `"1,0,0"`

---

## Connection to existing Akkadian infrastructure

The reader already calls `akk_translate(op)` before dispatch — Akkadian
operator names transparently remap to English. Cuneiform number recognition
happens *before* `akk_translate`, in the lexer/reader, not in the evaluator.
The two systems are orthogonal:

- `akk_translate` handles *names* (operators, special forms, identifiers)
- Cuneiform number reader handles *literals* (numeric tokens)

Both are active simultaneously. A valid Curry program can use cuneiform digits
as number literals alongside cuneiform operator names.

---

## Fix included: `number->string` binary bug

`(number->string n 2)` currently returns the decimal string instead of the
binary representation. This is a reader/printer bug in the radix dispatch and
will be fixed as part of this release.

```scheme
(number->string 255 2)   ; was: "255"  → fixed: "11111111"
(number->string 10 2)    ; was: "10"   → fixed: "1010"
```

---

## Historical notes worth including in docs

**YBC 7289** — a clay tablet from Old Babylonian period (~1800–1600 BCE)
showing a square with its diagonal. The diagonal is labelled `1;24,51,10`,
which equals 1.41421296... — √2 accurate to 6 decimal places. The method was
almost certainly successive approximation (the Babylonian method, now called
Newton's method for square roots). Worth showing in the examples:

```scheme
(display (number->string (sqrt 2) 'neugebauer #:places 4))
; → "1;24,51,10"   — the YBC 7289 tablet value
```

**Plimpton 322** — a table of Pythagorean triples, also Old Babylonian, whose
purpose is still debated (trigonometric table? accounting? teaching tool?).
The triples are exact; the values are large sexagesimal integers. A short
example in the docs showing that Curry can reproduce the table exactly using
exact rationals would be satisfying.

**Degree-minute-second survives** because Babylonian astronomers (Chaldean
period, ~500 BCE onward) used sexagesimal for celestial measurements. Greek
astronomers inherited it. We still measure angles and time in base 60 because
of choices made in Babylon four thousand years ago.

---

## Implementation sketch

1. **Lexer** (`src/reader.c`): add cuneiform block detection; parse ASH/U/SHAR2
   sequences into sexagesimal digit groups separated by spaces; convert to
   exact rational.
2. **`#s` prefix** (`src/reader.c`): Neugebauer integer/fractional parser;
   result is exact rational (or fixnum for single-digit values ≤ 59).
3. **`number->string` extension** (`src/builtins.c` / `src/numeric.c`): add
   `'cuneiform` and `'neugebauer` branches; `#:places` keyword for flonum
   truncation.
4. **`string->number` extension**: add `'cuneiform` and `'neugebauer` radix symbols.
5. **Helpers** (`lib/curry/modules/curry/sexagesimal.scm`): pure Scheme —
   `sexagesimal->rational`, `rational->sexagesimal`, `hms->seconds`,
   `seconds->hms`, `dms->degrees`, `degrees->dms`, `cuneiform->neugebauer`,
   `neugebauer->cuneiform`.
6. **`current-number-notation`** (`src/port.c` or thread-local in `src/eval.c`):
   dynamic variable; `write`/`display` consult it.
7. **Test suite** (`tests/babylonian_tests.scm`): YBC 7289, Plimpton 322 spot
   checks, round-trip parse/format, zero handling, REPL notation switching.
8. **Docs** (`docs/reference/babylonian-numbers.md`): historical context,
   full API reference, examples.

**Effort estimate:** 2–3 weeks.
