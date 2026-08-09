;;; (curry atom) — Atom 1.0 (RFC 4287) reader and writer, pure Scheme,
;;; built on (curry xml).
;;;
;;; An Atom document parses to an <atom-feed> record (feed-level
;;; metadata plus a list of <atom-entry> records), mirroring (curry
;;; rss)'s own shape deliberately — the two formats solve the same
;;; problem and this module's API reads the same way on purpose.
;;; Anything present in the source beyond the fields modeled below is
;;; preserved in an `extras` alist (tag name string -> text content
;;; string), same convention (curry rss)/(curry toml) both use.
;;;
;;; RFC 4287's "Person construct" (an author, with name/email/uri
;;; sub-elements) becomes an <atom-person> record; its "Link" element
;;; (rel="..." href="..." type="...") becomes an <atom-link> record.
;;; RFC 4287's "Text construct" attribute (`type="text"` / `"html"` /
;;; `"xhtml"` on title/subtitle/summary/rights) is not modeled — this
;;; module always reads the element's own text content and doesn't
;;; distinguish plain text from an HTML/XHTML payload, since decoding
;;; XHTML content specifically would need real (X)HTML handling this
;;; module has no other use for.
;;;
;;; `updated`/`published` are kept as their raw RFC 3339 string exactly
;;; as written — same rationale (curry rss) gives for keeping `pubDate`
;;; as raw text rather than a parsed date object.
;;;
;;; Supported: feed-level title, id, updated, subtitle, generator,
;;; rights, icon, logo, author (repeatable), link (repeatable),
;;; category (repeatable, term only); entry-level title, id, updated,
;;; published, summary, content, rights, author (repeatable), link
;;; (repeatable), category (repeatable, term only); everything else
;;; preserved via `extras`.
;;;
;;; Deliberately not supported (see docs/reference/module-atom.md):
;;;
;;; - `contributor`, `source` (an entry's copy of its origin feed's
;;;   metadata) — present (if at all) only in `extras`.
;;; - Category attributes beyond `term` (`scheme`, `label`) — only the
;;;   term string is kept, in `categories`.
;;; - XHTML content parsing (see above) — `content`'s own text/markup is
;;;   kept as a raw string regardless of its `type` attribute.

