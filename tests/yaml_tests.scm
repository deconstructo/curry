;;; YAML module tests — (curry yaml)

(import (curry yaml))

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

;;; Scalars

(check "plain string" (yaml-parse "hello") "hello")
(check "integer" (yaml-parse "42") 42)
(check "negative integer" (yaml-parse "-5") -5)
(check "float" (yaml-parse "3.14") 3.14)
(check "exponent float without dot" (yaml-parse "1e10") 1e10)
(check "hex int" (yaml-parse "0x1A") 26)
(check "octal int" (yaml-parse "0o17") 15)
(check "underscore int" (yaml-parse "1_000_000") 1000000)
(check "true" (yaml-parse "true") #t)
(check "yes as bool" (yaml-parse "yes") #t)
(check "false" (yaml-parse "false") #f)
(check "no as bool" (yaml-parse "no") #f)
(check "null tilde" (yaml-null? (yaml-parse "~")) #t)
(check "null word" (yaml-null? (yaml-parse "null")) #t)
(check "empty is null" (yaml-null? (yaml-parse "")) #t)
(check "inf" (yaml-parse ".inf") +inf.0)
(check "neg inf" (yaml-parse "-.inf") -inf.0)
(check "nan is nan" (nan? (yaml-parse ".nan")) #t)
(check "single-quoted" (yaml-parse "'a''b'") "a'b")
(check "double-quoted escapes" (yaml-parse "\"a\\nb\\t\\\"c\\\"\"") "a\nb\t\"c\"")
(check "double-quoted hex escape" (yaml-parse "\"\\x41\"") "A")

;;; Block collections

(check "block mapping"
       (yaml-parse "name: Alice\nage: 30")
       '(("name" . "Alice") ("age" . 30)))

(check "block sequence"
       (yaml-parse "- a\n- b\n- c")
       '("a" "b" "c"))

(check "nested mapping"
       (yaml-parse "outer:\n  inner: 1\n  other: 2")
       '(("outer" ("inner" . 1) ("other" . 2))))

(check "sequence of mappings (compact form)"
       (yaml-parse "- name: a\n  x: 1\n- name: b\n  x: 2")
       '((("name" . "a") ("x" . 1)) (("name" . "b") ("x" . 2))))

(check "mapping of sequence"
       (yaml-parse "tags:\n  - x\n  - y")
       '(("tags" "x" "y")))

(check "plain scalar folding across continuation lines"
       (yaml-parse "key: first\n  second\n  third")
       '(("key" . "first second third")))

(check "colon inside value not mistaken for mapping"
       (yaml-parse "url: http://example.com:8080/path")
       '(("url" . "http://example.com:8080/path")))

(check "comment stripped"
       (yaml-parse "# leading comment\nkey: value # trailing")
       '(("key" . "value")))

;;; Flow collections

(check "flow sequence" (yaml-parse "[1, 2, 3]") '(1 2 3))
(check "flow mapping" (yaml-parse "{a: 1, b: 2}") '(("a" . 1) ("b" . 2)))
(check "nested flow"
       (yaml-parse "[1, [2, 3], {a: b}]")
       '(1 (2 3) (("a" . "b"))))
(check "flow trailing comma" (yaml-parse "[1, 2, 3,]") '(1 2 3))

;;; Block scalars

(check "literal block scalar clip chomping"
       (yaml-parse "a: |\n  line one\n  line two")
       '(("a" . "line one\nline two\n")))

(check "folded block scalar"
       (yaml-parse "a: >\n  this is\n  folded")
       '(("a" . "this is folded\n")))

(check "literal block scalar strip chomping"
       (yaml-parse "a: |-\n  text\n\n\nb: 2")
       '(("a" . "text") ("b" . 2)))

(check "literal block scalar keep chomping"
       (yaml-parse "a: |+\n  text\n\n\nb: 2")
       '(("a" . "text\n\n\n") ("b" . 2)))

;;; Anchors, aliases, merge keys

(check "anchor and alias on scalar"
       (yaml-parse "x: &n 5\ny: *n")
       '(("x" . 5) ("y" . 5)))

(check "anchor and alias on mapping"
       (yaml-parse "base: &b\n  a: 1\n  b: 2\nother: *b")
       '(("base" ("a" . 1) ("b" . 2)) ("other" ("a" . 1) ("b" . 2))))

(check "merge key"
       (yaml-parse "base: &b\n  a: 1\n  b: 2\nderived:\n  <<: *b\n  c: 3")
       '(("base" ("a" . 1) ("b" . 2))
         ("derived" ("c" . 3) ("a" . 1) ("b" . 2))))

(check "merge key does not override own keys"
       (yaml-parse "base: &b\n  a: 1\nderived:\n  <<: *b\n  a: 99")
       '(("base" ("a" . 1)) ("derived" ("a" . 99))))

;;; Explicit tags

(check "!!str forces string over numeric-looking text"
       (yaml-parse "!!str 123")
       "123")

(check "!!int forces int"
       (yaml-parse "!!int \"42\"")
       42)

(check "unrecognized tag ignored, implicit typing still applies"
       (yaml-parse "!custom 5")
       5)

;;; Multi-document streams

(check "multi-document stream"
       (yaml-parse-all "---\na: 1\n---\nb: 2\n")
       '((("a" . 1)) (("b" . 2))))

(check "single document via yaml-parse takes the first"
       (yaml-parse "---\na: 1\n---\nb: 2\n")
       '(("a" . 1)))

;;; Writer

(check "stringify then reparse round-trips a nested structure"
       (let ((v '(("name" . "Alice")
                  ("age" . 30)
                  ("active" . #t)
                  ("tags" "x" "y")
                  ("nested" ("a" . 1) ("b" . (2 3))))))
         (equal? v (yaml-parse (yaml-stringify v))))
       #t)

(check "stringify empty list as inline []"
       (yaml-stringify '(("empty" . ())))
       "empty: []\n")

(check "stringify a string that looks numeric quotes it"
       (yaml-parse (yaml-stringify "8080"))
       "8080")

(check "stringify null"
       (yaml-stringify yaml-null)
       "null\n")

(check "stringify a multi-line string as a literal block, reparses correctly"
       (yaml-parse (yaml-stringify "line one\nline two"))
       "line one\nline two")

;;; Regressions found by independent review

(check "stringify a sequence of mappings does not misclassify as one big mapping"
       (yaml-stringify '((("name" . "a") ("x" . 1)) (("name" . "b") ("x" . 2))))
       "- \n  name: a\n  x: 1\n- \n  name: b\n  x: 2\n")

(check "sequence of mappings round-trips through stringify+parse"
       (let ((v '((("name" . "a") ("x" . 1)) (("name" . "b") ("x" . 2)))))
         (equal? v (yaml-parse (yaml-stringify v))))
       #t)

(check "block scalar as a bare sequence item"
       (yaml-parse "- |\n  x\n  y")
       '("x\ny\n"))

(check "block scalar as a sequence item nested in a mapping (k8s command: idiom)"
       (yaml-parse "items:\n  - |\n    x\n    y\n  - z")
       '(("items" "x\ny\n" "z")))

(check "multi-source merge key flattens and dedupes with first-source-wins"
       (yaml-parse "a: &a {x: 1, y: 2}\nb: &b {y: 20, z: 3}\nc: {<<: [*a, *b], w: 4}")
       '(("a" ("x" . 1) ("y" . 2))
         ("b" ("y" . 20) ("z" . 3))
         ("c" ("w" . 4) ("x" . 1) ("y" . 2) ("z" . 3))))

(check "sequence item whose value is a nested mapping containing a nested sequence"
       (let ((v '(("a" ("b" ("c" 1 2 (("d" . 3) ("e" 4 5))))))))
         (equal? v (yaml-parse (yaml-stringify v))))
       #t)

(check "root-level multi-line plain scalar folds instead of truncating"
       (yaml-parse "first line\nsecond line")
       "first line second line")

(check "root-level multi-line plain scalar does not fabricate extra documents"
       (yaml-parse-all "first line\nsecond line")
       '("first line second line"))

;;; File convenience wrappers

(define tmp-path (string-append "/tmp/curry-yaml-test-" (number->string (current-jiffy))))
(yaml-dump-file '(("hello" . "world") ("n" . 5)) tmp-path)
(check "yaml-load-file round-trips yaml-dump-file"
       (yaml-load-file tmp-path)
       '(("hello" . "world") ("n" . 5)))
(delete-file tmp-path)

;;; Port-level API (yaml-read/yaml-read-all/yaml-write)

(check "yaml-read matches yaml-parse, given a port"
       (yaml-read (open-input-string "hello: world\nn: 5"))
       (yaml-parse "hello: world\nn: 5"))

(check "yaml-read-all matches yaml-parse-all, given a port"
       (yaml-read-all (open-input-string "a: 1\n---\nb: 2"))
       (yaml-parse-all "a: 1\n---\nb: 2"))

(check "yaml-write matches yaml-stringify, given a port"
       (let ((out (open-output-string)))
         (yaml-write '(("hello" . "world") ("n" . 5)) out)
         (get-output-string out))
       (yaml-stringify '(("hello" . "world") ("n" . 5))))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
