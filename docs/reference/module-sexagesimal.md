# (curry sexagesimal) — Babylonian Base-60 Arithmetic

*v1.2.5 — 2026-06-08*

Curry supports Babylonian sexagesimal (base-60) arithmetic natively: in the
reader, in `number->string`/`string->number`, and in the `(curry sexagesimal)`
pure-Scheme convenience library.

The notation follows Otto Neugebauer (1935), who introduced the comma/semicolon
transcription used by modern Assyriology. The sexagesimal system survives today
in hours/minutes/seconds and degrees/arcminutes/arcseconds.

---

## Reader syntax

### `#s` prefix — Neugebauer literals

```scheme
#s1;30        ; → 3/2       (1 + 30/60)
#s1,0,0       ; → 3600      (1×60² + 0×60 + 0)
#s1;24,51,10  ; → 30547/21600   (YBC 7289 √2 approximation, c. 1800 BCE)
#s0;30        ; → 1/2
```

- Commas separate integer sexagesimal places (most significant first).
- Semicolons mark the radix point. Everything after `;` is fractional.
- The semicolon is **not** a delimiter inside `#s` literals.
- Results are always exact rationals or exact integers.

### Cuneiform Unicode tokens

Babylonian cuneiform numerals are valid tokens directly in source:

| Glyph | Codepoint | Meaning |
|-------|-----------|---------|
| 𒁹 | U+12079 ASH | unit strokes (1–9) |
| 𒌋 | U+1230B U | tens (10, 20, …, 50) |
| 𒑊 | U+1244A | zero placeholder |

Adjacent glyphs form one sexagesimal group; groups are separated by spaces:

```scheme
𒌋𒌋𒁹𒁹𒁹   ; → 23   (2 tens + 3 ones)
𒁹 𒌋𒁹     ; → 71   (1×60 + 11)
𒁹 𒑊       ; → 60   (1×60 + 0)
𒑊           ; → 0
```

**Note:** 𒁹 and 𒌋 are also Akkadian synonyms for `define` and `and`
respectively. In operator position (head of a form) they act as those symbols;
in value position as part of a multi-group token (e.g. `𒁹 𒌋𒁹`) they remain
sexagesimal numbers. Use `(string->number "𒁹" 'cuneiform)` to unambiguously
obtain the number 1.

---

## Built-in procedures

### `(number->string n 'neugebauer)`
### `(number->string n 'neugebauer #:places k)`

Format any exact or inexact number in Neugebauer notation.

```scheme
(number->string 71 'neugebauer)           ; → "1,11"
(number->string 3/2 'neugebauer)          ; → "1;30"
(number->string 30547/21600 'neugebauer)  ; → "1;24,51,10"
(number->string 3600 'neugebauer)         ; → "1,0,0"
(number->string 0 'neugebauer)            ; → "0"
(number->string (sqrt 2) 'neugebauer #:places 3)  ; → "1;24,51,10"
```

`#:places k` limits fractional sexagesimal digits for flonums.

### `(number->string n 'cuneiform)`

Format as cuneiform glyph string. Supports the entire numeric tower.

```scheme
(number->string 71 'cuneiform)   ; → "𒁹 𒌋𒁹"
(number->string 23 'cuneiform)   ; → "𒌋𒌋𒁹𒁹𒁹"
(number->string 0 'cuneiform)    ; → "𒑊"
```

A rational or flonum gets a fractional part after "·" (U+00B7 MIDDLE DOT, the
cuneiform radix point):

```scheme
(number->string 3/2 'cuneiform)  ; → "𒁹 · 𒌋𒌋𒌋"     (1;30)
```

Complex/quaternion/octonion/multivector render as repeated
`('+'|'-') <magnitude> <unit>` terms after a leading scalar, where `<unit>` is
𒄿 (U+1213F CUNEIFORM SIGN I — reused for its sound value, the traditional
single complex imaginary axis) for complex numbers, or one-or-more
`𒂊<digit>` (U+1208A CUNEIFORM SIGN E + a cuneiform digit 1-8, the basis vector
e_n) units for quaternion/octonion/multivector:

