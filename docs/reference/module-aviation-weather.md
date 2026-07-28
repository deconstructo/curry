# Module: (curry aviation-weather)

*unreleased*

METAR, TAF, and ATIS aviation weather report parsing. Pure Scheme, using `(curry regex)` (POSIX extended regex, already always built in) — no new C code, no new dependency.

## Import

```scheme
(import (curry aviation-weather))
```

## Design

Unlike a typical regex-per-whole-message parser, a report is split into whitespace-delimited tokens first, and each token is classified independently by a small, local, anchored regex — wind, visibility, RVR, weather phenomena, cloud layers, temperature/dewpoint, and altimeter each get their own tiny pattern tried in turn against each token, in any order, until a token matches none of them. That token, and everything after it, becomes the report's `remarks` (or, for a `RMK`-prefixed tail, everything following `RMK`). This sidesteps POSIX ERE's lack of named capture groups entirely — group *position* only ever matters within one tiny pattern — and it means the same field-classification code is shared across METAR, TAF's per-period forecast groups, and ATIS.

Parsing produces plain immutable records, not a validating framework — curry has nothing like Pydantic, and R7RS records are the natural fit. Each record type has a `->alist` converter (`metar-report->alist`, `taf-report->alist`, `atis-report->alist`, plus `wind->alist`, `cloud-layer->alist`, `weather-phenomenon->alist`, `rvr->alist` for the nested pieces) rather than a baked-in `.to_json()`/`.to_yaml()` method — hand the alist to `(curry json)`'s `json-stringify` or `(curry yaml)`'s `yaml-stringify` for a string.

## Scope

