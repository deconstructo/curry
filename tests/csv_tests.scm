;;; CSV module tests — (curry csv)

(import (curry csv))

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

(check "single field" (csv-parse "a") '(("a")))
(check "single row" (csv-parse "a,b,c") '(("a" "b" "c")))
(check "multiple rows, LF" (csv-parse "a,b\nc,d") '(("a" "b") ("c" "d")))
(check "multiple rows, CRLF" (csv-parse "a,b\r\nc,d") '(("a" "b") ("c" "d")))
(check "trailing newline optional" (csv-parse "a,b\n") '(("a" "b")))
(check "trailing newline absent" (csv-parse "a,b") '(("a" "b")))
(check "empty document" (csv-parse "") '())
(check "blank line is one empty field" (csv-parse "\n") '(("")))
(check "empty fields" (csv-parse "a,,c") '(("a" "" "c")))

;;; Quoting

(check "quoted field" (csv-parse "\"hello\",b") '(("hello" "b")))
(check "quoted field with embedded comma" (csv-parse "\"a,b\",c") '(("a,b" "c")))
(check "quoted field with embedded newline"
  (csv-parse "\"a\nb\",c")
  '(("a\nb" "c")))
(check "quoted field with embedded CRLF"
  (csv-parse "\"a\r\nb\",c")
  '(("a\r\nb" "c")))
(check "doubled-quote escape" (csv-parse "\"a\"\"b\",c") '(("a\"b" "c")))
(check "quoted field followed by more rows"
  (csv-parse "\"a,b\"\nc,d")
  '(("a,b") ("c" "d")))
(check "quoted empty field" (csv-parse "\"\",b") '(("" "b")))

;;; Custom delimiter

(check "tab delimiter" (csv-parse "a\tb\tc" #\tab) '(("a" "b" "c")))
(check "semicolon delimiter" (csv-parse "a;b;c" #\;) '(("a" "b" "c")))
(check "custom delimiter with quoting"
  (csv-parse "\"a;b\";c" #\;)
  '(("a;b" "c")))

;;; Header mode

(check "header mode basic"
  (csv-parse "name,age\nAlice,30\nBob,25" #\, #t)
  '((("name" . "Alice") ("age" . "30"))
    (("name" . "Bob") ("age" . "25"))))
(check "header mode, no data rows"
  (csv-parse "name,age" #\, #t)
  '())
(check "header mode, empty document"
  (csv-parse "" #\, #t)
  '())

;;; Port / file round-trip

(check "csv-read from string port"
  (csv-read (open-input-string "a,b\nc,d"))
  '(("a" "b") ("c" "d")))

(let ((path "/tmp/curry-csv-test.csv"))
  (csv-dump-file '(("a" "b") ("c" "d")) path)
  (check "csv-load-file round-trip" (csv-load-file path) '(("a" "b") ("c" "d"))))

;;; Writing

(check "csv-stringify basic" (csv-stringify '(("a" "b") ("c" "d"))) "a,b\r\nc,d\r\n")
(check "csv-stringify quotes field with comma"
  (csv-stringify '(("a,b" "c")))
  "\"a,b\",c\r\n")
(check "csv-stringify quotes field with embedded quote"
  (csv-stringify '(("a\"b" "c")))
  "\"a\"\"b\",c\r\n")
(check "csv-stringify quotes field with embedded newline"
  (csv-stringify '(("a\nb" "c")))
  "\"a\nb\",c\r\n")
(check "csv-stringify custom delimiter"
  (csv-stringify '(("a" "b")) #\;)
  "a;b\r\n")
(check "csv-stringify plain field with no special chars is not quoted"
  (csv-stringify '(("plain")))
  "plain\r\n")
(check "csv-stringify alist row writes values only"
  (csv-stringify (list (list (cons "name" "Alice") (cons "age" "30"))))
  "Alice,30\r\n")
(check-error "csv-write raises on a non-string field value"
  (lambda () (csv-stringify '((42 "b")))))

;;; Round-trip: parse then re-stringify should be stable for simple data

(check "round-trip simple grid"
  (csv-parse (csv-stringify '(("a" "b") ("c" "d"))))
  '(("a" "b") ("c" "d")))
(check "round-trip with quoted special chars"
  (csv-parse (csv-stringify '(("a,b" "c\"d" "e\nf"))))
  '(("a,b" "c\"d" "e\nf")))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
