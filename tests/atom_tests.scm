;;; Atom module tests — (curry atom)

(import (curry atom))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

(define sample "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<feed xmlns=\"http://www.w3.org/2005/Atom\">
  <title>Example Atom Feed</title>
  <id>urn:uuid:1</id>
  <updated>2026-08-09T12:00:00Z</updated>
  <subtitle>An example</subtitle>
  <generator>curry</generator>
  <rights>(c) 2026</rights>
  <icon>http://example.com/icon.png</icon>
  <logo>http://example.com/logo.png</logo>
  <author><name>Alice</name><email>alice@example.com</email><uri>http://example.com/alice</uri></author>
  <author><name>Carol</name></author>
  <link href=\"http://example.com/\" rel=\"alternate\" type=\"text/html\"/>
  <link href=\"http://example.com/feed.atom\" rel=\"self\"/>
  <category term=\"tech\"/>
  <category term=\"news\"/>
  <foo>bar</foo>
  <entry>
    <title>First entry</title>
    <id>urn:uuid:2</id>
    <updated>2026-08-09T12:00:00Z</updated>
    <published>2026-08-09T11:00:00Z</published>
    <summary>A summary &amp; more</summary>
    <content>Full content here</content>
    <rights>(c) 2026 entry</rights>
    <author><name>Bob</name></author>
    <link href=\"http://example.com/1\" rel=\"alternate\"/>
    <category term=\"news\"/>
    <extra-thing>preserved</extra-thing>
  </entry>
  <entry>
    <title>Second entry</title>
    <id>urn:uuid:3</id>
    <updated>2026-08-09T13:00:00Z</updated>
  </entry>
</feed>")

(define feed (atom-parse sample))

;;; Feed-level fields

(check "feed title" (atom-feed-title feed) "Example Atom Feed")
(check "feed id" (atom-feed-id feed) "urn:uuid:1")
(check "feed updated kept as raw string" (atom-feed-updated feed) "2026-08-09T12:00:00Z")
(check "feed subtitle" (atom-feed-subtitle feed) "An example")
(check "feed generator" (atom-feed-generator feed) "curry")
(check "feed rights" (atom-feed-rights feed) "(c) 2026")
(check "feed icon" (atom-feed-icon feed) "http://example.com/icon.png")
(check "feed logo" (atom-feed-logo feed) "http://example.com/logo.png")
(check "feed authors, in order"
  (map atom-person-name (atom-feed-authors feed))
  '("Alice" "Carol"))
(check "feed author has email/uri"
  (let ((a (car (atom-feed-authors feed)))) (list (atom-person-email a) (atom-person-uri a)))
  (list "alice@example.com" "http://example.com/alice"))
(check "feed author with only name has #f email/uri"
  (let ((a (cadr (atom-feed-authors feed)))) (list (atom-person-email a) (atom-person-uri a)))
  (list #f #f))
(check "feed links, in order, with rel/type"
  (map (lambda (l) (list (atom-link-href l) (atom-link-rel l) (atom-link-type l))) (atom-feed-links feed))
  (list (list "http://example.com/" "alternate" "text/html")
        (list "http://example.com/feed.atom" "self" #f)))
(check "feed categories, term only" (atom-feed-categories feed) '("tech" "news"))

;; Regression: a <category> missing its (RFC-4287-required, but
;; real-world-omittable) term attribute produced #f in the categories
;; list; round-tripping that back through atom-stringify then crashed
;; the whole process (%category->xml wrote #f as an attribute value,
;; and the underlying attribute-escaping code segfaulted on a non-
;; string value) rather than just dropping the malformed category.
(check "a <category> with no term attribute is dropped, not kept as #f"
  (atom-feed-categories (atom-parse "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>T</title><id>I</id><updated>U</updated><category/></feed>"))
  '())
(check "atom-stringify doesn't crash on a feed that had a termless category"
  (atom-stringify (atom-parse "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>T</title><id>I</id><updated>U</updated><category/></feed>"))
  "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>T</title><id>I</id><updated>U</updated></feed>")
(check "feed extras preserves unrecognized elements" (atom-feed-extras feed) '(("foo" . "bar")))
(check "feed has two entries" (length (atom-feed-entries feed)) 2)

;;; Entry-level fields

(define entry1 (car (atom-feed-entries feed)))
(check "entry title" (atom-entry-title entry1) "First entry")
(check "entry id" (atom-entry-id entry1) "urn:uuid:2")
(check "entry updated" (atom-entry-updated entry1) "2026-08-09T12:00:00Z")
(check "entry published" (atom-entry-published entry1) "2026-08-09T11:00:00Z")
(check "entry summary entity-decoded" (atom-entry-summary entry1) "A summary & more")
(check "entry content" (atom-entry-content entry1) "Full content here")
(check "entry rights" (atom-entry-rights entry1) "(c) 2026 entry")
(check "entry authors" (map atom-person-name (atom-entry-authors entry1)) '("Bob"))
(check "entry links" (map atom-link-href (atom-entry-links entry1)) '("http://example.com/1"))
(check "entry categories" (atom-entry-categories entry1) '("news"))
(check "entry extras preserves unrecognized elements"
  (atom-entry-extras entry1)
  '(("extra-thing" . "preserved")))

;; Second entry: everything not present should be #f/empty, not error
(define entry2 (cadr (atom-feed-entries feed)))
(check "entry with only title/id/updated: published is #f" (atom-entry-published entry2) #f)
(check "entry with only title/id/updated: summary is #f" (atom-entry-summary entry2) #f)
(check "entry with only title/id/updated: authors is empty" (atom-entry-authors entry2) '())
(check "entry with only title/id/updated: links is empty" (atom-entry-links entry2) '())
(check "entry with only title/id/updated: categories is empty" (atom-entry-categories entry2) '())

;;; Errors

(check-error "non-<feed> root raises" (lambda () (atom-parse "<notfeed/>")))

;;; Writing / round-trip

(check "atom-stringify then atom-parse preserves title/id/updated"
  (let ((feed2 (atom-parse (atom-stringify feed))))
    (list (atom-feed-title feed2) (atom-feed-id feed2) (atom-feed-updated feed2)))
  (list "Example Atom Feed" "urn:uuid:1" "2026-08-09T12:00:00Z"))

(check "atom-stringify then atom-parse preserves authors"
  (map atom-person-name (atom-feed-authors (atom-parse (atom-stringify feed))))
  '("Alice" "Carol"))

(check "atom-stringify then atom-parse preserves links with rel/type"
  (map (lambda (l) (list (atom-link-href l) (atom-link-rel l) (atom-link-type l)))
       (atom-feed-links (atom-parse (atom-stringify feed))))
  (list (list "http://example.com/" "alternate" "text/html")
        (list "http://example.com/feed.atom" "self" #f)))

(check "atom-stringify then atom-parse preserves categories"
  (atom-feed-categories (atom-parse (atom-stringify feed)))
  '("tech" "news"))

(check "atom-stringify then atom-parse preserves feed extras"
  (atom-feed-extras (atom-parse (atom-stringify feed)))
  '(("foo" . "bar")))

(check "atom-stringify then atom-parse preserves entry extras"
  (atom-entry-extras (car (atom-feed-entries (atom-parse (atom-stringify feed)))))
  '(("extra-thing" . "preserved")))

(check "atom-stringify escapes special characters in summary"
  (let ((f (make-atom-feed "T" "I" "U" #f #f #f #f #f '() '() '()
                            (list (make-atom-entry "hi" "i2" "u2" #f "a & b" #f #f '() '() '() '()))
                            '())))
    (atom-entry-summary (car (atom-feed-entries (atom-parse (atom-stringify f))))))
  "a & b")

;;; Minimal feed with no entries

(check "feed with no entries has an empty entries list"
  (atom-feed-entries (atom-parse "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>T</title><id>I</id><updated>U</updated></feed>"))
  '())

;;; Port / file

(check "atom-read from string port" (atom-feed-title (atom-read (open-input-string sample))) "Example Atom Feed")

(let ((path "/tmp/curry-atom-test.xml"))
  (atom-dump-file feed path)
  (check "atom-load-file round-trip" (atom-feed-title (atom-load-file path)) "Example Atom Feed"))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
