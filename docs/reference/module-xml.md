# Module: (curry xml)

*unreleased*

A minimal XML reader/writer, pure Scheme — not a general XML 1.0 processor, scoped to what well-formed feed documents and similarly simple XML actually need. This is `(curry rss)`/`(curry atom)`'s shared foundation, factored out the same way `(curry http)` is a shared dependency for `(curry naips)`/`(curry airports)`/`(curry llm)` rather than something each reimplements.

## Import

```scheme
(import (curry xml))
```

## The element tree

A document parses to a single `<xml-element>`: a tag (a symbol, kept exactly as written including any namespace prefix — see Scope below), an attribute alist (string keys, string values), and a list of children, each either a nested `<xml-element>` or a plain string (a text/CDATA run).

### `(make-xml-element tag attrs children)`

### `(xml-element tag . children)`

A friendlier constructor for hand-building a tree to write out: `attrs` defaults to `'()`, and `children` is a var-arg list rather than an explicit list argument — `(xml-element 'title "Hello")` instead of `(make-xml-element 'title '() (list "Hello"))`. Pass an explicit attrs alist as the first "child" argument to set both: `(xml-element 'link '(("href" . "...")))`.

### `(xml-element? x)` / `(xml-tag el)` / `(xml-attrs el)` / `(xml-children el)`

### `(xml-attr el name)` → string or `#f`

The value of attribute `name` (a string), or `#f` if absent.

### `(xml-element-text el)` → string

Concatenates every direct string child (text/CDATA runs) into one string, skipping nested elements — "this element's own text content", the common case for a leaf element like `<title>some text</title>`.

### `(xml-find el tag)` → `<xml-element>` or `#f`

The first direct child element named `tag` (a symbol).

### `(xml-find-all el tag)` → list

Every direct child element named `tag`, in document order.

## Scope

Supported: elements (nested, self-closing, or with text/mixed content), attributes (single- or double-quoted), CDATA sections (`<![CDATA[ ... ]]>`, kept as literal unescaped text), comments and processing instructions including the XML declaration (skipped), a DOCTYPE declaration (skipped), and the five predefined XML entities (`&amp; &lt; &gt; &quot; &apos;`) plus numeric character references (`&#NNN;`, `&#xHHHH;`).

Deliberately **not** supported:

- **Namespace resolution.** A tag or attribute written `content:encoded` is read (and written) as the literal symbol/string `content:encoded` — the colon is not treated specially, and no `xmlns` declaration is resolved to a URI. The same pragmatic simplification most minimal feed parsers make, since RSS/Atom consumers overwhelmingly match on the literal prefixed name anyway.
- **Named entities beyond the five XML predefines** (e.g. HTML's `&nbsp;`) are left in the text unchanged rather than decoded or rejected — there is no DTD processing here to look them up against.
- **External DTD subsets, internal DTD subsets with entity declarations, and XInclude** — a DOCTYPE line is recognized only enough to be skipped past, never parsed.

## Reading

### `(xml-parse string)` → `<xml-element>`

Parses a document string and returns its single root element, skipping any leading XML declaration/comments/DOCTYPE.

```scheme
(import (curry xml))
(define doc (xml-parse "<root a=\"1\"><child>text &amp; more</child></root>"))
(xml-attr doc "a")                       ; => "1"
(xml-element-text (xml-find doc 'child)) ; => "text & more"
```

### `(xml-read port)` → `<xml-element>`

Parses a whole port's content as XML — reads to end-of-file, then `xml-parse`s the result (XML's nested structure means the full document has to be in hand before parsing can start, same as `(curry toml)`/`(curry csv)`'s own port-native entry points).

### `(xml-load-file path)` → `<xml-element>`

Convenience wrapper: `(call-with-input-file path xml-read)`.

## Writing

### `(xml-stringify el)` → string

### `(xml-write el port)`

Writes `el` as XML directly to `port`. `xml-stringify`/`xml-dump-file` are both thin wrappers around this.

### `(xml-dump-file el path)`

```scheme
(import (curry xml))
(xml-stringify (xml-element 'root '(("a" . "1")) (xml-element 'child "hi")))
; => "<root a=\"1\"><child>hi</child></root>"
```

Text is escaped on write (`&`, `<`, `>`); attribute values escape `&`, `<`, and `"` as well. There is no way to request a CDATA section on write — a string child is always written as escaped text, even if it came from a CDATA section on read (CDATA-ness isn't preserved through the tree; see Notes).

## Notes

- Whitespace inside an element is preserved exactly as read, in text-node children — this module never trims or collapses it. `xml-element-text` doesn't trim either; a caller who wants that does it themselves.
- CDATA-ness isn't part of the tree — `<a><![CDATA[x]]></a>` and `<a>x</a>` parse to the identical structure. Round-tripping such a document through `xml-parse`/`xml-stringify` preserves the *text content* exactly, but not the original CDATA wire representation.

## See also

- [`module-rss.md`](module-rss.md), [`module-atom.md`](module-atom.md) — the two feed formats built on this module
