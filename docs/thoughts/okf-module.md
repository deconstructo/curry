# `(curry okf)` — Open Knowledge Format support
## A module design for reading, querying, and writing OKF v0.2 bundles

*Drafted 2026-08-04. Status: pre-implementation design sketch.*

---

## What OKF actually is

OKF looks like a file format. It is not primarily a file format. It is a
**trust protocol for agent handoffs**.

The problem it solves: when Agent A produces knowledge and Agent B needs to consume it,
B needs to know whether to act on what it reads. Was this produced by an LLM that may
have hallucinated, or by a deterministic process over live data? Was it verified by a
human? Is it still current? Was the number computed the way the policy says it must be?
None of that is answerable from plain markdown. OKF makes it answerable from frontmatter,
without requiring A and B to share a runtime, a schema registry, or an organization.

As a standalone file format, evaluated in isolation, it is not especially interesting —
it is just a structured convention for markdown files. Its value only shows up at the
handoff boundary: when an agent writes into a bundle and something else (another agent,
a host LLM via MCP, a human reviewer, an attester process) needs to read and act on what
was written with an informed sense of how much to trust it.

---

## The format itself

[Open Knowledge Format](https://github.com/GoogleCloudPlatform/knowledge-catalog/tree/main/okf)
v0.2 represents knowledge as a directory of markdown files with YAML frontmatter. Each
file is a *concept* — a table definition, a metric, an incident playbook, an attested
computation — and the directory tree is a *bundle*. Cross-links between concepts (standard
markdown `[text](path.md)`) make the bundle a graph, not just a hierarchy.

The only required frontmatter key is `type`. Everything else — `title`, `description`,
`resource`, `tags`, `sources`, `generated`, `verified`, `status`, `stale_after` — is
optional but standardized when present. A consumer that ignores unknown keys is fully
conformant. The spec motto is: if you can `cat` a file you can read OKF; if you can
`git clone` a repo you can ship it.

v0.2 adds four things that make the trust protocol work:

- **Provenance**: `sources` lists what the concept was extracted from, with per-source
  credibility signals (`author`, `usage_count`, `last_modified`).
- **Trust**: `generated` records who produced the content; `verified` records who confirmed
  it. A concept is unverified, machine-confirmed, or human-reviewed based on whether any
  `verified[].by` actor starts with `human:`.
- **Lifecycle**: `status` (`draft` / `stable` / `deprecated`) and `stale_after`
  (an absolute `YYYY-MM-DD` date; staleness is `today >= stale_after`).
- **Attested Computation**: a concept type that carries not just what a value *means* but
  a sanctioned way to *compute* it and an attester that confirms the right code ran.

---

## The reference agent: what a producer looks like

The [reference agent](https://github.com/GoogleCloudPlatform/knowledge-catalog/tree/main/okf/src/reference_agent)
in the OKF repo is a good illustration of what agent-produced bundles look like in
practice, and of some non-obvious design decisions.

The agent runs in two passes, each backed by a separate Gemini agent (Google ADK):

**BQ pass.** Given a BigQuery dataset, the agent calls `list_concepts` (which reads BQ
metadata), then for each table calls `read_concept_raw` (schema, partitioning, row counts,
timestamps) and optionally `sample_rows`. It writes one OKF concept per table — with
schema, common query patterns, and cross-links to sibling tables that it discovered by
calling `list_concepts` first. Each invocation of the agent enriches exactly one concept
and ends with a single `write_concept_doc` call.

**Web pass.** This is the more interesting one. The agent is given seed URLs and a page
budget, and it **drives its own crawl**. It calls `fetch_url`, which returns the page
as markdown plus its outbound links. From those links it decides — using its own judgment
as the filter — which are worth following: schema reference pages, metric definitions,
query cookbooks. It skips nav links, "getting started" pages, release notes, anything
with "overview" or "tutorial" in the URL. For each page it fetches, it decides:

1. **Enrich an existing concept** — augment a table doc with what the web page reveals.
2. **Mint a new reference concept** — create a `references/metrics/`, `references/joins/`,
   or other reference doc if the page defines something independently referenceable.
3. **Skip** — the page is noise.

Several design choices in the web agent are worth noting:

**Augmentation is strictly additive, and the tool enforces it.** The agent's system
prompt has non-negotiable rules: every existing `#` heading must appear in the new body
in the same order; the `# Schema` section (populated from real BQ metadata) must retain
every field; the `sources` list may only grow. The `write_concept_doc` tool enforces
these as guards and rejects writes that violate them. A rejected write is not treated as
done — the agent is instructed to re-read the existing doc and retry rather than abandon.

**The crawl graph is tracked inside the tool, not the agent.** `fetch_url` rejects URLs
that were not returned as links by a previously-fetched page, enforced via a
`url_depth` dict. This prevents the agent from inventing URLs, and means the hop-depth
cap is reliable — the agent cannot shortcut to an arbitrary page by typing its URL.

**Metrics and joins are always minted as reference concepts.** The agent has a four-gate
test for whether a page warrants a new reference doc (is it referenceable by name? is it
not an overview? can you write a citation sentence? will two concepts use it?). Metrics
and joins bypass all four gates — they are always minted as `references/metrics/<slug>.md`
and `references/joins/<a>__<b>.md`, and the agent is required to immediately link them
back from the contributing primary docs. An orphaned metric reference that nothing links
to is treated as a bug.

**Orphan prevention at session end.** Before stopping, the web agent verifies that every
reference it minted during the session is linked from at least one primary doc. If any is
still orphaned, it goes back and augments the relevant table docs before finishing.

The overall pattern: a BQ agent that writes authoritative-but-sparse concepts from
structured metadata, followed by a web agent that enriches them with human-authored
context while being strictly constrained from overwriting or shrinking the authoritative
content. The two passes compose cleanly because OKF's frontmatter makes provenance and
content-source explicit, so the second agent can always tell what came from where.

---

## Why Curry should have this

The interesting case is `(curry okf)` + `(curry mcp)` together. Curry scripts can already
be MCP servers. An OKF bundle is structured, trust-tagged knowledge that an LLM host can
navigate incrementally. Combining the two makes a Curry script into an MCP knowledge
server that exposes a bundle's concepts as resources, and query operations (by type, tag,
trust tier, staleness) as tools — with no bespoke schema negotiation, because the bundle
already carries the structure.

```scheme
(import (curry okf) (curry mcp))

(define bundle (okf-load-bundle "./bundles/ga4"))

;; (curry mcp) resources are static URIs, not templates — so each concept
;; gets its own registration up front, one per id, rather than a single
;; "okf://concept/{id}" pattern. The LLM still fetches one at a time,
;; following links as it needs them; OKF's progressive disclosure maps
;; exactly to how an LLM agent actually wants to work.
(for-each
  (lambda (c)
    (mcp-resource (string-append "okf://concept/" (okf-concept-id c))
      (string-append "OKF concept: " (or (okf-concept-title c) (okf-concept-id c)))
      (lambda (uri) (mcp-text (okf-concept-body c)))))
  (okf-bundle-concepts bundle))

;; Tools for structured queries the LLM can call instead of reading every file
(mcp-tool "find-concepts" "List concept ids of a given type"
  '((type . ((type . "string") (description . "OKF type, e.g. \"BigQuery Table\""))))
  (lambda (args)
    (mcp-json (map okf-concept-id
                   (okf-concepts-by-type bundle (arg args 'type))))))

(mcp-tool "trust-summary" "Count concepts per trust tier" '()
  (lambda (_)
    (mcp-json (map (lambda (tier)
                      (cons tier (length (okf-concepts-by-trust-tier bundle tier))))
                    '(unverified machine-confirmed human-reviewed)))))

(mcp-tool "stale-concepts" "List concepts past their stale_after date" '()
  (lambda (_)
    (mcp-json (map (lambda (c)
                      `((id . ,(okf-concept-id c))
                        (stale_after . ,(okf-concept-stale-after c))
                        (trust . ,(symbol->string (okf-trust-tier c)))))
                    (okf-concepts-stale bundle)))))