(define-library (curry atom)
  (import (scheme base) (curry xml))
  (export
    make-atom-feed atom-feed? atom-feed-title atom-feed-id atom-feed-updated
    atom-feed-subtitle atom-feed-generator atom-feed-rights atom-feed-icon
    atom-feed-logo atom-feed-authors atom-feed-links atom-feed-categories
    atom-feed-entries atom-feed-extras
    make-atom-entry atom-entry? atom-entry-title atom-entry-id atom-entry-updated
    atom-entry-published atom-entry-summary atom-entry-content atom-entry-rights
    atom-entry-authors atom-entry-links atom-entry-categories atom-entry-extras
    make-atom-person atom-person? atom-person-name atom-person-email atom-person-uri
    make-atom-link atom-link? atom-link-href atom-link-rel atom-link-type
    atom-parse atom-read atom-load-file
    atom-stringify atom-write atom-dump-file)
  (begin

;;; =========================================================================
;;; Records
;;; =========================================================================

(define-record-type <atom-person>
  (make-atom-person name email uri)
  atom-person?
  (name  atom-person-name)
  (email atom-person-email)
  (uri   atom-person-uri))

(define-record-type <atom-link>
  (make-atom-link href rel type)
  atom-link?
  (href atom-link-href)
  (rel  atom-link-rel)
  (type atom-link-type))

(define-record-type <atom-entry>
  (make-atom-entry title id updated published summary content rights authors links categories extras)
  atom-entry?
  (title       atom-entry-title)
  (id          atom-entry-id)
  (updated     atom-entry-updated)
  (published   atom-entry-published)
  (summary     atom-entry-summary)
  (content     atom-entry-content)
  (rights      atom-entry-rights)
  (authors     atom-entry-authors)
  (links       atom-entry-links)
  (categories  atom-entry-categories)
  (extras      atom-entry-extras))

(define-record-type <atom-feed>
  (make-atom-feed title id updated subtitle generator rights icon logo authors links categories entries extras)
  atom-feed?
  (title       atom-feed-title)
  (id          atom-feed-id)
  (updated     atom-feed-updated)
  (subtitle    atom-feed-subtitle)
  (generator   atom-feed-generator)
  (rights      atom-feed-rights)
  (icon        atom-feed-icon)
  (logo        atom-feed-logo)
  (authors     atom-feed-authors)
  (links       atom-feed-links)
  (categories  atom-feed-categories)
  (entries     atom-feed-entries)
  (extras      atom-feed-extras))

;;; =========================================================================
;;; Reading
;;; =========================================================================

(define %known-feed-tags '(title id updated subtitle generator rights icon logo author link category entry))
(define %known-entry-tags '(title id updated published summary content rights author link category))

(define (%tag-name el) (symbol->string (xml-tag el)))

(define (%extras el known)
  (let loop ((cs (xml-children el)) (acc '()))
    (cond
      ((null? cs) (reverse acc))
      ((and (xml-element? (car cs)) (not (memq (xml-tag (car cs)) known)))
       (loop (cdr cs) (cons (cons (%tag-name (car cs)) (xml-element-text (car cs))) acc)))
      (else (loop (cdr cs) acc)))))

(define (%text-or-false parent tag)
  (let ((el (xml-find parent tag))) (and el (xml-element-text el))))

(define (%parse-person el)
  (make-atom-person (%text-or-false el 'name) (%text-or-false el 'email) (%text-or-false el 'uri)))

(define (%parse-link el) (make-atom-link (xml-attr el "href") (xml-attr el "rel") (xml-attr el "type")))

;; RFC 4287 requires a <category>'s term attribute, but a real-world feed
;; can still omit it (invalid, but the kind of thing this module aims to
;; tolerate rather than reject outright) -- filter those out rather than
;; keeping #f in the categories list, which %category->xml would later
;; try to write back out as an attribute VALUE, not just an absent one.
;; (filter/%truthy? are defined further down, in the Writing section —
;; fine to forward-reference here, since nothing calls %parse-categories
;; until atom-parse runs, by which point every top-level define in this
;; library body has already been established.)
(define (%parse-categories el)
  (filter %truthy? (map (lambda (c) (xml-attr c "term")) (xml-find-all el 'category))))

(define (%parse-entry el)
  (make-atom-entry
    (%text-or-false el 'title)
    (%text-or-false el 'id)
    (%text-or-false el 'updated)
    (%text-or-false el 'published)
    (%text-or-false el 'summary)
    (%text-or-false el 'content)
    (%text-or-false el 'rights)
    (map %parse-person (xml-find-all el 'author))
    (map %parse-link (xml-find-all el 'link))
    (%parse-categories el)
    (%extras el %known-entry-tags)))

;; (atom-parse string) -> <atom-feed>. Raises if the document's root
;; isn't a <feed> element.
(define (atom-parse str)
  (let ((doc (xml-parse str)))
    (unless (eq? (xml-tag doc) 'feed) (error "atom: root element is not <feed>" (xml-tag doc)))
    (make-atom-feed
      (%text-or-false doc 'title)
      (%text-or-false doc 'id)
      (%text-or-false doc 'updated)
      (%text-or-false doc 'subtitle)
      (%text-or-false doc 'generator)
      (%text-or-false doc 'rights)
      (%text-or-false doc 'icon)
      (%text-or-false doc 'logo)
      (map %parse-person (xml-find-all doc 'author))
      (map %parse-link (xml-find-all doc 'link))
      (%parse-categories doc)
      (map %parse-entry (xml-find-all doc 'entry))
      (%extras doc %known-feed-tags))))

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

(define (atom-read port) (atom-parse (%port->string port)))
(define (atom-load-file path) (call-with-input-file path atom-read))

;;; =========================================================================
;;; Writing
;;; =========================================================================

(define (filter pred lst)
  (cond ((null? lst) '()) ((pred (car lst)) (cons (car lst) (filter pred (cdr lst)))) (else (filter pred (cdr lst)))))
(define (%truthy? x) x) ; used with filter to drop #f entries from a list

(define (%maybe-el tag value) (and value (xml-element tag value)))

(define (%person->xml person)
  (make-xml-element 'author '()
    (filter %truthy?
      (list (%maybe-el 'name (atom-person-name person))
            (%maybe-el 'email (atom-person-email person))
            (%maybe-el 'uri (atom-person-uri person))))))

(define (%link->xml link)
  (make-xml-element 'link
    (filter %truthy?
      (list (let ((h (atom-link-href link))) (and h (cons "href" h)))
            (let ((r (atom-link-rel link))) (and r (cons "rel" r)))
            (let ((t (atom-link-type link))) (and t (cons "type" t)))))
    '()))

(define (%category->xml term) (make-xml-element 'category (list (cons "term" term)) '()))

(define (%extras->xml extras) (map (lambda (kv) (xml-element (string->symbol (car kv)) (cdr kv))) extras))

(define (%entry->xml entry)
  (make-xml-element 'entry '()
    (append
      (filter %truthy?
        (list
          (%maybe-el 'title (atom-entry-title entry))
          (%maybe-el 'id (atom-entry-id entry))
          (%maybe-el 'updated (atom-entry-updated entry))
          (%maybe-el 'published (atom-entry-published entry))
          (%maybe-el 'summary (atom-entry-summary entry))
          (%maybe-el 'content (atom-entry-content entry))
          (%maybe-el 'rights (atom-entry-rights entry))))
      (map %person->xml (atom-entry-authors entry))
      (map %link->xml (atom-entry-links entry))
      (map %category->xml (atom-entry-categories entry))
      (%extras->xml (atom-entry-extras entry)))))

(define (%feed->xml feed)
  (make-xml-element 'feed (list (cons "xmlns" "http://www.w3.org/2005/Atom"))
    (append
      (filter %truthy?
        (list
          (%maybe-el 'title (atom-feed-title feed))
          (%maybe-el 'id (atom-feed-id feed))
          (%maybe-el 'updated (atom-feed-updated feed))
          (%maybe-el 'subtitle (atom-feed-subtitle feed))
          (%maybe-el 'generator (atom-feed-generator feed))
          (%maybe-el 'rights (atom-feed-rights feed))
          (%maybe-el 'icon (atom-feed-icon feed))
          (%maybe-el 'logo (atom-feed-logo feed))))
      (map %person->xml (atom-feed-authors feed))
      (map %link->xml (atom-feed-links feed))
      (map %category->xml (atom-feed-categories feed))
      (%extras->xml (atom-feed-extras feed))
      (map %entry->xml (atom-feed-entries feed)))))

;; (atom-write feed port) -> writes `feed` as Atom 1.0 XML directly to `port`.
(define (atom-write feed port) (xml-write (%feed->xml feed) port))

(define (atom-stringify feed)
  (let ((out (open-output-string)))
    (atom-write feed out)
    (get-output-string out)))

(define (atom-dump-file feed path) (call-with-output-file path (lambda (p) (atom-write feed p))))

  )) ;; end begin, define-library
