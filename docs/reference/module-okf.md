# Module: (curry okf)

*unreleased*

[Open Knowledge Format](https://github.com/GoogleCloudPlatform/knowledge-catalog/tree/main/okf) (OKF) v0.2 bundle reader, query layer, and writer, pure Scheme — no C, no external dependency. An OKF bundle is a directory tree of markdown files ("concepts"), each with a YAML frontmatter block carrying provenance (`sources`), trust (`generated`/`verified`), and lifecycle (`status`/`stale_after`) metadata; cross-links between concepts (standard markdown `[text](path.md)`) make the bundle a graph. See [`docs/thoughts/okf-module.md`](../thoughts/okf-module.md) for the design rationale.

Frontmatter parsing/serialization is delegated entirely to [`(curry yaml)`](module-yaml.md) — OKF frontmatter is a strict subset of YAML, and `(curry yaml)` already covers it in full. This module implements only what's OKF-specific: splitting a concept file's leading `---`-delimited frontmatter from its body, trust-tier/staleness derivation, bundle loading, the concept link graph, Attested Computation helpers, and atomic writing.

## Import

```scheme
(import (curry okf))
```

## Scope

Supported: the full OKF v0.2 concept/bundle model — frontmatter accessors (`type`, `title`, `description`, `resource`, `tags`, `sources`, `generated`, `verified`, `status`, `stale_after`, plus an escape hatch for producer-defined keys), trust-tier derivation (§5.3) and staleness (§5.5), bundle-relative and relative markdown link resolution (§6.1) with silent tolerance of broken links (per spec), and Attested Computation contracts (§10) including both inline-computation forms — a fenced code block and a plain 4-space/tab-indented block (the spec's own §10.2 worked example uses the indented form).

Deliberately **not** supported:

- **Attested computation execution.** Running the computation and checking the receipt is outside the module's scope — the `executor`/`attester` fields are just frontmatter (`okf-concept-field`), invoking what they name is caller code.
- **The `computation:` external-file indirection** (§10.3) isn't specially resolved — `okf-computation-inline-sql` only reads the body fence/indented block. A concept using `computation: path/to/file.sql` instead needs the caller to read that path itself via `(okf-concept-field c "computation")`.
- **Restricting frontmatter to the OKF-used YAML subset.** Since parsing is delegated entirely to `(curry yaml)`, frontmatter can in practice use anything that module supports — anchors/aliases, merge keys, explicit scalar tags — even though OKF itself doesn't need any of that. Nothing rejects it.
- **`index.md`'s optional `okf_version` key** (§12, bundle-root `index.md` only). `index.md` is a reserved filename (§3.1) this module never loads as a concept at all.
- **Bundle diffing and `viz.html` generation** — separate concerns; the graph structure `okf-bundle->graph` builds is the right input for either.

## Data model

```scheme
(okf-concept? x)              ; => boolean
(okf-concept-id c)             ; "tables/events_" — bundle-relative path, no .md
(okf-concept-path c)           ; the concept's absolute filesystem path
(okf-concept-frontmatter c)    ; alist of parsed YAML (string keys)
(okf-concept-body c)           ; string: everything after the closing ---

(okf-bundle? x)                ; => boolean
(okf-bundle-root b)            ; bundle's filesystem root, trailing slash stripped
(okf-bundle-table b)           ; hash-table: id -> <okf-concept>
```

## Loading

Layered like `(curry yaml)`/`(curry toml)`: a port-native primitive that does the actual parsing, and file/directory wrappers built on it.

### `(okf-read-concept port id path)` → `<okf-concept>`

Reads `port` to end-of-file, splits frontmatter from body, parses the frontmatter via `yaml-parse`. `id`/`path` are supplied by the caller since a port has neither.

### `(okf-load-concept root path)` → `<okf-concept>`

File wrapper: `(call-with-input-file path (lambda (p) (okf-read-concept p id path)))`, with `id` derived from `path` relative to `root`.

### `(okf-load-bundle root)` → `<okf-bundle>`

Recursively walks `root`, loading every `.md` file except the reserved `index.md`/`log.md` (§3.1, checked at every directory level) as a concept. Symlinks are skipped outright — not followed — so a symlink cycle can't cause unbounded recursion; an entry that can't be `stat`'d (permission error, a concurrent delete) is skipped rather than aborting the whole load.

```scheme
(import (curry okf))
(define bundle (okf-load-bundle "./bundles/ga4"))
(length (okf-bundle-concepts bundle))   ; => however many concept files it found
```

## Frontmatter accessors

All return `#f` (or `'()` for list-valued fields) when the key is absent.

```scheme
(okf-concept-type c)         ; string, or #f (REQUIRED by spec, but this module tolerates its absence)
(okf-concept-title c)        ; string or #f
(okf-concept-description c)  ; string or #f
(okf-concept-resource c)     ; string or #f
(okf-concept-tags c)         ; list of strings, default '()
(okf-concept-sources c)      ; list of alists, default '()
(okf-concept-generated c)    ; alist, e.g. (("by" . "agent/1.0") ("at" . "...")), or #f
(okf-concept-status c)       ; "draft" | "stable" | "deprecated", default "stable"
(okf-concept-stale-after c)  ; "2026-09-23"-shaped string, or #f
(okf-concept-verified c)     ; list of {by,at} alists, default '() — see normalization below
(okf-concept-field c key)    ; escape hatch: raw lookup of any frontmatter key, string or #f
```

### `verified` normalization

Per spec §5.2, a single verifier MAY be written as one bare `{ by, at }` mapping instead of a one-element list; `okf-concept-verified` normalizes that bare-mapping form into a one-element list, so callers always see a list regardless of which form the frontmatter used. Malformed frontmatter (`verified: true`, or any other non-map, non-list value) is treated as absent (`'()`) rather than raising.

## Trust and lifecycle

### `(okf-trust-tier c)` → `'unverified` | `'machine-confirmed` | `'human-reviewed`

Per spec §5.3: no `verified` entries ⇒ `'unverified`; only non-`human:`-prefixed actors ⇒ `'machine-confirmed`; any `human:<id>` actor ⇒ `'human-reviewed`.

### `(okf-stale? c)` → boolean

Per spec §5.5: `(string>=? today (okf-concept-stale-after c))`. ISO 8601 dates sort lexicographically, so this is a plain string comparison, no date arithmetic. `#f` if `stale_after` is absent.

## Querying

```scheme
(okf-bundle-concepts bundle)              ; list of every <okf-concept>
(okf-bundle-ref bundle id)                ; <okf-concept> for id, or #f

(okf-concepts-by-type bundle type)        ; filter by exact type string
(okf-concepts-by-tag bundle tag)          ; filter by tag membership
(okf-concepts-by-trust-tier bundle tier)  ; filter by okf-trust-tier
(okf-concepts-stale bundle)               ; filter by okf-stale?

(okf-attested-computation? c)             ; type = "Attested Computation"
```

## Graph

### `(okf-concept-links c)` → list of raw target strings

Extracts every `[text](target.md)` markdown link from the body via `(curry regex)`, returning the raw target text (not yet resolved to a bundle id).

### `(okf-resolve-link bundle c target)` → id | `#f`

Resolves a raw link target against `c`'s own location to a bundle id, per spec §6.1: a target beginning with `/` is bundle-relative from the root; anything else is resolved relative to `c`'s own directory (`../`/`./` segments collapse; excess `..` beyond the bundle root silently drop rather than erroring or escaping — the result is only ever used as a hash-table lookup key against real ids the directory walk discovered, so it can never resolve outside the bundle). Returns `#f` if the resolved id doesn't name a concept in `bundle` — per spec, a broken link is not an error.

### `(okf-bundle->graph bundle)` → hash-table: id → list of ids

Every concept's resolved (non-broken) outgoing links.

### `(okf-graph-backlinks graph)` → hash-table: id → list of ids

The reverse adjacency of a graph from `okf-bundle->graph`, for "what links here" queries.

### `(okf-bundle-broken-links bundle)` → list of `(id . list-of-raw-targets)`

Linting helper: every concept that has at least one link not resolving to a concept in the bundle, paired with its broken raw targets.

```scheme
(define bundle (okf-load-bundle "./bundles/ga4"))
(define graph  (okf-bundle->graph bundle))
(define back   (okf-graph-backlinks graph))
(hash-table-ref back "tables/events_" '())   ; => ids of concepts linking to events_
```

## Frontmatter validation

Advisory only — never enforced at load time. `okf-load-bundle` and the accessors above (e.g. `okf-concept-verified`'s malformed-input normalization, `okf-concept-type`'s missing-`type` tolerance) deliberately keep working even when frontmatter doesn't match the spec's shapes, matching OKF's own conformance rule that a consumer ignoring unknown keys is fully conformant. These two functions are for a caller who wants to know about shape problems anyway — before trusting a concept's derived `okf-trust-tier`, say, or before publishing agent-produced output — without forcing every other caller to pay for strict enforcement they didn't ask for. They read the raw frontmatter directly rather than through the tolerant accessors, since surfacing exactly what those accessors paper over is the point.

### `(okf-validate-concept c)` → list of strings

Checks the shapes the spec actually promises: `type` present and non-empty (spec-required); `status` one of `draft`/`stable`/`deprecated` if present; `stale_after` shaped like an ISO 8601 date if present; `tags` a list of strings if present; `sources` a list of maps if present; `generated` and each `verified` entry (bare-map or list form) a map with a non-empty `by`. Returns a list of human-readable issue strings, `'()` when clean. Note this catches shapes the tolerant accessors above deliberately hide — `verified: true` normalizes silently to `'()` via `okf-concept-verified`, but `okf-validate-concept` flags it as `"verified must be a map or a list of maps"`.

### `(okf-validate-bundle bundle)` → list of `(id . issues)`

Bundle-wide sweep, same shape convention as `okf-bundle-broken-links`: only concepts with at least one issue appear.

```scheme
(define bundle (okf-load-bundle "./bundles/ga4"))
(okf-validate-bundle bundle)
;; => '() if every concept's frontmatter matches the spec's shapes
```

## Attested Computation helpers

Per spec §10.2, these only make sense for `type: Attested Computation` concepts (`okf-attested-computation?`).

### `(okf-computation-runtime c)` → string or `#f`

`runtime` is spec-required at the concept's **top level** — never nested under `executor` (`executor` only carries `resource`/`receipt`).

### `(okf-computation-parameters c)` → list of alists, default `'()`

The raw `parameters` frontmatter list, each entry typically `{ name, type, required }`.

### `(okf-computation-inline-sql c)` → string or `#f`

Extracts the computation from a `# Computation` body section, in either form the spec allows (§10.3): a fenced code block, or a plain 4-space/tab-indented block. Returns `#f` if there's no `# Computation` heading, or no such block before the next top-level heading (including when `computation:` names an external file instead — that form isn't followed; see Scope above).

```scheme
(define dau (okf-bundle-ref bundle "computations/daily-active-users"))
(okf-computation-runtime dau)      ; => "bigquery"
(okf-computation-inline-sql dau)   ; => "SELECT ...\n"
```

## Writing

### `(make-okf-concept #:type type [#:title #:description #:resource #:tags #:sources #:generated #:verified #:status #:stale-after #:body])` → `<okf-concept>`

Builds a concept from keyword-style arguments (plain rest-arg plist parsing — see `fits-write-image`'s `#:header` for the same convention elsewhere in curry, not a true keyword-lambda feature). `#:type` is required and raises if omitted; `#:status` defaults to `"stable"`; `#:tags`/`#:sources`/`#:verified` default to `'()` and are omitted from the written frontmatter entirely when empty. The returned concept has no id/path yet — those are supplied when it's written.

### `(okf-write-concept-port c port)`

Port-native primitive: writes `---`-delimited frontmatter (via `yaml-write`) followed by the body, to `port`.

### `(okf-write-concept c root id)`

File wrapper: writes to a pid-suffixed `.tmp` sibling of `root/id.md`, then renames — so a crash mid-write never leaves a half-written concept file. Creates any missing parent directories, tolerating a directory that springs into existence between the check and the create (two writers racing to create the same first-of-its-kind subdirectory).

`id` MUST be bundle-relative: no leading `/`, no `..` path segment, no embedded NUL byte. Violating this raises rather than writing — `id`/`prefix`/`dir` are exactly the kind of value an LLM-agent-driven caller (the design doc's own motivating use case) might get wrong or have manipulated, and silently trusting them would let a crafted id write outside `root`.

```scheme
(define c (make-okf-concept #:type "BigQuery Table" #:title "Daily Active Users"
                             #:body "# Schema\n\n..."))
(okf-write-concept c (okf-bundle-root bundle) "tables/daily_active_users")
```

## Index and log generation

### `(okf-generate-index bundle prefix)`

Regenerates `prefix/index.md`: collects every concept whose id starts with `prefix` and has no further `/` beyond it (immediate children only, not nested subdirectories), and writes a `# Section` heading followed by a `* [title](id.md) - description` line per concept. `prefix ""` targets the bundle root. Same `id`-style validation as `okf-write-concept` applies to `prefix`.

### `(okf-log-append bundle dir kind text)`

Appends a `- YYYY-MM-DD **kind** — text` line to `dir/log.md`, creating it if absent. Goes through the same tmp+rename replacement as `okf-write-concept`, so the file itself is never left half-written — but the read-existing/append/write-back sequence is not lock-protected (no OS-level file-locking primitive exists in `(curry posix)`), so two callers racing on the same `log.md` can still each read the same "existing" content and one's entry is lost to the other's rename. Same `id`-style validation applies to `dir`.

## Notes

- **Reserved filenames** (`index.md`, `log.md`) are recognized by basename at every directory level, per spec §3.1 — a `tables/index.md` is skipped exactly like a root `index.md`.
- **Path safety.** `okf-write-concept`/`okf-generate-index`/`okf-log-append` all validate their bundle-relative-path argument before touching the filesystem; this exists specifically because those arguments are the kind of value an agentic caller might pass unsanitized (see `docs/thoughts/okf-module.md`'s MCP-server example, which forwards an agent-supplied concept id straight into `okf-write-concept`).
- **`okf-resolve-link` never touches the filesystem.** It's a pure string computation followed by a hash-table lookup — a link body can contain arbitrary `../../..` traversal attempts and the worst outcome is `okf-resolve-link` returning `#f` (or, in principle, a different bundle id if one happens to collide after clamping), never a filesystem read outside the bundle.

## Examples

- [`examples/okf_bundle_report.scm`](../../examples/okf_bundle_report.scm) — plain-text health report for a bundle: counts by type, trust tiers, stale concepts, broken links, most-linked-to concepts. No MCP, no setup — the fastest way to see the module do something real.
- [`examples/mcp_okf.scm`](../../examples/mcp_okf.scm) — the design doc's MCP knowledge server, made runnable: every concept as a resource, plus `list-concepts`/`get-concept`/`trust-summary`/`stale-concepts`/`backlinks`/`broken-links` tools.
- [`examples/okf_bootstrap_bundle.scm`](../../examples/okf_bootstrap_bundle.scm) — the producer side: mint concepts with `make-okf-concept`, write them, regenerate indexes, append a log entry, then reload and verify the round trip.

All three default to running against `tests/fixtures/okf-real/acme_retail` (a real bundle vendored from the OKF reference implementation — see that directory's `NOTICE.md`) when no bundle path is given.

## See also

- [`module-yaml.md`](module-yaml.md) — does all the actual frontmatter parsing/serialization this module builds on
- [`module-posix.md`](module-posix.md) — directory walking, file info, rename
- [`module-regex.md`](module-regex.md) — markdown link extraction
- [`module-mcp.md`](module-mcp.md) — the natural pairing per the design doc: an OKF bundle exposed as an MCP knowledge server's resources/tools
- [`docs/thoughts/okf-module.md`](../thoughts/okf-module.md) — the design doc this module implements