(mcp-serve "okf" "1.0")
```

That is an MCP knowledge server in roughly 30 lines (a fuller version, with
backlink/broken-link tools and a resource per concept, lives at
`examples/mcp_okf.scm`). The host LLM can fetch concepts by
id (getting back the markdown OKF already formats for readability), ask what concepts
exist by type or tag, check what is stale and how trusted each thing is, and navigate the
graph by following the links embedded in concept bodies — all without loading the whole
bundle into context at once.

The other direction is equally natural: a Curry agent (actor system + writer tools) that
produces an OKF bundle as it works — accumulating structured, provenance-tagged knowledge
as it researches a dataset or audits a codebase — and then serves that bundle to the next
agent or checks it into git for human review.

The implementation requires nothing that Curry does not already have:
`(curry posix)` for directory traversal, `(curry regex)` for link extraction,
R7RS `define-record-type` for the concept record, and `(curry yaml)` for the frontmatter
itself — OKF frontmatter is a strict subset of YAML, and `(curry yaml)` already covers it
in full (block/flow mappings and sequences, all scalar styles, implicit typing). No new
parser is needed.

---

## Data model

Every OKF concept maps to a single Scheme record:

```scheme
(define-record-type <okf-concept>
  (make-okf-concept% id path frontmatter body)
  okf-concept?
  (id          okf-concept-id)           ; "tables/events_"  (bundle-relative, no .md)
  (path        okf-concept-path)         ; "/home/user/bundle/tables/events_.md"
  (frontmatter okf-concept-frontmatter)  ; alist of parsed YAML
  (body        okf-concept-body))        ; string: everything after the closing ---