```scheme
(number->string (make-rectangular 3 4) 'cuneiform)
  ; → "𒁹𒁹𒁹+𒁹𒁹𒁹𒁹𒄿"          (3+4i)
(number->string (make-quaternion 1 2 3 4) 'cuneiform)
  ; → "𒁹+𒁹𒁹𒂊𒁹+𒁹𒁹𒁹𒂊𒁹𒁹+𒁹𒁹𒁹𒁹𒂊𒁹𒁹𒁹"   (1+2i+3j+4k)
```

A multivector's blade label concatenates one `𒂊<digit>` unit per set bit, in
ascending index order — the cuneiform analogue of `mv-write`'s ASCII `e13`
labels (`𒂊𒁹𒂊𒁹𒁹𒁹` for blade `e13`).

### `(string->number s 'neugebauer)`

Parse a Neugebauer notation string to an exact rational or integer.
Returns `#f` if the string is not valid Neugebauer notation.

```scheme
(string->number "1;30" 'neugebauer)       ; → 3/2
(string->number "1,0,0" 'neugebauer)      ; → 3600
(string->number "1;24,51,10" 'neugebauer) ; → 30547/21600
(string->number "bad" 'neugebauer)        ; → #f
```

### `(string->number s 'cuneiform)`

Parse a cuneiform glyph string produced by `(number->string _ 'cuneiform)`
back to an exact number — this is the inverse of everything documented above
(integers, rationals/flonums with a "·" fractional part, and
complex/quaternion/octonion/multivector extended notation). Returns `#f` if
the string doesn't parse as a WHOLE cuneiform literal (no partial-prefix
successes), including any out-of-range basis index (only 1-8 are valid) or
trailing unrecognized content.

```scheme
(string->number "𒁹 𒌋𒁹" 'cuneiform)      ; → 71
(string->number "𒌋𒌋𒁹𒁹𒁹" 'cuneiform)   ; → 23
(string->number "𒑊" 'cuneiform)           ; → 0
(string->number "𒁹 · 𒌋𒌋𒌋" 'cuneiform)  ; → 3/2
(string->number "𒁹𒁹𒁹+𒁹𒁹𒁹𒁹𒄿" 'cuneiform)  ; → 3+4i
```

A general multivector's metric signature (p,q,r) isn't observable from the
notation itself (it only records which blades are nonzero) — reading one back
reconstructs a Euclidean `Cl(n,0,0)` sized to the highest basis index
referenced, which is not a lossless round-trip for a non-Euclidean algebra
(e.g. `Cl(3,1,0)` Minkowski spacetime).

### `(current-number-notation)`
### `(current-number-notation sym)`

Global display notation parameter. Affects `display` and `write` in the REPL
and in scripts.

```scheme
(current-number-notation)            ; → #f  (decimal, the default)
(current-number-notation 'neugebauer)
(display 71) (newline)               ; prints: 1,11
(current-number-notation 'cuneiform)
(display 71) (newline)               ; prints: 𒁹 𒌋𒁹
(current-number-notation #f)         ; reset to decimal
```

---

## `(curry sexagesimal)` module

Pure-Scheme convenience library. Import with:

```scheme
(import (curry sexagesimal))
```

### Rational ↔ digit list

#### `(rational->sexagesimal r)`
#### `(rational->sexagesimal r #:places k)`

Convert an exact rational or integer to a list of sexagesimal digit groups.
The first element is the integer part; subsequent elements are fractional digits
(each 0–59).

```scheme
(rational->sexagesimal 3/2)          ; → '(1 30)
(rational->sexagesimal 3600)         ; → '(3600)   ; single group ≥ 60
(rational->sexagesimal 71)           ; → '(1 11)
(rational->sexagesimal 1/4)          ; → '(0 15)
(rational->sexagesimal (sqrt 2) #:places 4)  ; → '(1 24 51 10)
```

