;;; (curry xml) — a minimal XML reader/writer, pure Scheme.
;;;
;;; Not a general XML 1.0 processor — scoped to what well-formed feed
;;; documents (RSS, Atom) and similarly simple XML actually need: this
;;; module is (curry rss)/(curry atom)'s shared foundation, factored out
;;; the same way (curry http) is a shared dependency for (curry naips)/
;;; (curry airports)/(curry llm) rather than something each reimplements.
;;;
;;; An XML document parses to a single <xml-element> tree: each element
;;; has a tag (a symbol, kept exactly as written including any namespace
;;; prefix — see "Deliberately not supported" below), an attribute alist
;;; (string keys, string values), and a list of children, each either a
;;; nested <xml-element> or a plain string (a text/CDATA run).
;;;
;;; Supported: elements (nested, self-closing, or with text/mixed
;;; content), attributes (single- or double-quoted), CDATA sections
;;; (`<![CDATA[ ... ]]>`, kept as literal unescaped text), comments and
;;; processing instructions including the XML declaration (skipped), a
;;; DOCTYPE declaration (skipped), and the five predefined XML entities
;;; (`&amp; &lt; &gt; &quot; &apos;`) plus numeric character references
;;; (`&#NNN;`, `&#xHHHH;`).
;;;
;;; Deliberately not supported (see docs/reference/module-xml.md):
;;;
;;; - Namespace resolution. A tag or attribute written `content:encoded`
;;;   is read (and written) as the literal symbol/string
;;;   `content:encoded` — the colon is not treated specially, and no
;;;   `xmlns` declaration is resolved to a URI. This is the same
;;;   pragmatic simplification most minimal feed parsers make, since RSS/
;;;   Atom consumers overwhelmingly match on the literal prefixed name
;;;   anyway.
;;; - Named entities beyond the five XML predefines (e.g. HTML's `&nbsp;`)
;;;   are left in the text unchanged (`&nbsp;` stays `&nbsp;`) rather than
;;;   decoded or rejected — there is no DTD processing here to look them
;;;   up against.
;;; - External DTD subsets, internal DTD subsets with entity
;;;   declarations, and XInclude — a DOCTYPE line is recognized only
;;;   enough to be skipped past, never parsed.

(define-library (curry xml)
  (import (scheme base) (scheme char))
  (export
    xml-element make-xml-element xml-element? xml-tag xml-attrs xml-children
    xml-attr xml-element-text xml-find xml-find-all
    xml-parse xml-read xml-load-file
    xml-stringify xml-write xml-dump-file)
  (begin

;;; =========================================================================
;;; The element tree
;;; =========================================================================

(define-record-type <xml-element>
  (make-xml-element tag attrs children)
  xml-element?
  (tag      xml-tag)
  (attrs    xml-attrs)
  (children xml-children))

;; Convenience constructor with the common "no attrs" case defaulted, and
;; a var-arg children list instead of an explicit list argument — nicer
;; for hand-building a tree to write out (as (curry rss)/(curry atom)'s
;; own writers do).
(define (xml-element tag . rest)
  (if (and (pair? rest) (or (null? (car rest)) (pair? (car rest))) (or (null? (car rest)) (pair? (car (car rest)))))
      (make-xml-element tag (car rest) (cdr rest))
      (make-xml-element tag '() rest)))

(define (xml-attr el name) (let ((p (assoc name (xml-attrs el)))) (and p (cdr p))))

;; Concatenates every direct string child (text/CDATA runs) into one
;; string, skipping nested elements — "this element's own text content",
;; the common case for a leaf element like <title>some text</title>.
(define (xml-element-text el)
  (apply string-append (filter string? (xml-children el))))

(define (filter pred lst)
  (cond ((null? lst) '())
        ((pred (car lst)) (cons (car lst) (filter pred (cdr lst))))
        (else (filter pred (cdr lst)))))

;; The first direct child element named `tag`, or #f.
(define (xml-find el tag)
  (let loop ((cs (xml-children el)))
    (cond ((null? cs) #f)
          ((and (xml-element? (car cs)) (eq? (xml-tag (car cs)) tag)) (car cs))
          (else (loop (cdr cs))))))

;; Every direct child element named `tag`, in document order.
(define (xml-find-all el tag)
  (filter (lambda (c) (and (xml-element? c) (eq? (xml-tag c) tag))) (xml-children el)))

;;; =========================================================================
;;; Cursor (same imperative-recursive-descent convention (curry csv)/
;;; (curry toml) use)
;;; =========================================================================

(define (%mk-cursor s) (vector s 0 (string-length s)))
(define (%c-str c) (vector-ref c 0))
(define (%c-pos c) (vector-ref c 1))
(define (%c-len c) (vector-ref c 2))
(define (%c-set! c p) (vector-set! c 1 p))
(define (%c-eof? c) (>= (%c-pos c) (%c-len c)))
(define (%c-ch c) (if (%c-eof? c) #f (string-ref (%c-str c) (%c-pos c))))
(define (%c-adv! c) (%c-set! c (+ 1 (%c-pos c))))
(define (%c-adv-n! c n) (%c-set! c (+ n (%c-pos c))))

(define (%c-looking-at? c lit)
  (let ((n (string-length lit)) (len (%c-len c)) (pos (%c-pos c)) (s (%c-str c)))
    (and (<= (+ pos n) len)
         (let loop ((i 0)) (or (= i n) (and (char=? (string-ref s (+ pos i)) (string-ref lit i)) (loop (+ i 1))))))))

(define (%xml-error msg . irritants) (apply error (string-append "xml: " msg) irritants))

(define (%skip-ws! c) (let loop () (when (and (not (%c-eof? c)) (char-whitespace? (%c-ch c))) (%c-adv! c) (loop))))

;; Advances past whatever comes right up to (not including) the next `<`
;; that starts a real tag/comment/etc — used between markup, so leading
;; insignificant whitespace before the next `<` doesn't need its own
;; special-casing at every call site.
(define (%skip-to-lt! c) (%skip-ws! c))

;;; =========================================================================
;;; Entities
;;; =========================================================================

(define (%decode-entities s)
  (if (not (%c-looking-at-in? s #\&)) s (%decode-entities-slow s)))

(define (%c-looking-at-in? s ch) (%string-contains? s ch))
(define (%string-contains? s ch)
  (let loop ((i 0)) (and (< i (string-length s)) (or (char=? (string-ref s i) ch) (loop (+ i 1))))))

(define (%decode-entities-slow s)
  (let ((out (open-output-string)) (len (string-length s)))
    (let loop ((i 0))
      (if (>= i len)
          (get-output-string out)
          (if (and (char=? (string-ref s i) #\&) (%find-semicolon s i))
              (let* ((semi (%find-semicolon s i))
                     (name (substring s (+ i 1) semi)))
                (cond
                  ((string=? name "amp")  (write-char #\& out) (loop (+ semi 1)))
                  ((string=? name "lt")   (write-char #\< out) (loop (+ semi 1)))
                  ((string=? name "gt")   (write-char #\> out) (loop (+ semi 1)))
                  ((string=? name "quot") (write-char #\" out) (loop (+ semi 1)))
                  ((string=? name "apos") (write-char #\' out) (loop (+ semi 1)))
                  ((and (> (string-length name) 1) (char=? (string-ref name 0) #\#))
                   (let ((cp (%parse-char-ref name)))
                     (if cp
                         (begin (write-char (integer->char cp) out) (loop (+ semi 1)))
                         (begin (write-char (string-ref s i) out) (loop (+ i 1))))))
                  (else (write-char (string-ref s i) out) (loop (+ i 1)))))
              (begin (write-char (string-ref s i) out) (loop (+ i 1))))))))

;; Finds the `;` closing an entity reference starting at `&`'s index,
;; but only within a short lookahead window (entity names are always
;; short) — an unescaped bare `&` in real-world feed text (a genuine
;; XML violation, but common in the wild) shouldn't make this scan to
;; the end of the whole document looking for some unrelated `;`.
(define (%find-semicolon s start)
  (let ((len (string-length s)))
    (let loop ((i (+ start 1)) (n 0))
      (cond ((or (>= i len) (> n 32)) #f)
            ((char=? (string-ref s i) #\;) i)
            (else (loop (+ i 1) (+ n 1)))))))

(define (%parse-char-ref name)
  ;; name is "#NNN" or "#xHHHH" (the leading # already included)
  (let ((body (substring name 1 (string-length name))))
    (if (and (> (string-length body) 0) (memv (string-ref body 0) (list #\x #\X)))
        (string->number (substring body 1 (string-length body)) 16)
        (string->number body 10))))

;;; =========================================================================
;;; Parsing
;;; =========================================================================

(define (%name-char? ch) (and ch (not (char-whitespace? ch)) (not (memv ch (list #\< #\> #\/ #\= #\" #\' #\?)))))

(define (%parse-name! c)
  (let ((start (%c-pos c)))
    (let loop () (when (%name-char? (%c-ch c)) (%c-adv! c) (loop)))
    (if (= start (%c-pos c)) (%xml-error "expected a name" (%c-pos c)) (substring (%c-str c) start (%c-pos c)))))

;; Skips `<?...?>` (processing instructions, including the XML
;; declaration), `<!--...-->` (comments), and `<!DOCTYPE...>` (only
;; enough to find its closing `>`, ignoring any internal subset's own
;; nested `<...>` markup -- feeds essentially never have one, and this
;; module has no use for what it would say).
(define (%skip-markup! c)
  (cond
    ((%c-looking-at? c "<?")
     (%c-adv-n! c 2)
     (let loop () (cond ((%c-eof? c) (%xml-error "unterminated processing instruction"))
                         ((%c-looking-at? c "?>") (%c-adv-n! c 2))
                         (else (%c-adv! c) (loop)))))
    ((%c-looking-at? c "<!--")
     (%c-adv-n! c 4)
     (let loop () (cond ((%c-eof? c) (%xml-error "unterminated comment"))
                         ((%c-looking-at? c "-->") (%c-adv-n! c 3))
                         (else (%c-adv! c) (loop)))))
    ((%c-looking-at? c "<!")
     (%c-adv-n! c 2)
     (let loop () (cond ((%c-eof? c) (%xml-error "unterminated declaration"))
                         ((char=? (%c-ch c) #\>) (%c-adv! c))
                         (else (%c-adv! c) (loop)))))))

(define (%parse-attr-value! c)
  (let ((q (%c-ch c)))
    (%c-adv! c)
    (let ((start (%c-pos c)))
      (let loop () (cond ((%c-eof? c) (%xml-error "unterminated attribute value"))
                          ((char=? (%c-ch c) q) #t)
                          (else (%c-adv! c) (loop))))
      (let ((s (substring (%c-str c) start (%c-pos c))))
        (%c-adv! c) ; closing quote
        (%decode-entities s)))))

(define (%parse-attrs! c)
  (let loop ((acc '()))
    (%skip-ws! c)
    (if (or (%c-eof? c) (memv (%c-ch c) (list #\> #\/ #\?)))
        (reverse acc)
        (let ((name (%parse-name! c)))
          (%skip-ws! c)
          (unless (eqv? (%c-ch c) #\=) (%xml-error "expected = after attribute name" name))
          (%c-adv! c)
          (%skip-ws! c)
          (unless (memv (%c-ch c) (list #\" #\')) (%xml-error "expected quoted attribute value" name))
          (let ((val (%parse-attr-value! c)))
            (loop (cons (cons name val) acc)))))))

(define (%parse-cdata! c)
  (%c-adv-n! c 9) ; "<![CDATA["
  (let ((start (%c-pos c)))
    (let loop () (cond ((%c-eof? c) (%xml-error "unterminated CDATA section"))
                        ((%c-looking-at? c "]]>") #t)
                        (else (%c-adv! c) (loop))))
    (let ((s (substring (%c-str c) start (%c-pos c))))
      (%c-adv-n! c 3)
      s)))

;; Hard cap on element nesting depth. %parse-element!/%parse-content!
;; are mutually recursive with no other bound on how deep that
;; recursion goes — curry's tree-walking evaluator has no general stack-
;; depth guard of its own, so a maliciously (or just very badly)
;; deeply-nested document (trivial to construct: "<a>" x N + "</a>" x N)
;; would otherwise overflow the C stack and segfault the whole process
;; instead of raising a catchable error. 500 is generous for any
;; realistic feed document while still being far short of where a
;; crash was observed (confirmed safe well past this depth, crashing
;; only in the 600-700+ range).
(define %xml-max-depth 500)

;; Parses one element (cursor positioned at its opening "<"), returning
;; it. Handles self-closing (`<tag/>`) and normal (`<tag>...</tag>`)
;; forms, and any mix of text/CDATA/nested-element children in the
;; latter.
(define (%parse-element! c depth)
  (when (> depth %xml-max-depth) (%xml-error "element nesting too deep" %xml-max-depth))
  (%c-adv! c) ; "<"
  (let ((tag (%parse-name! c)) (attrs (begin (%skip-ws! c) (%parse-attrs! c))))
    (%skip-ws! c)
    (cond
      ((%c-looking-at? c "/>")
       (%c-adv-n! c 2)
       (make-xml-element (string->symbol tag) attrs '()))
      ((eqv? (%c-ch c) #\>)
       (%c-adv! c)
       (let ((children (%parse-content! c tag depth)))
         (make-xml-element (string->symbol tag) attrs children)))
      (else (%xml-error "malformed start tag" tag)))))

;; Parses the mixed content of an element up to (and consuming) its
;; matching `</tag>` close tag. `depth` is this element's own nesting
;; depth (already checked by %parse-element!); any child element parsed
;; here is one level deeper.
(define (%parse-content! c open-tag depth)
  (let loop ((acc '()))
    (cond
      ((%c-eof? c) (%xml-error "unterminated element" open-tag))
      ((%c-looking-at? c "</")
       (%c-adv-n! c 2)
       (let ((close-tag (%parse-name! c)))
         (unless (string=? close-tag open-tag)
           (%xml-error "mismatched close tag" open-tag close-tag))
         (%skip-ws! c)
         (unless (eqv? (%c-ch c) #\>) (%xml-error "malformed close tag" close-tag))
         (%c-adv! c)
         (reverse acc)))
      ((%c-looking-at? c "<![CDATA[") (loop (cons (%parse-cdata! c) acc)))
      ((or (%c-looking-at? c "<?") (%c-looking-at? c "<!--") (%c-looking-at? c "<!"))
       (%skip-markup! c) (loop acc))
      ((eqv? (%c-ch c) #\<) (loop (cons (%parse-element! c (+ depth 1)) acc)))
      (else
       (let ((start (%c-pos c)))
         (let text-loop () (when (and (not (%c-eof? c)) (not (eqv? (%c-ch c) #\<))) (%c-adv! c) (text-loop)))
         (loop (cons (%decode-entities (substring (%c-str c) start (%c-pos c))) acc)))))))

;; (xml-parse string) -> the document's single root <xml-element>,
;; skipping any leading XML declaration/comments/DOCTYPE.
(define (xml-parse str)
  (let ((c (%mk-cursor str)))
    (let loop ()
      (%skip-to-lt! c)
      (cond
        ((%c-eof? c) (%xml-error "no root element found"))
        ((or (%c-looking-at? c "<?") (%c-looking-at? c "<!--") (%c-looking-at? c "<!"))
         (%skip-markup! c) (loop))
        (else (%parse-element! c 0))))))

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

;; (xml-read port) -> parses a whole port's content as XML.
(define (xml-read port) (xml-parse (%port->string port)))

(define (xml-load-file path) (call-with-input-file path xml-read))

;;; =========================================================================
;;; Writing
;;; =========================================================================

(define (%write-escaped s out)
  (string-for-each
    (lambda (ch)
      (case ch
        ((#\&) (write-string "&amp;" out))
        ((#\<) (write-string "&lt;" out))
        ((#\>) (write-string "&gt;" out))
        (else (write-char ch out))))
    s))

(define (%write-attr-escaped s out)
  (string-for-each
    (lambda (ch)
      (case ch
        ((#\&) (write-string "&amp;" out))
        ((#\<) (write-string "&lt;" out))
        ((#\") (write-string "&quot;" out))
        (else (write-char ch out))))
    s))

(define (%tag->string tag) (if (symbol? tag) (symbol->string tag) tag))

(define (%write-element el out)
  (let ((tag (%tag->string (xml-tag el))))
    (write-char #\< out) (write-string tag out)
    (for-each
      (lambda (kv) (write-char #\space out) (write-string (car kv) out) (write-string "=\"" out)
              (%write-attr-escaped (cdr kv) out) (write-char #\" out))
      (xml-attrs el))
    (if (null? (xml-children el))
        (write-string "/>" out)
        (begin
          (write-char #\> out)
          (for-each
            (lambda (ch) (if (string? ch) (%write-escaped ch out) (%write-element ch out)))
            (xml-children el))
          (write-string "</" out) (write-string tag out) (write-char #\> out)))))

;; (xml-write el port) -> writes `el` as XML directly to `port`.
(define (xml-write el port) (%write-element el port))

(define (xml-stringify el)
  (let ((out (open-output-string)))
    (xml-write el out)
    (get-output-string out)))

(define (xml-dump-file el path) (call-with-output-file path (lambda (p) (xml-write el p))))

  )) ;; end begin, define-library