```

`id` is the concept's stable key within a bundle — the bundle-relative file path with the
`.md` suffix removed. It doubles as the node identifier in the link graph.

`frontmatter` is a plain alist. Keys are strings, values are strings, numbers, booleans,
or lists/alists for nested structures. No custom types: YAML parses into Scheme's own
data primitives, and callers access it through named accessors rather than raw alist
lookups.

A bundle is a hash table of concepts keyed by id, plus the filesystem root:

```scheme
(define-record-type <okf-bundle>
  (make-okf-bundle% root table)
  okf-bundle?
  (root  okf-bundle-root)   ; "/home/user/bundle"
  (table okf-bundle-table)) ; hash-table: id → <okf-concept>
```

---

## Public API

### Loading

Following the layered convention `(curry yaml)`/`(curry json)`/`(curry toml)` already
use: a port-native primitive that does the actual parsing, and file-path wrappers built
on top of it. A concept's on-disk form (frontmatter + body) isn't representable as a
single YAML document, so the port primitive here is OKF-specific rather than a direct
call into `(curry yaml)` — but it takes and needs nothing beyond a port, so callers
reading from sockets, `open-input-string`, or anything else port-shaped are first-class,
not an afterthought.

```scheme
;; Port-native primitive: reads a whole port, splits frontmatter/body, parses via
;; (curry yaml)'s yaml-parse. id/path are supplied by the caller since a port has
;; neither.
(okf-read-concept port id path)   ; => <okf-concept>

;; File wrapper: (call-with-input-file path (lambda (p) (okf-read-concept p id path)))
(define c (okf-load-concept bundle-root "./bundles/ga4/tables/events_.md"))

;; Walks the directory tree, calls okf-load-concept on every non-reserved .md file.
;; index.md and log.md are skipped (they are not concept documents per spec §3.1).
(define bundle (okf-load-bundle "./bundles/ga4"))
```

### Frontmatter accessors

All accessors return `#f` (or `'()` for list fields) when the key is absent.
`verified` is normalized: a bare `{ by, at }` map becomes a one-element list,
per spec §5.2.

```scheme
(okf-concept-type c)         ; "BigQuery Table"
(okf-concept-title c)        ; "GA4 Events" or #f
(okf-concept-description c)  ; one-liner or #f
(okf-concept-resource c)     ; URI string or #f
(okf-concept-tags c)         ; '("analytics" "sessions") or '()
(okf-concept-sources c)      ; list of alists, default '()
(okf-concept-generated c)    ; alist '(("by" . "agent/1.0") ("at" . "...")) or #f
(okf-concept-verified c)     ; list of {by,at} alists, default '()
(okf-concept-status c)       ; "draft" | "stable" | "deprecated", default "stable"
(okf-concept-stale-after c)  ; "2026-09-23" or #f

;; Escape hatch for producer-defined keys
(okf-concept-field c "runtime")    ; arbitrary frontmatter key lookup
```

### Trust and lifecycle

```scheme
;; Derives the trust tier from verified[].by actors.
;; Spec §5.3: no verified → 'unverified; only non-human: → 'machine-confirmed;
;; any human: actor → 'human-reviewed.
(okf-trust-tier c)   ; => 'unverified | 'machine-confirmed | 'human-reviewed

;; Staleness is a lexicographic string comparison on ISO 8601 dates.
;; Spec §5.5: stale when today >= stale_after.
(okf-stale? c)       ; => #t | #f
```

