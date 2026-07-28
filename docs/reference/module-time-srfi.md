# `(surfage s19 time)`

*unreleased*

[SRFI-19](https://srfi.schemers.org/srfi-19/) time data types and procedures: time objects, dates, Julian/Modified Julian Day conversions, and `strftime`-style formatting/parsing. Pure Scheme except for `current-time`/`current-date`, which need `(curry posix)` for the underlying wall-clock/monotonic reads (`-DBUILD_MODULE_POSIX=ON`, the default).

## Import

```scheme
(import (surfage s19 time))
```

## Scope — what's different from the full SRFI

- **`time-tai` is not supported.** International Atomic Time requires a leap-second table that needs periodic maintenance to stay correct. Rather than give a plausible-but-silently-wrong answer (right only for the current leap-second era, wrong for any historical date, wrong again after the next leap second), every `time-tai`-producing or `time-tai`-consuming procedure raises a clear error instead — `current-time 'time-tai`, `date->time-tai`, `time-utc->time-tai`, `julian-day->time-tai`, etc.
- **`time-monotonic` is numerically identical to `time-utc`** in this implementation. curry's underlying monotonic clock has no separate fixed epoch to convert to/from calendar time meaningfully; the SRFI explicitly permits treating the two as interchangeable when an implementation doesn't distinguish them.
- **`time-process`/`time-thread`** (as arguments to `current-time`) return the same value as `time-monotonic` — curry has no separate process/thread CPU time clock exposed at the Scheme level, only wall/monotonic time.
- **`current-date` has no local timezone detection.** No `localtime()`/`tm_gmtoff` binding exists yet, so the optional `tz-offset` argument defaults to `0` (UTC) rather than silently guessing a local offset. Pass an explicit offset (seconds east of Greenwich) for local time.
- **`~Z`** (symbolic timezone name) is unimplemented — exactly as the SRFI text itself specifies ("not-implemented"). **`~c`/`~x`/`~X`** ("locale's representation") use a fixed reasonable format since curry has no locale subsystem.

Everything else — time objects, dates, comparison, arithmetic, Julian Day/Modified Julian Day conversion, `date->string`/`string->date` — is implemented per the SRFI.

## Time objects

Mutable records with a type tag (`time-utc`, `time-monotonic`, `time-duration`, `time-tai`, `time-process`, `time-thread`) plus a `(second nanosecond)` pair.

- `(make-time type nanosecond second)` → *time* — note the SRFI's own argument order, nanosecond before second.
- `(time? x)`, `(time-type t)`, `(time-second t)`, `(time-nanosecond t)`
- `(set-time-type! t v)`, `(set-time-second! t v)`, `(set-time-nanosecond! t v)`, `(copy-time t)`
- `(current-time [type])` → *time*, defaulting to `time-utc`.
- `(time-resolution [type])` → `1` (nanosecond-labeled fields; the actual clock resolution behind `posix-time`/`monotonic-time` is coarser, this is the field's granularity, not a claim about measured precision).

### Comparison

`(time<? a b)`, `(time<=? a b)`, `(time=? a b)`, `(time>=? a b)`, `(time>? a b)`.

### Arithmetic

`(time-difference a b)`, `(time-difference! a b)`, `(add-duration t dur)`, `(add-duration! t dur)`, `(subtract-duration t dur)`, `(subtract-duration! t dur)` — the `!` variants mutate and return their first argument; the non-`!` variants operate on a copy. Nanosecond overflow/underflow is normalized by carrying into/borrowing from seconds.

## Date objects

Immutable records — Gregorian calendar, with a zone offset in seconds east of Greenwich.

- `(make-date nanosecond second minute hour day month year zone-offset)` → *date*
- `(date? x)` and accessors: `date-nanosecond`, `date-second`, `date-minute`, `date-hour`, `date-day`, `date-month`, `date-year`, `date-zone-offset`
- `(date-year-day date)` → 0-indexed day of year (Jan 1 = `0`)
- `(date-week-day date)` → 0-indexed day of week (Sunday = `0`)
- `(date-week-number date day-of-week-starting-week)` → week number, with weeks starting on the given weekday (`0` = Sunday-starting, `1` = Monday-starting)
- `(current-date [tz-offset])` → *date*, UTC unless `tz-offset` is given (see Scope above)

## Conversions

- `(date->julian-day date)` / `(date->modified-julian-day date)` → exact rational
- `(julian-day->date jd [tz-offset])` / `(modified-julian-day->date mjd [tz-offset])` → *date*
- `(date->time-utc date)` / `(time-utc->date time [tz-offset])`
- `(date->time-monotonic date)` / `(time-monotonic->date time [tz-offset])`
- `(time-utc->julian-day t)`, `(time-utc->modified-julian-day t)`, `(julian-day->time-utc jd)`, `(modified-julian-day->time-utc mjd)`
- `(time-monotonic->julian-day t)`, `(time-monotonic->modified-julian-day t)`, `(julian-day->time-monotonic jd)`, `(modified-julian-day->time-monotonic mjd)`
- `(time-utc->time-monotonic t)` / `(time-monotonic->time-utc t)` (plus `!` in-place variants) — numerically a no-op beyond changing the type tag, per the Scope note above.
- Every `time-tai`-involving converter (`date->time-tai`, `time-tai->date`, `time-utc->time-tai`, `time-monotonic->time-tai`, `julian-day->time-tai`, `modified-julian-day->time-tai`, and their `!` variants) raises.

```scheme
(import (surfage s19 time))

(define d (make-date 0 0 30 14 15 6 2023 0))
(date->string d "~Y-~m-~dT~H:~M:~S~z")   ; => "2023-06-15T14:30:00+0000"
(date-week-day d)                         ; => 4  (Thursday)

(date->julian-day d)                      ; => an exact rational
(date-day (julian-day->date (date->julian-day d)))  ; => 15, same as d
```

## `date->string` / `string->date`

`(date->string date [format-string])` — default format `"~c"`. Supported directives: `~~ ~a ~A ~b ~B ~c ~d ~D ~e ~f ~h ~H ~I ~j ~k ~l ~m ~M ~n ~N ~p ~r ~s ~S ~t ~T ~U ~V ~w ~W ~x ~X ~y ~Y ~z ~Z(no-op) ~1 ~2 ~3 ~4 ~5` — see [the SRFI text](https://srfi.schemers.org/srfi-19/srfi-19.html) for what each means; they match exactly except `~Z`/`~c`/`~x`/`~X` per the Scope note above.

`(string->date input-string template-string)` — supports `~~ ~a ~A ~b ~B ~d ~e ~h ~H ~k ~m ~M ~S ~y ~Y ~z`. Weekday-name directives (`~a`/`~A`) are parsed and skipped but don't set any date field (there's no `date-week-day` setter — it's computed, not stored); month-name directives (`~b`/`~B`/`~h`) do set `date-month`.

```scheme
(string->date "2023-06-15 14:30:00" "~Y-~m-~d ~H:~M:~S")
; => a date object for 2023-06-15T14:30:00
```
