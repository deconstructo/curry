;;; TOML module tests — (curry toml)

(import (curry toml))

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

;;; Basic key/value, scalar types

(check "string value" (toml-parse "s = \"hello\"") '(("s" . "hello")))
(check "integer value" (toml-parse "n = 42") '(("n" . 42)))
(check "negative integer" (toml-parse "n = -17") '(("n" . -17)))
(check "float value" (toml-parse "f = 3.14") '(("f" . 3.14)))
(check "float with exponent" (toml-parse "f = 6.626e-34") '(("f" . 6.626e-34)))
(check "positive sign integer" (toml-parse "n = +99") '(("n" . 99)))
(check "true" (toml-parse "b = true") '(("b" . #t)))
(check "false" (toml-parse "b = false") '(("b" . #f)))
(check "hex integer" (toml-parse "n = 0xDEADBEEF") '(("n" . 3735928559)))
(check "octal integer" (toml-parse "n = 0o755") '(("n" . 493)))
(check "binary integer" (toml-parse "n = 0b1010") '(("n" . 10)))
(check "underscored integer" (toml-parse "n = 1_000_000") '(("n" . 1000000)))
(check "underscored float" (toml-parse "f = 1_234.567_8") '(("f" . 1234.5678)))
(check "inf" (toml-parse "f = inf") '(("f" . +inf.0)))
(check "neg inf" (toml-parse "f = -inf") '(("f" . -inf.0)))
(check "nan" (nan? (cdr (car (toml-parse "f = nan")))) #t)

;;; Strings

(check "literal string, no escapes" (toml-parse "s = 'C:\\Users\\nodejs'") '(("s" . "C:\\Users\\nodejs")))
(check "basic string escapes" (toml-parse "s = \"a\\nb\\tc\"") '(("s" . "a\nb\tc")))
(check "basic string quote escape" (toml-parse "s = \"a\\\"b\"") '(("s" . "a\"b")))
(check "multi-line basic string" (toml-parse "s = \"\"\"\nRoses are red\"\"\"") '(("s" . "Roses are red")))
(check "multi-line basic string line-continuation"
       (toml-parse "s = \"\"\"The quick brown \\\n  fox\"\"\"")
       '(("s" . "The quick brown fox")))
(check "multi-line literal string" (toml-parse "s = '''\nliteral\ntext'''") '(("s" . "literal\ntext")))
(check "unicode escape \\u" (toml-parse "s = \"\\u00e9\"") '(("s" . "\x00e9;")))

;;; Keys

(check "quoted key" (toml-parse "\"a b\" = 1") '(("a b" . 1)))
(check "literal-quoted key" (toml-parse "'a.b' = 1") '(("a.b" . 1)))
(check "dotted key creates nested table"
       (toml-parse "name.first = \"Tom\"\nname.last = \"Preston\"")
       '(("name" ("first" . "Tom") ("last" . "Preston"))))
(check "key with underscore and digits" (toml-parse "key_1 = 1") '(("key_1" . 1)))

;;; Arrays

(check "simple array" (toml-parse "a = [1, 2, 3]") '(("a" 1 2 3)))
(check "nested array" (toml-parse "a = [[1, 2], [3, 4]]") '(("a" (1 2) (3 4))))
(check "heterogeneous array" (toml-parse "a = [1, \"two\", 3.0]") '(("a" 1 "two" 3.0)))
(check "multi-line array with trailing comma"
       (toml-parse "a = [\n  1,\n  2,\n  3,\n]")
       '(("a" 1 2 3)))
(check "empty array" (toml-parse "a = []") '(("a")))

;;; Inline tables

(check "inline table" (toml-parse "p = { x = 1, y = 2 }") '(("p" ("x" . 1) ("y" . 2))))
(check "nested inline table" (toml-parse "p = { a = { b = 1 } }") '(("p" ("a" ("b" . 1)))))
(check "array of inline tables"
       (toml-parse "a = [{ x = 1 }, { x = 2 }]")
       '(("a" (("x" . 1)) (("x" . 2)))))

;;; Standard tables

(check "simple table" (toml-parse "[a]\nx = 1") '(("a" ("x" . 1))))
(check "dotted table header"
       (toml-parse "[a.b.c]\nx = 1")
       '(("a" ("b" ("c" ("x" . 1))))))
(check "multiple tables"
       (toml-parse "[a]\nx = 1\n\n[b]\ny = 2")
       '(("a" ("x" . 1)) ("b" ("y" . 2))))
(check "table then dotted sub-table reopens correctly"
       (toml-parse "[servers]\n\n[servers.alpha]\nip = \"10.0.0.1\"")
       '(("servers" ("alpha" ("ip" . "10.0.0.1")))))

;;; Arrays of tables

(check "simple array of tables"
       (toml-parse "[[fruits]]\nname = \"apple\"\n\n[[fruits]]\nname = \"banana\"")
       '(("fruits" (("name" . "apple")) (("name" . "banana")))))
(check "array of tables with sub-table on last element"
       (toml-parse "[[fruits]]\nname = \"apple\"\n\n[fruits.physical]\ncolor = \"red\"")
       '(("fruits" (("name" . "apple") ("physical" ("color" . "red"))))))
(check "array of tables with nested array of tables"
       (toml-parse (string-append
                      "[[fruits]]\nname = \"apple\"\n\n"
                      "[[fruits.varieties]]\nname = \"red delicious\"\n\n"
                      "[[fruits.varieties]]\nname = \"granny smith\"\n\n"
                      "[[fruits]]\nname = \"banana\"\n\n"
                      "[[fruits.varieties]]\nname = \"plantain\""))
       '(("fruits"
          (("name" . "apple") ("varieties" (("name" . "red delicious")) (("name" . "granny smith"))))
          (("name" . "banana") ("varieties" (("name" . "plantain")))))))

;;; Comments and whitespace

(check "comment ignored" (toml-parse "# a comment\nx = 1") '(("x" . 1)))
(check "trailing comment" (toml-parse "x = 1 # trailing") '(("x" . 1)))
(check "blank lines ignored" (toml-parse "x = 1\n\n\ny = 2") '(("x" . 1) ("y" . 2)))

;;; Datetimes

(let ((d (cdr (car (toml-parse "d = 1979-05-27T07:32:00-08:00")))))
  (check "offset date-time is a toml-datetime" (toml-datetime? d) #t)
  (check "offset date-time text preserved" (toml-datetime->string d) "1979-05-27T07:32:00-08:00"))
(let ((d (cdr (car (toml-parse "d = 1979-05-27T07:32:00")))))
  (check "local date-time text preserved" (toml-datetime->string d) "1979-05-27T07:32:00"))
(let ((d (cdr (car (toml-parse "d = 1979-05-27")))))
  (check "local date text preserved" (toml-datetime->string d) "1979-05-27"))
(let ((d (cdr (car (toml-parse "d = 07:32:00")))))
  (check "local time text preserved" (toml-datetime->string d) "07:32:00"))

;;; Writing

(check "stringify simple table" (toml-stringify '(("a" . 1) ("b" . "hi"))) "a = 1\nb = \"hi\"\n")
(check "stringify nested table"
       (toml-stringify '(("owner" ("name" . "Tom"))))
       "\n[owner]\nname = \"Tom\"\n")
(check "stringify array" (toml-stringify '(("a" 1 2 3))) "a = [1, 2, 3]\n")
(check "stringify float always has a decimal point" (toml-stringify '(("f" . 1.0))) "f = 1.0\n")
(check "stringify datetime unquoted"
       (toml-stringify (list (cons "d" (toml-datetime "1979-05-27"))))
       "d = 1979-05-27\n")
(check "stringify array of tables uses [[...]] headers"
       (toml-stringify '(("fruits" (("name" . "apple")) (("name" . "banana")))))
       "\n[[fruits]]\nname = \"apple\"\n\n[[fruits]]\nname = \"banana\"\n")
(check "stringify a table mixed into a plain array uses inline { }, not a header"
       (toml-stringify (list (cons "mixed" (list (list (cons "x" 1)) 2 3))))
       "mixed = [{x = 1}, 2, 3]\n")

;;; Round-trip

(define (norm v)
  (cond
    ((toml-datetime? v) (list 'dt (toml-datetime->string v)))
    ((and (pair? v) (pair? (car v)) (string? (caar v)))
     (map (lambda (kv) (cons (car kv) (norm (cdr kv)))) v))
    ((list? v) (map norm v))
    (else v)))

(let* ((doc (list (cons "title" "Example")
                  (cons "owner" (list (cons "name" "Tom")))
                  (cons "database" (list (cons "ports" (list 1 2 3))))
                  (cons "fruits" (list (list (cons "name" "apple")
                                              (cons "varieties" (list (list (cons "name" "red delicious"))))))))))
  (check "round-trip: parse . stringify . parse == original"
         (norm (toml-parse (toml-stringify doc)))
         (norm doc)))

;;; Errors

(check "missing = raises" (guard (e (#t 'raised)) (toml-parse "x 1")) 'raised)
(check "unterminated string raises" (guard (e (#t 'raised)) (toml-parse "s = \"unterminated")) 'raised)
(check "redefining a key as a table raises"
       (guard (e (#t 'raised)) (toml-parse "a = 1\n[a]\nb = 2"))
       'raised)
(check "toml-stringify on a non-table raises" (guard (e (#t 'raised)) (toml-stringify 5)) 'raised)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