### Querying

```scheme
(okf-bundle-concepts bundle)
;; => list of all <okf-concept> records

(okf-concepts-by-type bundle "BigQuery Table")
(okf-concepts-by-tag  bundle "finance")
(okf-concepts-by-trust-tier bundle 'human-reviewed)
(okf-concepts-stale bundle)

;; Predicates for use with filter/find:
(okf-attested-computation? c)   ; type = "Attested Computation"
```

### Graph

```scheme
;; Parses [text](path.md) links from every concept body.
;; Resolves bundle-relative (/path) and relative (./sibling) forms to concept ids.
;; Returns hash-table: id → (list of id)
(define graph (okf-bundle->graph bundle))

;; Reverses the adjacency for backlink queries.
;; Returns hash-table: id → (list of ids that link to it)
(define back  (okf-graph-backlinks graph))

;; Per-concept link extraction (returns raw target strings before id resolution)
(okf-concept-links c)
```

### Attested Computation helpers

```scheme
(okf-computation-runtime c)      ; "bigquery" | "dbt" | "python" | ...
(okf-computation-parameters c)   ; '((("name" . "year") ("type" . "integer") ("required" . #t)))
(okf-computation-inline-sql c)   ; extracts first fenced block under # Computation, or #f
```

### Writing

```scheme
;; Build a concept from keyword args; path is set when writing.
(define c
  (make-okf-concept
    #:type "BigQuery Table"
    #:title "Session Events"
    #:description "One row per GA4 session event."
    #:resource "https://console.cloud.google.com/bigquery?..."
    #:tags '("analytics" "sessions")
    #:generated '(("by" . "my-agent/1.0") ("at" . "2026-08-04T12:00:00Z"))
    #:body "# Schema\n\n| column | type |\n|--------|------|\n| event_name | STRING |"))

;; Port-native primitive: writes frontmatter (via (curry yaml)'s yaml-write) delimited
;; by `---` lines, then the body, to port.
(okf-write-concept-port c port)

;; File wrapper: writes to a `.tmp` sibling via okf-write-concept-port, then renames —
;; so a crash mid-write never leaves a half-written concept file.
(okf-write-concept c (okf-bundle-root bundle) "tables/session_events")

;; Regenerate index.md for a directory prefix.
;; Collects all concepts whose id starts with the prefix and has no further /,
;; writes a # Section / * [title](id.md) - description listing.
(okf-generate-index bundle "")         ; bundle root
(okf-generate-index bundle "tables")
(okf-generate-index bundle "computations")

;; Append an entry to log.md at the given directory level.
(okf-log-append bundle "" "Creation"
  "Initialized bundle from GA4 BigQuery Export.")
(okf-log-append bundle "tables" "Update"
  (string-append "Added [session_events](session_events.md)."))
```

---

## A worked session

```scheme
(import (curry okf))

(define bundle (okf-load-bundle "./bundles/ga4"))
(define graph  (okf-bundle->graph bundle))
(define back   (okf-graph-backlinks graph))

;; Summarize the bundle by trust tier
(for-each
  (lambda (tier)
    (let ((cs (okf-concepts-by-trust-tier bundle tier)))
      (display (length cs))
      (display " ")
      (display tier)
      (newline)))
  '(unverified machine-confirmed human-reviewed))

;; Find every stale computation and list what links to it
(for-each
  (lambda (c)
    (when (and (okf-attested-computation? c) (okf-stale? c))
      (display (okf-concept-id c))
      (display " (stale, referenced by: ")
      (display (hash-table-ref/default back (okf-concept-id c) '()))
      (display ")\n")))
  (map cdr (okf-bundle-concepts bundle)))

;; Add a new table concept and regenerate the tables index
(okf-write-concept
  (make-okf-concept
    #:type "BigQuery Table"
    #:title "Daily Active Users"
    #:description "Deduplicated count of users with at least one session event per day."
    #:tags '("analytics" "metrics")
    #:generated `(("by" . "my-agent/1.0")
                  ("at" . ,(date->string (current-date) "~Y-~m-~dT~H:~M:~SZ")))
    #:body "# Schema\n\n| column | type |\n|--------|------|\n| date | DATE |\n| dau | INTEGER |")
  (okf-bundle-root bundle)
  "tables/daily_active_users")

