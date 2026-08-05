# Module: `(curry naips)`

*unreleased*

Client for Airservices Australia's public NAIPS `briefing-service` SOAP endpoint. Covers the *briefing* operation family — `loc-brief`, `area-brief`, `met-brief`, `notam-brief` — and turns the response into the record types `(curry aviation-weather)` already defines. Pure Scheme, built on `(curry http)` and `(curry regex)` — no XML library was added to build this; see [Design](#design) for why.

Requires a NAIPS account (a requestor id and password). This module only speaks the wire protocol; it does not manage, store, or validate account credentials beyond basic shape checks.

## Import

```scheme
(import (curry naips))
```

## Design

### Why not a full XML parser?

Every operation this module covers shares one response shape, and that shape has exactly one level of nesting that matters. Pulled directly from the service's own published WSDL/XSD (`https://www.airservicesaustralia.com/naips/briefing-service?wsdl`, importing `?xsd=1`), a briefing response (`LocationBriefingRsp`/`AreaBriefingRsp`/`METBriefingRsp`/`NOTAMBriefingRsp`) is a bare extension of `BriefingResponse`:

```xml
<loc-brief-rsp status="SUCCESS">
  <content>... the whole briefing as one text blob ...</content>
  <product type="TEXT"><content>BASE64...</content></product>
  <product type="TEXT"><content>BASE64...</content></product>
  <!-- one <product> per METAR/TAF/ATIS/NOTAM in the briefing -->
</loc-brief-rsp>
```

(On a non-`SUCCESS` status, `content`/`product` are absent entirely and an `<info>` element explains why.)

Each product's payload is base64 — chosen by the schema itself so a product can be binary (a chart image) as well as text — which means the payload can **never contain `<`** and so can never be confused with markup. Combined with `elementFormDefault="qualified"` (every element is namespace-prefixed, but the prefix itself isn't part of the contract — different serializers may pick `ns0`, `ns1`, `SOAP-ENV`, ...), the whole response is handled in two passes:

1. One regex strips every `<prefix:` / `</prefix:` down to `<` / `</`, so nothing downstream needs to know or care what prefix the server chose.
2. A handful of small, anchored `(curry regex)` patterns — the same "one tiny pattern per field" style `(curry aviation-weather)` already uses — pull out `status`, the optional `<info>`, the first `<content>` (the top-level text blob; schema order guarantees it precedes any `<product>`), and every `<product type="...">​<content>...</content></product>` block.

A real, general-purpose XML parser would need to handle DTDs, mixed content, arbitrary nesting depth, and namespace-aware querying — none of which this one, fixed, known response shape actually exercises. Building one would be solving a materially bigger problem than the one this module has.

### Report classification

Each `TEXT`-type product's base64 is decoded via `(curry base64)`'s `base64-decode-string` and classified by its leading token, the same way `(curry aviation-weather)` itself distinguishes report types: `TAF` → `taf-parse`, `METAR`/`SPECI` → `metar-parse`, a bare `HHMMZ` timestamp → `atis-parse`. Anything else (NOTAM text, SIGMET/AIRMET, ...) is classified `'other` and exposed as raw text only — `(curry aviation-weather)` has no NOTAM structural parser.

## Scope

Covers the four operations that share the WSDL's `BriefingRequest`/`BriefingResponse` shape:

| Operation | Function | Locations/area | Notes |
|-----------|----------|----------------|-------|
| `loc-brief` | `naips-loc-briefing` | 1-12 location/airspace names | METAR/TAF/ATIS, optionally NOTAMs |
| `area-brief` | `naips-area-briefing` | 1-5 area codes (`7`/`8`/`9` + 3 digits) | same product set, no `sigmet` flag |
| `met-brief` | `naips-met-briefing` | 1-4 locations | restricted to caller-chosen MET message types |
| `notam-brief` | `naips-notam-briefing` | 1 location/area (2-5 alphanumeric) | NOTAM summary only, raw text (not structurally parsed) |

**Not covered** — the WSDL has roughly 40 other operations with their own request/response shapes, deliberately out of scope here:

