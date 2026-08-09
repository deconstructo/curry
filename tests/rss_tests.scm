;;; RSS module tests — (curry rss)

(import (curry rss))

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

(define sample "<?xml version=\"1.0\"?>
<rss version=\"2.0\">
  <channel>
    <title>Example Feed</title>
    <link>http://example.com/</link>
    <description>An example feed</description>
    <language>en-us</language>
    <pubDate>Mon, 06 Sep 2021 09:00:00 GMT</pubDate>
    <lastBuildDate>Tue, 07 Sep 2021 09:00:00 GMT</lastBuildDate>
    <generator>curry</generator>
    <ttl>60</ttl>
    <image>
      <url>http://example.com/logo.png</url>
      <title>Example Feed</title>
      <link>http://example.com/</link>
    </image>
    <copyright>2026 Example</copyright>
    <item>
      <title>First post</title>
      <link>http://example.com/1</link>
      <description>Hello &amp; welcome</description>
      <author>alice@example.com</author>
      <category>tech</category>
      <category>news</category>
      <guid>http://example.com/1</guid>
      <pubDate>Mon, 06 Sep 2021 09:00:00 GMT</pubDate>
      <enclosure url=\"http://example.com/a.mp3\" length=\"12345\" type=\"audio/mpeg\"/>
      <comments>http://example.com/1/comments</comments>
    </item>
    <item>
      <title>Second post</title>
      <link>http://example.com/2</link>
    </item>
  </channel>
</rss>")

(define feed (rss-parse sample))

;;; Channel-level fields

(check "feed title" (rss-feed-title feed) "Example Feed")
(check "feed link" (rss-feed-link feed) "http://example.com/")
(check "feed description" (rss-feed-description feed) "An example feed")
(check "feed language" (rss-feed-language feed) "en-us")
(check "feed pubDate kept as raw string" (rss-feed-pub-date feed) "Mon, 06 Sep 2021 09:00:00 GMT")
(check "feed lastBuildDate" (rss-feed-last-build-date feed) "Tue, 07 Sep 2021 09:00:00 GMT")
(check "feed generator" (rss-feed-generator feed) "curry")
(check "feed ttl" (rss-feed-ttl feed) "60")
(check "feed image url/title/link"
  (let ((img (rss-feed-image feed))) (list (rss-image-url img) (rss-image-title img) (rss-image-link img)))
  (list "http://example.com/logo.png" "Example Feed" "http://example.com/"))
(check "feed extras preserves unrecognized channel elements"
  (rss-feed-extras feed)
  '(("copyright" . "2026 Example")))
(check "feed has two items" (length (rss-feed-items feed)) 2)

;;; Item-level fields

(define item1 (car (rss-feed-items feed)))
(check "item title" (rss-item-title item1) "First post")
(check "item link" (rss-item-link item1) "http://example.com/1")
(check "item description entity-decoded" (rss-item-description item1) "Hello & welcome")
(check "item author" (rss-item-author item1) "alice@example.com")
(check "item categories, in order" (rss-item-categories item1) '("tech" "news"))
(check "item guid" (rss-item-guid item1) "http://example.com/1")
(check "item pubDate" (rss-item-pub-date item1) "Mon, 06 Sep 2021 09:00:00 GMT")
(check "item enclosure url/length/type"
  (let ((enc (rss-item-enclosure item1))) (list (rss-enclosure-url enc) (rss-enclosure-length enc) (rss-enclosure-type enc)))
  (list "http://example.com/a.mp3" "12345" "audio/mpeg"))
(check "item extras preserves unrecognized item elements"
  (rss-item-extras item1)
  '(("comments" . "http://example.com/1/comments")))

;; Second item: everything not present should be #f/empty, not error
(define item2 (cadr (rss-feed-items feed)))
(check "item with only title/link: description is #f" (rss-item-description item2) #f)
(check "item with only title/link: categories is empty" (rss-item-categories item2) '())
(check "item with only title/link: enclosure is #f" (rss-item-enclosure item2) #f)
(check "item with only title/link: extras is empty" (rss-item-extras item2) '())

;;; Errors

(check-error "non-<rss> root raises" (lambda () (rss-parse "<notrss/>")))
(check-error "<rss> with no <channel> raises" (lambda () (rss-parse "<rss version=\"2.0\"/>")))

;;; Writing / round-trip

(check "rss-stringify then rss-parse preserves title/link/description"
  (let ((feed2 (rss-parse (rss-stringify feed))))
    (list (rss-feed-title feed2) (rss-feed-link feed2) (rss-feed-description feed2)))
  (list "Example Feed" "http://example.com/" "An example feed"))

(check "rss-stringify then rss-parse preserves item categories"
  (rss-item-categories (car (rss-feed-items (rss-parse (rss-stringify feed)))))
  '("tech" "news"))

(check "rss-stringify then rss-parse preserves feed extras"
  (rss-feed-extras (rss-parse (rss-stringify feed)))
  '(("copyright" . "2026 Example")))

(check "rss-stringify then rss-parse preserves item extras"
  (rss-item-extras (car (rss-feed-items (rss-parse (rss-stringify feed)))))
  '(("comments" . "http://example.com/1/comments")))

(check "rss-stringify then rss-parse preserves enclosure"
  (let ((enc (rss-item-enclosure (car (rss-feed-items (rss-parse (rss-stringify feed)))))))
    (list (rss-enclosure-url enc) (rss-enclosure-length enc) (rss-enclosure-type enc)))
  (list "http://example.com/a.mp3" "12345" "audio/mpeg"))

(check "rss-stringify escapes special characters in description"
  (let ((f (make-rss-feed "T" "L" "D" #f #f #f #f #f #f
                           (list (make-rss-item "hi" #f "a & b" #f '() #f #f #f '()))
                           '())))
    (rss-item-description (car (rss-feed-items (rss-parse (rss-stringify f))))))
  "a & b")

;;; Minimal feed with no items

(check "feed with no items has an empty items list"
  (rss-feed-items (rss-parse "<rss version=\"2.0\"><channel><title>T</title><link>L</link><description>D</description></channel></rss>"))
  '())

;;; Port / file

(check "rss-read from string port" (rss-feed-title (rss-read (open-input-string sample))) "Example Feed")

(let ((path "/tmp/curry-rss-test.xml"))
  (rss-dump-file feed path)
  (check "rss-load-file round-trip" (rss-feed-title (rss-load-file path)) "Example Feed"))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