(okf-generate-index bundle "tables")
(okf-log-append bundle "tables" "Creation" "Added daily_active_users.")
```

---

## Implementation notes

### YAML frontmatter

No new YAML parser is needed. `(curry yaml)` is a full pure-Scheme YAML reader/writer
already in the codebase (block/flow mappings and sequences, all scalar styles, implicit
null/bool/int/float typing, anchors/aliases, merge keys) — well beyond the subset OKF
frontmatter actually uses. Frontmatter parsing is just `(yaml-parse frontmatter-str)`;
`yaml-null` (distinguished from `#f`) and `yaml-stringify` handle the round-trip.

What OKF-specific code still needs writing is the document-splitting step — finding the
frontmatter's opening/closing `---` and separating it from the body, which is a plain
string/line operation, not a YAML concern:

```scheme
;; string → (yaml-str . body-str)
;; Finds the opening and closing ---, splits there.
(define (okf-split-frontmatter content) ...)
```

Output of `yaml-parse` is plain alists for maps, plain lists for sequences, strings/
numbers/booleans/`yaml-null` for scalars. No custom OKF types: `(assoc "by"
(okf-concept-generated c))` works without any accessor magic.

Writing frontmatter back out reuses `yaml-write` directly against the alist built from
keyword args — no separate OKF serializer. `yaml-stringify`'s block style (2-space
indent, inline `{...}`/`[...]` where it fits) already matches OKF's conventional
frontmatter shape closely enough that no custom formatting logic is needed.

### Staleness

`okf-stale?` is `(string>=? today (okf-concept-stale-after c))` where `today`
is `(date->string (current-date) "~Y-~m-~d")` via `(curry posix)`. ISO 8601
sorts lexicographically, so the comparison is exact with no date arithmetic.

### Link extraction

`(okf-concept-links c)` uses `(curry regex)` to find all `[text](target)` links
in the body where target ends in `.md`. It returns raw target strings. The
graph builder calls `okf-resolve-link` to normalize them to concept ids:
bundle-relative paths (`/tables/events_.md`) strip the leading `/` and the
`.md`; relative paths (`../other.md`) are resolved against the concept's own
directory and then made bundle-relative.

Broken links (target not in the bundle) are not errors — the spec explicitly
permits them (§6.1). The graph builder silently drops them; a linting helper
`okf-bundle-broken-links` can surface them separately.

### Atomic writes

`okf-write-concept` writes to a `.tmp` sibling and renames, so a crash during
write never leaves a half-written concept file. OKF bundles are typically under
git, so this is belt-and-suspenders, but it costs nothing.

---

## What this does not cover

- **Attested computation execution**: running the computation and checking the
  receipt is deliberately outside the module's scope. The executor/attester
  are referenced by `resource` paths; invoking them is caller code.
- **Restricting frontmatter to the OKF-used YAML subset**: since frontmatter
  parsing is delegated entirely to `(curry yaml)` rather than a
  purpose-built subset parser, OKF frontmatter can in practice use anything
  `(curry yaml)` supports — anchors/aliases, merge keys, the core explicit
  scalar tags, multi-document markers — even though OKF itself doesn't
  specify or need any of that. Nothing rejects it; it just parses. A bundle
  linter that wants to flag "frontmatter using non-OKF YAML features" would
  need to walk the parsed alist itself, not rely on the parser refusing it.
- **Bundle diffing**: comparing two bundle snapshots for changed/added/removed
  concepts is useful but belongs in a separate utility or thin wrapper around
  `git diff`.
- **Visualization**: generating the self-contained `viz.html` the reference
  agent produces. The graph structure this module builds is the right input for
  such a renderer; the HTML templating is a separate concern.

---

## File layout when implemented

```
lib/curry/modules/curry/
  okf.scm                     ; the module: (curry okf), imports (curry yaml)
```

Link extraction and frontmatter splitting are small and used only by this module, so
they live directly in `okf.scm` rather than a `private/` helper — no other module has a
reason to import them (unlike `(curry private binary-io)`, which `fits`/`netcdf`/`hdf5`
genuinely share).

Tests go in `tests/okf_tests.scm`. The test fixtures can be a small synthetic
bundle checked into `tests/fixtures/okf/`, covering: a minimal concept (type
only), a full v0.2 concept, an Attested Computation, a stale concept, broken
links, and the `verified` bare-map normalization case.