- **`notam-brief`'s "history" mode.** The request schema offers two mutually exclusive branches: `summary` (a plain `EntityId` string — what this module supports) or `history` (a specific NOTAM's revision history), which needs a compound `NOTAMId` — itself `{ loc: EntityKey, series-id, number, year }`, where `EntityKey` is in turn `{ id, fir, type }`. A caller would need to already know the FIR and entity-type of the location, which normally comes from browsing a NOTAM directory (also not covered) — not a "briefing" in the same sense as the other three operations, so it's left out.
- **Charts and other binary products.** A briefing can request chart images (`charts`/`reference`/`variant` flags); a non-`TEXT` product comes back as a `<naips-product>` with `type` set but `text`/`parsed` both `#f` — the base64 is simply not decoded to text. Decoding chart images to a bytevector would be straightforward to add but has no consumer in this module today.
- **Everything else**: SPFIB flight-plan templates, NOTAM proposal submission, RAIM, wind/temp profiles, first/last light, general MET message directories, chart/resource retrieval. Several of these are write operations with real account-state consequences; the rest have entirely different response shapes this module's parser doesn't (and shouldn't be stretched to) handle.

## API

### `(naips-loc-briefing requestor password locations [flags])` → *naips-briefing*

```scheme
(import (curry naips) (curry aviation-weather))

(define b (naips-loc-briefing "MYACCT01" "MyPassw0rd" '("YSSY")))

(naips-briefing-status b)   ; => "SUCCESS"
(define taf (car (naips-briefing-products b)))
(naips-product-report-kind taf)              ; => 'taf
(taf-station (naips-product-parsed taf))     ; => "YSSY"
```

`flags` is an alist of booleans; defaults to `'(("met" . #t))` (METAR/TAF/ATIS only). Pass `'(("met" . #t) ("ntm" . #t))` to also include NOTAMs (they arrive as `'other` products — raw text, not parsed). Other recognized flags: `"hon"`, `"sigmet"`, `"charts"`, `"reference"`. An unrecognized flag name, a flag not valid for this operation (e.g. `"sigmet"` on `area-brief`), or the same flag name given twice all raise immediately rather than being silently dropped or sent as malformed duplicate-attribute XML.

Raises a Scheme error if `locations` isn't 1-12 items, on a non-2xx HTTP response, on a SOAP fault, or on malformed base64 in a product. A well-formed but unsuccessful NAIPS response (bad credentials, unknown location, ...) is **not** an error — check `naips-briefing-status`.

### `(naips-area-briefing requestor password areas [flags])` → *naips-briefing*

Same shape, for a whole briefing area instead of named locations. `areas` is 1-5 codes matching `[789][0-9]{3}` (e.g. `"7100"`). `flags` accepts `"met"`/`"ntm"`/`"hon"`/`"charts"`/`"reference"` (no `"sigmet"` — not offered by `area-brief`'s own schema).

### `(naips-met-briefing requestor password locations message-types)` → *naips-briefing*

`locations`: 1-4 names. `message-types`: 1+ strings from `"TAF"`, `"ADWRNG"`, `"METAR"`, `"SPECI"`, `"WSWRNG"`, `"AQNH"`, `"SIGMET"`, `"AIRMET"`, `"ATIS"` — an unknown type raises immediately rather than being sent to the server.

```scheme
(naips-met-briefing "MYACCT01" "MyPassw0rd" '("YSSY" "YMML") '("METAR" "TAF"))
```

### `(naips-notam-briefing requestor password entity-id)` → *naips-briefing*

`entity-id`: a location or area code, 2-5 alphanumeric characters. Requests the NOTAM *summary* briefing (see Scope for why "history" mode isn't offered). Every product comes back classified `'other` (raw text) — there's no structured NOTAM record type to parse into.

## Records

### `<naips-briefing>`

- `naips-briefing-status` — `"SUCCESS"` / `"ERROR"` / `"INVALID"` / `"ACCESS_VIOLATION"` / ... (the full enum is the service's, not enumerated here).
- `naips-briefing-info` — string explaining a non-`SUCCESS` status, or `#f`.
- `naips-briefing-content` — the whole briefing as one text blob (what a human would read top to bottom), or `#f` if the response wasn't `SUCCESS`.
- `naips-briefing-products` — list of `<naips-product>`, in briefing order.
- `naips-briefing->alist` — alist converter (hand it to `(curry json)`'s `json-stringify` for a string).

### `<naips-product>`

- `naips-product-type` — `"TEXT"` / `"GIF"` / `"PDF"` / `"PNG"` / `"JPEG"`.
- `naips-product-report-kind` — `'taf` / `'metar` / `'atis` / `'other` (unparsed text) / `#f` (non-text product).
- `naips-product-text` — decoded text (`TEXT` products only), or `#f`.
- `naips-product-parsed` — the `(curry aviation-weather)` record (`taf-report`/`metar-report`/`atis-report`), or `#f` for `'other`/non-text products.
- `naips-product->alist` — alist converter; `"parsed"` uses whichever of `taf-report->alist`/`metar-report->alist`/`atis-report->alist` applies.

## Lower-level: request/response functions

Each operation's request builder and the shared response parser are exported directly — useful for logging/inspecting an outgoing request, sending it over a transport other than `(curry http)`, or parsing a response captured elsewhere (a proxy, a saved log) without a live account. This is also how the test suite exercises the module without network access.

- `(naips-build-loc-brief-request requestor password locations flags)` → XML string
- `(naips-build-area-brief-request requestor password areas flags)` → XML string
- `(naips-build-met-brief-request requestor password locations message-types)` → XML string
- `(naips-build-notam-brief-request requestor password entity-id)` → XML string
- `(naips-parse-briefing-response raw-xml)` → *naips-briefing* — shared by all four operations, since they all extend the same `BriefingResponse` type.

## Security notes

- Credentials are XML-escaped before being embedded in the SOAP request body (`&`/`<`/`>`/`"`/`'`), so a password containing any of those characters can't break out of the surrounding markup or inject additional XML.
- The NAIPS endpoint is plain HTTPS (TLS via `(curry http)`/libcurl); there's no additional transport-level hardening in this module beyond what `(curry http)` already provides.
- This module never logs, stores, or echoes back the password — it only appears, escaped, in the one outgoing request body.

## MCP server example

`examples/mcp_naips.scm` exposes the same four operations as MCP tools (`loc_briefing`, `area_briefing`, `met_briefing`, `notam_briefing`) for use with Claude Code or another MCP client. Credentials are read once from `NAIPS_REQUESTOR`/`NAIPS_PASSWORD` at server startup rather than taken as tool arguments, so a password never flows through a tool call or an MCP client's call log.

```bash
NAIPS_REQUESTOR=... NAIPS_PASSWORD=... ./build/curry examples/mcp_naips.scm
```

See the file header for the Claude Code `mcpServers` config snippet.