- **Formats**: both North American (statute-mile visibility, `A`-prefixed inHg altimeter) and ICAO international (metre visibility, `Q`-prefixed hPa/QNH altimeter) field conventions are recognized at the token level, in the same report if mixed.
- **`P`/`M` visibility prefixes** (`P6SM` = at least 6SM, `M1/4SM` = less than 1/4SM) are recognized and their numeric value extracted, but the "at least"/"less than" qualifier itself isn't preserved — `(atis-visibility ...)`/`(metar-visibility ...)` for `P6SM` is exactly `6`, indistinguishable from a plain `6SM`.
- **`CAVOK`** ("ceiling and visibility OK": visibility ≥10km, no cloud below 5000ft or the highest minimum sector altitude, no CB, no significant weather — extremely common in international METAR/TAF) and **`NSW`** ("no significant weather") are recognized as boolean flags — `metar-cavok?`/`metar-nsw?`, `taf-group-cavok?`/`taf-group-nsw?`, `atis-cavok?`/`atis-nsw?` — rather than synthesizing a fabricated visibility/cloud/weather value, which would misrepresent a value the report never actually gave. `(metar-visibility ...)`/`(metar-cloud ...)`/`(metar-weather ...)` are `#f`/`()`/`()` on a CAVOK report, same as if those fields were simply absent — check the flag, not the absence, to distinguish "CAVOK" from "genuinely not reported."
- **Weather phenomena** are decomposed into their 2-letter codes (`"TSRA"` → `("TS" "RA")`) using the standard descriptor/precipitation/obscuration/other code tables, but non-standard or unrecognized 2-letter combinations are simply not recognized as a weather-phenomenon token at all (rather than guessed at) — they fall through to `remarks` like any other unrecognized token.
- **ATIS structured fields beyond the shared weather body** (approach in use, active runway, NOTAMs) are **not** parsed into their own fields — ATIS free text varies too much by airport and country for one fixed grammar. Everything after the weather fields is available as `atis-report-remarks` (a raw string), which is honest rather than overfit to one airport's phrasing.
- **European-style temperature notation** (e.g. `TM03`/`TP12` instead of `M03`/`12`) is not recognized.
- **TAF**: base forecast group, `TEMPO`, `BECMG`, `FMddhhmm`, `PROB30`/`PROB40` (with or without a nested `TEMPO`) are all recognized. An unrecognized token encountered after at least one group has already been parsed stops parsing there rather than guessing — the rest of the message becomes `taf-report-remarks`, so nothing is silently discarded even when something doesn't parse.
- **Any token none of the field classifiers (wind/visibility/RVR/`CAVOK`/`NSW`/weather/cloud/temp-dewpoint/altimeter) recognize stops body-parsing at that point** and hands everything from there on to `remarks` — this is deliberate (it's how a METAR's own `RMK` section boundary already works informally in real reports, and it's how a genuinely unparseable report degrades safely rather than guessing wrong), but it does mean a real-world report using some other special token this module doesn't yet know about (there are many, e.g. runway-condition codes, obscure remark markers) will have that token, and everything textually after it in that group, land in `remarks` instead of the field it should have populated — for a TAF specifically, this stops at the group boundary rather than the message boundary, so later groups are unaffected once one group has already been reached. `CAVOK`/`NSW` were added because independent review found them common enough to reproduce this exact failure mode routinely; further common tokens may still be missing.
- **Numeric values are exact**, not floating point — visibility fractions (`1/2SM`) and altimeter settings (`A2993` → `2993/100`) are exact rationals, matching curry's numeric tower. Call `exact->inexact` if you want a decimal.

## METAR / SPECI

### `(metar-parse text)` → *metar-report*

```scheme
(import (curry aviation-weather))
(define m (metar-parse "SPECI CYYZ 051326Z 15008KT 1/2SM R15L/4000V5500FT/D SN VV005 M03/M04 A2993"))

(metar-report-type m)   ; => 'speci
(metar-station m)       ; => "CYYZ"
(metar-time m)          ; => (("day" . 5) ("hour" . 13) ("minute" . 26))
(wind-speed (metar-wind m))       ; => 8
(metar-visibility m)              ; => 1/2
(metar-temperature m)             ; => -3
(metar-altimeter m)               ; => 2993/100
```

Accessors: `metar-report-type` (`'metar`/`'speci`), `metar-station`, `metar-time`, `metar-auto?`, `metar-wind`, `metar-visibility`, `metar-rvr` (list of `<rvr>`), `metar-weather` (list of `<weather-phenomenon>`), `metar-cloud` (list of `<cloud-layer>`), `metar-temperature`, `metar-dewpoint`, `metar-altimeter`, `metar-remarks`.

## TAF

### `(taf-parse text)` → *taf-report*

```scheme
(define t (taf-parse "TAF AMD CYYZ 051239Z 0512/0618 16010KT 1/2SM SN VV004
TEMPO 0512/0514 2SM -SN OVC010
FM051400 16010KT 1SM -SN BR OVC005"))

(taf-station t)                          ; => "CYYZ"
(map taf-group-type (taf-groups t))      ; => (base tempo from)
```

Accessors: `taf-station`, `taf-amended?`, `taf-corrected?`, `taf-issue-time`, `taf-valid-from`, `taf-valid-to`, `taf-groups` (list of `<taf-group>`, first always `'base`), `taf-remarks`.

Each `<taf-group>`: `taf-group-type` (`'base`/`'tempo`/`'becmg`/`'from`/`'prob30`/`'prob40`), `taf-group-valid-from`, `taf-group-valid-to` (`#f` for `'base` and `'from` groups, which don't have their own explicit end), `taf-group-wind`, `taf-group-visibility`, `taf-group-weather`, `taf-group-cloud`.

## ATIS

### `(atis-parse text)` → *atis-report*

```scheme
(define a (atis-parse "1326Z CYYZ ARR ATIS O
1326Z 16008KT 1/2SM SN
VV005 M03/M04 A2993 APCH
ILS RWY 15L. LDG RWY 15L"))

(atis-station a)              ; => "CYYZ"
(atis-type a)                 ; => 'arr
(atis-information-letter a)   ; => "O"
(atis-remarks a)              ; => "APCH ILS RWY 15L LDG RWY 15L"
```

Accessors: `atis-station`, `atis-type` (`'arr`/`'dep`/`#f`), `atis-information-letter`, `atis-time`, `atis-wind`, `atis-visibility`, `atis-weather`, `atis-cloud`, `atis-temperature`, `atis-dewpoint`, `atis-altimeter`, `atis-remarks` (see the Scope note above — this is raw text, not structured).

## Shared record types

- **`<wind>`**: `wind-direction` (exact integer degrees, or the symbol `'VRB`), `wind-speed`, `wind-gust` (or `#f`), `wind-unit` (`'kt`/`'mps`/`'kph`), `wind-variable-from`/`wind-variable-to` (or `#f`).
- **`<cloud-layer>`**: `cloud-layer-type` (`'skc`/`'clr`/`'few`/`'sct`/`'bkn`/`'ovc`/`'vv`), `cloud-layer-height` (hundreds of feet AGL, or `#f` for `skc`/`clr`), `cloud-layer-modifier` (`'cb`/`'tcu`/`#f`).
- **`<weather-phenomenon>`**: `weather-phenomenon-intensity` (`'-`/`'+`/`'vc`/`#f` for moderate), `weather-phenomenon-codes` (list of 2-letter code strings), `weather-phenomenon-raw` (the original token).
- **`<rvr>`**: `rvr-runway`, `rvr-min-distance`, `rvr-max-distance` (or `#f`), `rvr-unit` (currently always `'ft`), `rvr-trend` (`'u`/`'d`/`'n`/`#f`).

```scheme
(import (curry aviation-weather) (curry json))
(json-stringify (metar-report->alist (metar-parse "...")))
```
