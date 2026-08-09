# Module: (curry rss)

*unreleased*

An RSS 2.0 reader and writer, pure Scheme, built on [`(curry xml)`](module-xml.md).

## Import

```scheme
(import (curry rss))
```

## Values

An RSS document parses to an `<rss-feed>` record (channel-level metadata plus a list of `<rss-item>` records). Common channel and item elements get their own named field; anything else present in the source is preserved in an `extras` alist (tag name string → text content string) rather than silently dropped, the same "model the common case, keep the rest around" approach `(curry toml)`'s handling of unrecognized-but-present data takes — and `extras` is written back out on `rss-stringify`, not just kept around for reading.

Date fields (`pubDate`, `lastBuildDate`) are kept as their raw RFC 822 string exactly as written in the source, not parsed into a date object — RFC 822's own date grammar has enough real-world variation (obsolete/military timezone names, two- vs four-digit years) that getting this right is its own separable concern; a caller who wants a real date can already reach `(srfi 19)` themselves.

### `<rss-feed>`

| Field | Accessor | Type |
|---|---|---|
| title | `rss-feed-title` | string or `#f` |
| link | `rss-feed-link` | string or `#f` |
| description | `rss-feed-description` | string or `#f` |
| language | `rss-feed-language` | string or `#f` |
| pubDate | `rss-feed-pub-date` | raw string or `#f` |
| lastBuildDate | `rss-feed-last-build-date` | raw string or `#f` |
| generator | `rss-feed-generator` | string or `#f` |
| ttl | `rss-feed-ttl` | string or `#f` |
| image | `rss-feed-image` | `<rss-image>` or `#f` |
| items | `rss-feed-items` | list of `<rss-item>` |
| (unrecognized channel elements) | `rss-feed-extras` | alist |

`(make-rss-feed title link description language pub-date last-build-date generator ttl image items extras)` builds one directly.

### `<rss-item>`

| Field | Accessor | Type |
|---|---|---|
| title | `rss-item-title` | string or `#f` |
| link | `rss-item-link` | string or `#f` |
| description | `rss-item-description` | string or `#f` |
| author | `rss-item-author` | string or `#f` |
| category (repeatable) | `rss-item-categories` | list of strings |
| guid | `rss-item-guid` | string or `#f` |
| pubDate | `rss-item-pub-date` | raw string or `#f` |
| enclosure | `rss-item-enclosure` | `<rss-enclosure>` or `#f` |
| (unrecognized item elements) | `rss-item-extras` | alist |

`(make-rss-item title link description author categories guid pub-date enclosure extras)` builds one directly.

### `<rss-image>` / `<rss-enclosure>`

`(make-rss-image url title link)` with accessors `rss-image-url`/`rss-image-title`/`rss-image-link`.

`(make-rss-enclosure url length type)` with accessors `rss-enclosure-url`/`rss-enclosure-length`/`rss-enclosure-type` — `length` and `type` are the raw attribute strings (`length` is a byte count as text, not parsed to a number).

## Scope

Supported: `<channel>` metadata (title, link, description, language, pubDate, lastBuildDate, generator, ttl, image), `<item>` (title, link, description, author, category — repeatable, guid, pubDate, enclosure), everything else preserved via `extras`.

Deliberately **not** supported:

- `<cloud>`, `<textInput>`, `<skipHours>`, `<skipDays>`, `<rating>` — present (if at all) only in `extras`, not modeled as their own fields; these are rarely-used RSS 2.0 elements most feed readers themselves ignore.
- RSS 0.9x/1.0 (RDF-based) — this module reads/writes RSS 2.0 only.

## Reading

### `(rss-parse string)` → `<rss-feed>`

Raises if the document's root isn't an `<rss>` element containing a `<channel>`.

```scheme
(import (curry rss))
(define feed (rss-parse "<rss version=\"2.0\"><channel><title>Example</title><link>http://example.com/</link><description>D</description><item><title>Post</title></item></channel></rss>"))
(rss-feed-title feed)                        ; => "Example"
(rss-item-title (car (rss-feed-items feed))) ; => "Post"
```

### `(rss-read port)` → `<rss-feed>`

### `(rss-load-file path)` → `<rss-feed>`

## Writing

### `(rss-stringify feed)` → string

### `(rss-write feed port)`

### `(rss-dump-file feed path)`

```scheme
(import (curry rss))
(define feed
  (make-rss-feed "My Feed" "http://example.com/" "A feed" #f #f #f #f #f
                 (list (make-rss-item "Post 1" "http://example.com/1" #f #f '() #f #f #f '()))
                 '()))
(rss-stringify feed)
```

## See also

- [`module-xml.md`](module-xml.md) — the XML layer this module is built on
- [`module-atom.md`](module-atom.md) — the other feed format, mirroring this module's API shape deliberately
