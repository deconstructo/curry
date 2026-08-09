# Module: (curry atom)

*unreleased*

An Atom 1.0 ([RFC 4287](https://www.rfc-editor.org/rfc/rfc4287)) reader and writer, pure Scheme, built on [`(curry xml)`](module-xml.md).

## Import

```scheme
(import (curry atom))
```

## Values

An Atom document parses to an `<atom-feed>` record (feed-level metadata plus a list of `<atom-entry>` records), mirroring [`(curry rss)`](module-rss.md)'s own shape deliberately — the two formats solve the same problem and this module's API reads the same way on purpose. Anything present in the source beyond the fields modeled below is preserved in an `extras` alist (tag name string → text content string), same convention `(curry rss)`/`(curry toml)` both use, and written back out on `atom-stringify`.

RFC 4287's "Person construct" (an author, with name/email/uri sub-elements) becomes an `<atom-person>` record; its "Link" element (`rel`/`href`/`type` attributes) becomes an `<atom-link>` record. RFC 4287's "Text construct" `type` attribute (`"text"`/`"html"`/`"xhtml"` on `title`/`subtitle`/`summary`/`rights`) is not modeled — this module always reads the element's own text content and doesn't distinguish plain text from an HTML/XHTML payload, since decoding XHTML content specifically would need real (X)HTML handling this module has no other use for.

`updated`/`published` are kept as their raw RFC 3339 string exactly as written — same rationale `(curry rss)` gives for keeping `pubDate` as raw text rather than a parsed date object.

### `<atom-feed>`

| Field | Accessor | Type |
|---|---|---|
| title | `atom-feed-title` | string or `#f` |
| id | `atom-feed-id` | string or `#f` |
| updated | `atom-feed-updated` | raw string or `#f` |
| subtitle | `atom-feed-subtitle` | string or `#f` |
| generator | `atom-feed-generator` | string or `#f` |
| rights | `atom-feed-rights` | string or `#f` |
| icon | `atom-feed-icon` | string or `#f` |
| logo | `atom-feed-logo` | string or `#f` |
| author (repeatable) | `atom-feed-authors` | list of `<atom-person>` |
| link (repeatable) | `atom-feed-links` | list of `<atom-link>` |
| category (repeatable) | `atom-feed-categories` | list of strings (`term` only) |
| entry (repeatable) | `atom-feed-entries` | list of `<atom-entry>` |
| (unrecognized feed elements) | `atom-feed-extras` | alist |

`(make-atom-feed title id updated subtitle generator rights icon logo authors links categories entries extras)` builds one directly.

### `<atom-entry>`

| Field | Accessor | Type |
|---|---|---|
| title | `atom-entry-title` | string or `#f` |
| id | `atom-entry-id` | string or `#f` |
| updated | `atom-entry-updated` | raw string or `#f` |
| published | `atom-entry-published` | raw string or `#f` |
| summary | `atom-entry-summary` | string or `#f` |
| content | `atom-entry-content` | string or `#f` |
| rights | `atom-entry-rights` | string or `#f` |
| author (repeatable) | `atom-entry-authors` | list of `<atom-person>` |
| link (repeatable) | `atom-entry-links` | list of `<atom-link>` |
| category (repeatable) | `atom-entry-categories` | list of strings (`term` only) |
| (unrecognized entry elements) | `atom-entry-extras` | alist |

`(make-atom-entry title id updated published summary content rights authors links categories extras)` builds one directly.

### `<atom-person>` / `<atom-link>`

`(make-atom-person name email uri)` with accessors `atom-person-name`/`atom-person-email`/`atom-person-uri` (any of the three may be `#f`).

`(make-atom-link href rel type)` with accessors `atom-link-href`/`atom-link-rel`/`atom-link-type`.

## Scope

Supported: feed-level title, id, updated, subtitle, generator, rights, icon, logo, author (repeatable), link (repeatable), category (repeatable, term only); entry-level title, id, updated, published, summary, content, rights, author (repeatable), link (repeatable), category (repeatable, term only); everything else preserved via `extras`.

Deliberately **not** supported:

- `contributor`, `source` (an entry's copy of its origin feed's metadata) — present (if at all) only in `extras`.
- Category attributes beyond `term` (`scheme`, `label`) — only the term string is kept, in `categories`.
- XHTML content parsing — `content`'s own text/markup is kept as a raw string regardless of its `type` attribute.

## Reading

### `(atom-parse string)` → `<atom-feed>`

Raises if the document's root isn't a `<feed>` element.

```scheme
(import (curry atom))
(define feed
  (atom-parse "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Example</title><id>urn:uuid:1</id><updated>2026-08-09T12:00:00Z</updated><entry><title>Entry</title><id>urn:uuid:2</id><updated>2026-08-09T12:00:00Z</updated></entry></feed>"))
(atom-feed-title feed)                          ; => "Example"
(atom-entry-title (car (atom-feed-entries feed))) ; => "Entry"
```

### `(atom-read port)` → `<atom-feed>`

### `(atom-load-file path)` → `<atom-feed>`

## Writing

### `(atom-stringify feed)` → string

### `(atom-write feed port)`

### `(atom-dump-file feed path)`

Writes the feed element with an `xmlns="http://www.w3.org/2005/Atom"` attribute (RFC 4287 requires the Atom namespace on the root `<feed>` element).

```scheme
(import (curry atom))
(define feed
  (make-atom-feed "My Feed" "urn:uuid:1" "2026-08-09T12:00:00Z" #f #f #f #f #f '() '() '()
                  (list (make-atom-entry "Entry 1" "urn:uuid:2" "2026-08-09T12:00:00Z" #f #f #f #f '() '() '() '()))
                  '()))
(atom-stringify feed)
```

## See also

- [`module-xml.md`](module-xml.md) — the XML layer this module is built on
- [`module-rss.md`](module-rss.md) — the other feed format, whose API this module's shape deliberately mirrors
