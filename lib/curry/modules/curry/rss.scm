;;; (curry rss) — RSS 2.0 reader and writer, pure Scheme, built on
;;; (curry xml).
;;;
;;; An RSS document parses to an <rss-feed> record (channel-level
;;; metadata plus a list of <rss-item> records). Common channel and item
;;; elements get their own named field; anything else present in the
;;; source is preserved in an `extras` alist (tag name string -> text
;;; content string) rather than silently dropped, the same "model the
;;; common case, keep the rest around" approach (curry toml)'s handling
;;; of unrecognized-but-present data takes.
;;;
;;; Date fields (`pubDate`, `lastBuildDate`) are kept as their raw RFC
;;; 822 string exactly as written in the source, not parsed into a date
;;; object — RFC 822's own date grammar has enough real-world variation
;;; (obsolete/military timezone names, two- vs four-digit years) that
;;; getting this right is its own separable concern; a caller who wants
;;; a real date can already reach `(srfi 19)` themselves.
;;;
;;; Supported: `<channel>` metadata (title, link, description, language,
;;; pubDate, lastBuildDate, generator, ttl, image), `<item>` (title,
;;; link, description, author, category — repeatable, guid, pubDate,
;;; enclosure), everything else preserved via `extras`.
;;;
;;; Deliberately not supported (see docs/reference/module-rss.md):
;;;
;;; - `<cloud>`, `<textInput>`, `<skipHours>`, `<skipDays>`, `<rating>` —
;;;   present (if at all) only in `extras`, not modeled as their own
;;;   fields; these are rarely-used RSS 2.0 elements most feed readers
;;;   themselves ignore.
;;; - RSS 0.9x/1.0 (RDF-based) — this module reads/writes RSS 2.0 only.

