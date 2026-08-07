# Module: `(curry airports)`

*unreleased*

ICAO/name airport directory lookup. Pure Scheme, built on `(curry http)` and `(curry posix)`.

## Import

```scheme
(import (curry airports))
```

## Data source and caching

Data comes from [mborsetti/airportsdata](https://github.com/mborsetti/airportsdata)'s `airports.csv` (MIT licensed, derived from [OurAirports](https://ourairports.com/)' public-domain dataset) — roughly 28,000 airports and landing strips worldwide.

The CSV is downloaded lazily on first use into `~/.cache/curry/airports.csv` and reused after that:

- A cached copy younger than 30 days is used as-is — no network access at all.
- Once it's older than that, the next lookup does a conditional `GET` (`If-None-Match` against the stored `ETag`, via `(curry http)`'s `http-request/headers`) to check whether the source actually changed before re-downloading. A `304` just refreshes the local timestamp.
- If the network is unreachable when a refresh is due, the stale cache is used rather than failing outright — lookups keep working offline once the data has been fetched at least once.
- The CSV write is atomic (temp file + `rename-file`), so a crash or Ctrl-C mid-download can't leave a corrupt cache behind.

`airport-refresh!` bypasses the 30-day TTL and forces an immediate conditional check (still cheap on a `304`).

## API

### `(airport-lookup icao)` → `<airport>` or `#f`

Exact lookup by ICAO code (case-insensitive).

### `(airport-search substr)` → list of `<airport>`

Case-insensitive substring match against airport name.

### `(airport-refresh!)` → `'ok`

Forces a freshness check against the source now, bypassing the TTL, and reloads the in-memory index if the cache changed.

### `<airport>` accessors

| Accessor | Type | Notes |
|---|---|---|
| `airport-icao` | string | |
| `airport-iata` | string | `""` if none |
| `airport-name` | string | |
| `airport-city` | string | often `""` — the source dataset leaves this blank for many small/private fields |
| `airport-region` | string | ISO 3166-2 subdivision name, e.g. `"New South Wales"` |
| `airport-country` | string | ISO 3166-1 alpha-2, e.g. `"AU"` |
| `airport-elevation` | number or `#f` | feet MSL |
| `airport-lat` | number or `#f` | decimal degrees |
| `airport-lon` | number or `#f` | decimal degrees |
| `airport-tz` | string | IANA timezone name |
| `airport-lid` | string | FAA Location Identifier, US airports only |

## Example

```scheme
(import (curry airports))

(define a (airport-lookup "YOAS"))
(display (airport-name a))    ; "The Oaks Airport"
(display (airport-region a))  ; "New South Wales"

(for-each (lambda (a) (display (airport-icao a)) (display " ") (display (airport-name a)) (newline))
          (airport-search "katoomba"))
; YKAT Katoomba Airport
```

## Performance note

The first lookup in a process parses the full ~28,000-row CSV into an in-memory hash table — currently a few seconds, most of which is the general per-variable-lookup cost of running inside a `define-library`'s isolated environment rather than the CSV parsing itself. Every lookup after that in the same process is effectively instant, since the parsed index is kept in memory for the process's lifetime. This makes the module cheap for a long-running script, REPL session, or MCP server, but noticeable for a one-shot `curry -e` invocation.
