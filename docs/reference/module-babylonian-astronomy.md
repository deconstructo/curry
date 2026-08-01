# module: `(curry babylonian-astronomy)`

A handful of genuinely-attested Babylonian mathematical-astronomy
techniques (Neugebauer, *A History of Ancient Mathematical Astronomy*,
the "ACT" corpus) — not a full reimplementation of the MUL.APIN
star-almanac system, which is a much larger star-rising/setting-times
project on its own.

## Prerequisites

Pure Scheme — no C module, no build step, no external library. Works
everywhere curry runs. Depends only on the built-in `'neugebauer`
sexagesimal notation (`(string->number "29;31,50,8,20" 'neugebauer)`) for
the synodic-month constant.

## Scope

Three techniques:

- The **zigzag function** — the general linear ramp-and-reflect technique
  System A astronomy used for periodic quantities (length of daylight,
  lunar/solar velocity, ...).
- The **System B mean synodic month** constant and the **Saros** period
  relation (223 synodic months) used in Goal-Year-text eclipse prediction.
- The twelve **Babylonian civil calendar month names**.

The daylight-length example uses commonly cited System A parameters (max/min
in UŠ, a 12-month period) as a worked illustration of the zigzag technique,
not a transcription of any specific tablet — exact parameters vary across
the different System A/B schemes and periods actually attested.

## API

```scheme
(import (curry babylonian-astronomy))

(babylonian-zigzag max min half-period n)   ; -> exact rational
(system-a-daylight-length month)            ; -> exact rational, UŠ

(synodic-month-length)                      ; -> 765433/25920 (≈ 29.530594 days)
(saros-length-days)                         ; -> exact rational, ≈ 6585.32 days
(babylonian-next-eclipse-window known-eclipse-day)  ; -> exact rational

(babylonian-month-name n)                   ; -> string, n = 1..12
```

`babylonian-zigzag` rises linearly from `min` to `max` over `half-period`
steps, then falls back over the next `half-period` steps, repeating with
full period `2 * half-period`; `n` is the step index and may be negative or
larger than one period (wraps correctly via `modulo`). Returns `min` at
`n ≡ 0` and `max` at `n ≡ half-period` (mod the full period).

`babylonian-next-eclipse-window` adds one Saros (223 synodic months,
≈ 6585.32 days — also close to a whole number of anomalistic and draconic
months, which is *why* eclipses recur on this period) to a known eclipse's
day number (any consistent epoch, e.g. a Julian Day Number), giving the day
number of the next occurrence in the same Saros series.

## Akkadian aliases

Three of the above have Akkadian aliases (transliterated and cuneiform),
declared via `(curry private lang-aliases)`:

| English | Transliterated | Cuneiform |
|---|---|---|
| `synodic-month-length` | `warḫu` ("month") | 𒌑𒀭 |
| `babylonian-next-eclipse-window` | `attalû` ("eclipse" — attested, e.g. *attalû ša Sîn*, "eclipse of the Moon") | 𒀭 |
| `babylonian-month-name` | `šumu-ša-warḫi` ("name of the month") | 𒁹𒌑 |

`babylonian-zigzag` is deliberately left English-only: "zigzag function" is
Neugebauer's modern analytical term for the technique, not an attested
ancient name, so it isn't given a coined Akkadian name.

## Example

```scheme
(import (curry babylonian-astronomy))

(display (number->string (synodic-month-length) 'cuneiform)) (newline)
(display (babylonian-month-name 1)) (newline)             ; Nisannu
(display (system-a-daylight-length 0)) (newline)           ; 144 (winter solstice, UŠ)
(display (system-a-daylight-length 6)) (newline)           ; 216 (summer solstice, UŠ)
(display (babylonian-next-eclipse-window 2451545)) (newline)
```

See `examples/mul-apin-akkadian.scm` for a fuller worked example written in
Akkadian/cuneiform.

## See also

- `docs/reference/module-sexagesimal.md` — the underlying Babylonian
  base-60 reader/writer, including the cuneiform notation used throughout
  this module's examples.