(define-library (curry rss)
  (import (scheme base) (curry xml))
  (export
    make-rss-feed rss-feed? rss-feed-title rss-feed-link rss-feed-description
    rss-feed-language rss-feed-pub-date rss-feed-last-build-date
    rss-feed-generator rss-feed-ttl rss-feed-image rss-feed-items rss-feed-extras
    make-rss-item rss-item? rss-item-title rss-item-link rss-item-description
    rss-item-author rss-item-categories rss-item-guid rss-item-pub-date
    rss-item-enclosure rss-item-extras
    make-rss-image rss-image? rss-image-url rss-image-title rss-image-link
    make-rss-enclosure rss-enclosure? rss-enclosure-url rss-enclosure-length rss-enclosure-type
    rss-parse rss-read rss-load-file
    rss-stringify rss-write rss-dump-file)
  (begin

;;; =========================================================================
;;; Records
;;; =========================================================================

(define-record-type <rss-image>
  (make-rss-image url title link)
  rss-image?
  (url   rss-image-url)
  (title rss-image-title)
  (link  rss-image-link))

(define-record-type <rss-enclosure>
  (make-rss-enclosure url length type)
  rss-enclosure?
  (url    rss-enclosure-url)
  (length rss-enclosure-length)
  (type   rss-enclosure-type))

(define-record-type <rss-item>
  (make-rss-item title link description author categories guid pub-date enclosure extras)
  rss-item?
  (title       rss-item-title)
  (link        rss-item-link)
  (description rss-item-description)
  (author      rss-item-author)
  (categories  rss-item-categories)
  (guid        rss-item-guid)
  (pub-date    rss-item-pub-date)
  (enclosure   rss-item-enclosure)
  (extras      rss-item-extras))

(define-record-type <rss-feed>
  (make-rss-feed title link description language pub-date last-build-date generator ttl image items extras)
  rss-feed?
  (title           rss-feed-title)
  (link            rss-feed-link)
  (description     rss-feed-description)
  (language        rss-feed-language)
  (pub-date        rss-feed-pub-date)
  (last-build-date rss-feed-last-build-date)
  (generator       rss-feed-generator)
  (ttl             rss-feed-ttl)
  (image           rss-feed-image)
  (items           rss-feed-items)
  (extras          rss-feed-extras))

;;; =========================================================================
;;; Reading
;;; =========================================================================

;; The set of channel/item element names modeled as their own field —
;; everything else present under <channel>/<item> lands in `extras`
;; instead (tag name string -> its own xml-element-text).
(define %known-channel-tags '(title link description language pubDate lastBuildDate generator ttl image item))
(define %known-item-tags '(title link description author category guid pubDate enclosure))

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

(define (%parse-image el)
  (make-rss-image (%text-or-false el 'url) (%text-or-false el 'title) (%text-or-false el 'link)))

(define (%parse-enclosure el)
  (make-rss-enclosure (xml-attr el "url") (xml-attr el "length") (xml-attr el "type")))

(define (%parse-item el)
  (make-rss-item
    (%text-or-false el 'title)
    (%text-or-false el 'link)
    (%text-or-false el 'description)
    (%text-or-false el 'author)
    (map xml-element-text (xml-find-all el 'category))
    (%text-or-false el 'guid)
    (%text-or-false el 'pubDate)
    (let ((enc (xml-find el 'enclosure))) (and enc (%parse-enclosure enc)))
    (%extras el %known-item-tags)))

;; (rss-parse string) -> <rss-feed>. Raises if the document's root isn't
;; an <rss> element containing a <channel>.
(define (rss-parse str)
  (let ((doc (xml-parse str)))
    (unless (eq? (xml-tag doc) 'rss) (error "rss: root element is not <rss>" (xml-tag doc)))
    (let ((channel (xml-find doc 'channel)))
      (unless channel (error "rss: <rss> has no <channel>"))
      (make-rss-feed
        (%text-or-false channel 'title)
        (%text-or-false channel 'link)
        (%text-or-false channel 'description)
        (%text-or-false channel 'language)
        (%text-or-false channel 'pubDate)
        (%text-or-false channel 'lastBuildDate)
        (%text-or-false channel 'generator)
        (%text-or-false channel 'ttl)
        (let ((img (xml-find channel 'image))) (and img (%parse-image img)))
        (map %parse-item (xml-find-all channel 'item))
        (%extras channel %known-channel-tags)))))

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

(define (rss-read port) (rss-parse (%port->string port)))
(define (rss-load-file path) (call-with-input-file path rss-read))

;;; =========================================================================
;;; Writing
;;; =========================================================================

(define (%maybe-el tag value) (and value (xml-element tag value)))

;; Every `extras` entry (tag name string . text content string) is
;; written back out as its own child element, so nothing extract-and-
;; preserved by the reader is silently dropped by the writer.
(define (%extras->xml extras) (map (lambda (kv) (xml-element (string->symbol (car kv)) (cdr kv))) extras))

(define (%item->xml item)
  (make-xml-element 'item '()
    (append
      (filter %truthy?
        (list
          (%maybe-el 'title (rss-item-title item))
          (%maybe-el 'link (rss-item-link item))
          (%maybe-el 'description (rss-item-description item))
          (%maybe-el 'author (rss-item-author item))
          (and (rss-item-guid item) (xml-element 'guid (rss-item-guid item)))
          (%maybe-el 'pubDate (rss-item-pub-date item))
          (and (rss-item-enclosure item)
               (make-xml-element 'enclosure
                 (filter %truthy?
                   (list (let ((u (rss-enclosure-url (rss-item-enclosure item)))) (and u (cons "url" u)))
                         (let ((l (rss-enclosure-length (rss-item-enclosure item)))) (and l (cons "length" l)))
                         (let ((t (rss-enclosure-type (rss-item-enclosure item)))) (and t (cons "type" t)))))
                 '()))))
      (map (lambda (cat) (xml-element 'category cat)) (rss-item-categories item))
      (%extras->xml (rss-item-extras item)))))

(define (filter pred lst)
  (cond ((null? lst) '()) ((pred (car lst)) (cons (car lst) (filter pred (cdr lst)))) (else (filter pred (cdr lst)))))
(define (%truthy? x) x) ; used with filter to drop #f entries from a list

(define (%feed->xml feed)
  (make-xml-element 'rss (list (cons "version" "2.0"))
    (list
      (make-xml-element 'channel '()
        (append
          (filter %truthy?
            (list
              (%maybe-el 'title (rss-feed-title feed))
              (%maybe-el 'link (rss-feed-link feed))
              (%maybe-el 'description (rss-feed-description feed))
              (%maybe-el 'language (rss-feed-language feed))
              (%maybe-el 'pubDate (rss-feed-pub-date feed))
              (%maybe-el 'lastBuildDate (rss-feed-last-build-date feed))
              (%maybe-el 'generator (rss-feed-generator feed))
              (%maybe-el 'ttl (rss-feed-ttl feed))
              (and (rss-feed-image feed)
                   (make-xml-element 'image '()
                     (filter %truthy?
                       (list (%maybe-el 'url (rss-image-url (rss-feed-image feed)))
                             (%maybe-el 'title (rss-image-title (rss-feed-image feed)))
                             (%maybe-el 'link (rss-image-link (rss-feed-image feed)))))))))
          (%extras->xml (rss-feed-extras feed))
          (map %item->xml (rss-feed-items feed)))))))

;; (rss-write feed port) -> writes `feed` as RSS 2.0 XML directly to `port`.
(define (rss-write feed port) (xml-write (%feed->xml feed) port))

(define (rss-stringify feed)
  (let ((out (open-output-string)))
    (rss-write feed out)
    (get-output-string out)))

(define (rss-dump-file feed path) (call-with-output-file path (lambda (p) (rss-write feed p))))

  )) ;; end begin, define-library