#### `(sexagesimal->rational lst)`

Convert a digit list back to an exact rational. The first element is the
integer sexagesimal digit; subsequent elements are fractional positions.

```scheme
(sexagesimal->rational '(1 30))      ; → 3/2
(sexagesimal->rational '(1 24 51 10)) ; → 30547/21600
(sexagesimal->rational '())          ; → 0
```

### Time: hours / minutes / seconds

#### `(hms->seconds hms)`

Convert `(h m s)` list to total seconds. Exact if inputs are exact.

```scheme
(hms->seconds '(1 30 0))   ; → 5400
(hms->seconds '(0 1 30))   ; → 90
```

#### `(seconds->hms total-seconds)`

Convert total seconds to `(h m s)` list using integer arithmetic.

```scheme
(seconds->hms 5400)   ; → '(1 30 0)
(seconds->hms 90)     ; → '(0 1 30)
```

### Angle: degrees / arcminutes / arcseconds

#### `(dms->degrees dms)`

Convert `(deg arcmin arcsec)` list to decimal degrees (exact rational).

```scheme
(dms->degrees '(23 27 0))    ; → 14049/600  (≈ 23.45°)
(dms->degrees '(0 30 0))     ; → 1/2
```

#### `(degrees->dms deg)`

Convert decimal degrees to `(deg arcmin arcsec)` list, rounded to the
nearest arcsecond.

```scheme
(degrees->dms 23.45)   ; → '(23 27 0)
(degrees->dms 0.5)     ; → '(0 30 0)
```

### Notation conversion

#### `(cuneiform->neugebauer str)`

Convert a cuneiform glyph string to Neugebauer notation. Errors on invalid input.

```scheme
(cuneiform->neugebauer "𒁹 𒌋𒁹")    ; → "1,11"
(cuneiform->neugebauer "𒌋𒌋𒁹𒁹𒁹")  ; → "23"
```

#### `(neugebauer->cuneiform str)`

Convert a Neugebauer notation string to cuneiform glyphs. Errors on invalid input.

```scheme
(neugebauer->cuneiform "1,11")    ; → "𒁹 𒌋𒁹"
(neugebauer->cuneiform "1;30")    ; → "𒁹 𒌋𒌋𒌋"
```

### Historical constant

#### `(sex:ybc7289)`

Returns the exact rational encoded on Yale Babylonian Collection tablet YBC 7289
(c. 1800 BCE): the sexagesimal approximation of √2 to four fractional places,
`1;24,51,10`.

```scheme
(sex:ybc7289)                           ; → 30547/21600
(exact->inexact (sex:ybc7289))          ; → 1.4142129629629629
(number->string (sex:ybc7289) 'neugebauer)  ; → "1;24,51,10"
```

---

## Codepoint notes

- The cuneiform block covered by the reader is U+12000–U+1247F (`SEX_IS_CUNEIFORM`).
- The zero placeholder is **U+1244A** (CUNEIFORM NUMERIC SIGN TWO ASH TENU). Earlier
  design documents incorrectly listed U+12469.
- The radix-point separator is **U+00B7** MIDDLE DOT (not a cuneiform
  codepoint — chosen precisely so it can't be confused with a digit glyph).
- The extended-notation markers are **U+1213F** CUNEIFORM SIGN I (complex
  imaginary unit) and **U+1208A** CUNEIFORM SIGN E (quaternion/octonion/
  multivector basis blade e_n) — real cuneiform signs, reused here for their
  phonetic value rather than their logographic meaning, chosen to not collide
  with the digit glyphs or any reserved Akkadian keyword glyph.
- Implementation: `src/numeric.c` (parsing and formatting), `src/reader.c`
  (token dispatch), `lib/curry/modules/curry/sexagesimal.scm` (module).
