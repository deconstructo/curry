;;; XML module tests — (curry xml)

(import (curry xml))

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

;;; Basic parsing

(check "simple element" (xml-tag (xml-parse "<root/>")) 'root)
(check "self-closing element has no children" (xml-children (xml-parse "<root/>")) '())
(check "element with text content" (xml-element-text (xml-parse "<root>hello</root>")) "hello")
(check "element with attribute" (xml-attr (xml-parse "<root a=\"1\"/>") "a") "1")
(check "element with multiple attributes"
  (xml-attrs (xml-parse "<root a=\"1\" b=\"2\"/>"))
  '(("a" . "1") ("b" . "2")))
(check "single-quoted attribute" (xml-attr (xml-parse "<root a='1'/>") "a") "1")
(check "nested elements"
  (xml-tag (xml-find (xml-parse "<root><child/></root>") 'child))
  'child)
(check "xml-find-all returns every matching child in order"
  (map xml-element-text (xml-find-all (xml-parse "<r><a>1</a><a>2</a><a>3</a></r>") 'a))
  '("1" "2" "3"))
(check "xml-find returns #f when no matching child"
  (xml-find (xml-parse "<root/>") 'missing)
  #f)

;;; Prolog / comments / DOCTYPE

(check "XML declaration is skipped"
  (xml-tag (xml-parse "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>"))
  'root)
(check "comment before root is skipped"
  (xml-tag (xml-parse "<!-- a comment --><root/>"))
  'root)
(check "comment inside content is skipped"
  (xml-element-text (xml-parse "<root>a<!-- comment -->b</root>"))
  "ab")
(check "DOCTYPE is skipped"
  (xml-tag (xml-parse "<!DOCTYPE root SYSTEM \"root.dtd\"><root/>"))
  'root)

;;; Entities

(check "predefined entities decoded"
  (xml-element-text (xml-parse "<r>&amp; &lt; &gt; &quot; &apos;</r>"))
  "& < > \" '")
(check "decimal numeric character reference"
  (xml-element-text (xml-parse "<r>caf&#233;</r>"))
  "café")
(check "hex numeric character reference"
  (xml-element-text (xml-parse "<r>&#x65;nd</r>"))
  "end")
(check "unrecognized named entity passed through unchanged"
  (xml-element-text (xml-parse "<r>a&nbsp;b</r>"))
  "a&nbsp;b")
(check "entities decoded in attribute values"
  (xml-attr (xml-parse "<r a=\"1 &amp; 2\"/>") "a")
  "1 & 2")

;;; CDATA

(check "CDATA content is kept literally, not entity-decoded"
  (xml-element-text (xml-parse "<r><![CDATA[<not-a-tag> &amp;]]></r>"))
  "<not-a-tag> &amp;")
(check "CDATA round-trips through stringify/re-parse with the same text content"
  (let ((doc (xml-parse "<r><![CDATA[a & b <c>]]></r>")))
    (xml-element-text (xml-parse (xml-stringify doc))))
  "a & b <c>")

;; Regression: %parse-element!/%parse-content! had no nesting-depth
;; limit, so a deeply-nested document (trivially constructible from
;; untrusted feed input) overflowed the C stack and segfaulted the
;; whole process instead of raising a catchable error.
(check-error "deeply nested elements raise cleanly instead of crashing"
  (lambda ()
    (let* ((n 5000)
           (open (let loop ((i 0) (acc "")) (if (= i n) acc (loop (+ i 1) (string-append acc "<a>")))))
           (close (let loop ((i 0) (acc "")) (if (= i n) acc (loop (+ i 1) (string-append acc "</a>"))))))
      (xml-parse (string-append open "x" close)))))

;;; Mixed content

(check "mixed text and element children preserve order"
  (let ((doc (xml-parse "<r>a<b/>c</r>")))
    (list (car (xml-children doc)) (xml-tag (cadr (xml-children doc))) (caddr (xml-children doc))))
  (list "a" 'b "c"))

;;; Namespaced (prefixed) tag/attribute names kept literal

(check "namespaced tag name kept as one literal symbol"
  (xml-tag (xml-find (xml-parse "<r><content:encoded>x</content:encoded></r>") 'content:encoded))
  'content:encoded)

;;; Errors

(check-error "mismatched close tag raises" (lambda () (xml-parse "<a><b></a></b>")))
(check-error "unterminated element raises" (lambda () (xml-parse "<a><b></a>")))
(check-error "non-xml input raises" (lambda () (xml-parse "not xml at all")))
(check-error "empty input raises" (lambda () (xml-parse "")))

;;; Writing

(check "xml-stringify basic element" (xml-stringify (xml-element 'root)) "<root/>")
(check "xml-stringify with attrs"
  (xml-stringify (make-xml-element 'root '(("a" . "1")) '()))
  "<root a=\"1\"/>")
(check "xml-stringify with text child" (xml-stringify (xml-element 'root "hello")) "<root>hello</root>")
(check "xml-stringify escapes special characters in text"
  (xml-stringify (xml-element 'root "a & b < c"))
  "<root>a &amp; b &lt; c</root>")
(check "xml-stringify escapes special characters in attribute values"
  (xml-stringify (make-xml-element 'root (list (cons "a" "1 & \"2\"")) '()))
  "<root a=\"1 &amp; &quot;2&quot;\"/>")
(check "xml-stringify with nested element child"
  (xml-stringify (xml-element 'root (xml-element 'child "x")))
  "<root><child>x</child></root>")

;;; Round-trip

(check "parse then stringify then parse produces the same tag/text"
  (let* ((src "<root a=\"1\"><child>hi &amp; bye</child></root>")
         (doc1 (xml-parse src))
         (doc2 (xml-parse (xml-stringify doc1))))
    (list (xml-tag doc2) (xml-attr doc2 "a") (xml-element-text (xml-find doc2 'child))))
  (list 'root "1" "hi & bye"))

;;; Port / file

(check "xml-read from string port" (xml-tag (xml-read (open-input-string "<root/>"))) 'root)

(let ((path "/tmp/curry-xml-test.xml"))
  (xml-dump-file (xml-element 'root "hi") path)
  (check "xml-load-file round-trip" (xml-element-text (xml-load-file path)) "hi"))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
