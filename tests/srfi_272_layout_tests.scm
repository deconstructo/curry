;;; SRFI-272 layout tests: the style-driven layout engine, read-macro
;;; abbreviation, line/level/length limits, and terminal-column widths for
;;; Unicode text (CJK, emoji, combining marks, cuneiform and Akkadian
;;; transliteration). Expected strings were checked by hand against the
;;; SRFI's style grammar and UAX #11.
;;; https://srfi.schemers.org/srfi-272/

(import (scheme base) (scheme write)
        (srfi 272 advanced) (srfi 272 colorize))

(define pass-count 0)
(define fail-count 0)

(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin
        (set! fail-count (+ fail-count 1))
        (display "FAIL: ") (display name)
        (display " expected=") (write expected)
        (display " actual=") (write actual) (newline))))

(define (out obj . kv)
  (let ((p (open-output-string)))
    (apply pp obj p kv)
    (get-output-string p)))

(check "define body indent"
  "(define (add-all xs)\n  (fold-left + 0 xs)\n  (display \"done\"))\n"
  (out '(define (add-all xs) (fold-left + 0 xs) (display "done")) 'pp-width 30))
(check "named let keeps bindings on head line"
  "(let loop ((i 0) (acc '()))\n  (if\n    (> i 10)\n    acc\n    (loop (+ i 1) (cons i acc))))\n"
  (out '(let loop ((i 0) (acc '())) (if (> i 10) acc (loop (+ i 1) (cons i acc)))) 'pp-width 40))
(check "cond clauses align under the first"
  "(cond ((> x 10) 'big)\n      ((> x 5) 'medium)\n      (else 'small))\n"
  (out '(cond ((> x 10) 'big) ((> x 5) 'medium) (else 'small)) 'pp-width 30))
(check "case clauses indent as a body"
  "(case x\n  ((1) 'a)\n  (else 'b))\n"
  (out '(case x ((1) 'a) (else 'b)) 'pp-width 15))
(check "do lines its test clause up under the bindings"
  "(do ((i 0 (+ i 1))\n     (acc '() (cons i acc)))\n    ((= i 10) acc)\n  (display i))\n"
  (out '(do ((i 0 (+ i 1)) (acc '() (cons i acc))) ((= i 10) acc) (display i)) 'pp-width 40))
(check "lambda formals on head line"
  "(lambda (a b)\n  (display a)\n  (display b)\n  (+ a b))\n"
  (out '(lambda (a b) (display a) (display b) (+ a b)) 'pp-width 25))
(check "when test on head line"
  "(when (and (> x 1) (< x 100))\n  (display x)\n  (newline))\n"
  (out '(when (and (> x 1) (< x 100)) (display x) (newline)) 'pp-width 30))
(check "let* bindings"
  "(let* ((a 1) (b 2))\n  (+ a b))\n"
  (out '(let* ((a 1) (b 2)) (+ a b)) 'pp-width 20))
(check "or fills"
  "(or aaaaaaaaaaaa bbbbbbbbbbbbb cccccccccccc\n    ddddddddddd eeeeeeeeeeee)\n"
  (out '(or aaaaaaaaaaaa bbbbbbbbbbbbb cccccccccccc ddddddddddd eeeeeeeeeeee) 'pp-width 45))
(check "application aligns under the first argument"
  "(fold-left +\n           0\n           (list 1 2 3)\n           (list 4 5 6))\n"
  (out '(fold-left + 0 (list 1 2 3) (list 4 5 6)) 'pp-width 25))
(check "closing parens count toward the width: fits"
  "(xx ww\n    (yy zz uu))\n"
  (out '(xx ww (yy zz uu)) 'pp-width 15))
(check "closing parens count toward the width: breaks"
  "(xx ww\n    (yy zz\n        uu))\n"
  (out '(xx ww (yy zz uu)) 'pp-width 14))
(check "pp-code #f is plain data layout"
  "(define\n (f n)\n (display n))\n"
  (out '(define (f n) (display n)) 'pp-width 20 'pp-code #f))
(check "user style via pp-styles"
  "(my-form a\n  b\n  c\n  d\n  e\n  f)\n"
  (out '(my-form a b c d e f) 'pp-width 20 'pp-styles (add-pp-style (pp-styles) 'my-form '(_ e . body))))
(check "brackets for clauses"
  "(let ([a 1] [b 2])\n  (cond [(> a b) a]\n        [else b]))\n"
  (out '(let ((a 1) (b 2)) (cond ((> a b) a) (else b))) 'pp-width 20 'pp-brackets #t))
(check "quote forms abbreviate"
  "`(a ,b ,@c)\n"
  (out '(quasiquote (a (unquote b) (unquote-splicing c)))))
(check "quote of a symbol"
  "'a\n"
  (out ''a))
(check "dotted tail"
  "(a\n b\n c\n . d)\n"
  (out '(a b c . d) 'pp-width 6 'pp-code #f))
(check "dotted tail fits"
  "(a b . c)\n"
  (out '(a b . c)))
(check "level shell"
  "(a (b (...)))\n"
  (out '(a (b (c (d)))) 'pp-level 2 'pp-level-stub #t))
(check "level stub"
  "(a (b ...))\n"
  (out '(a (b (c (d)))) 'pp-level 2))
(check "length stub"
  "(1 2 3 ...)\n"
  (out '(1 2 3 4 5 6) 'pp-length 3))
(check "line limit"
  "(a\n b\n c\n...\n"
  (out '(a b c d e f g h) 'pp-width 3 'pp-lines 3 'pp-code #f))
(check "indent pads continuation lines"
  "(a\n     b\n     c\n     d)\n"
  (out '(a b c d) 'pp-width 8 'pp-indent 4 'pp-code #f))
(check "radix"
  "(#xff #x10)\n"
  (out '(255 16) 'pp-radix 16))
(check "pp-pretty #f"
  "(define (f x) (if x 1 2))"
  (out '(define (f x) (if x 1 2)) 'pp-pretty #f))
(check "pp-newline #f"
  "(a b)"
  (out '(a b) 'pp-newline #f))
(check "Akkadian head uses define's style"
  "(šakānum (f n)\n  (display n)\n  (newline))\n"
  (out '(šakānum (f n) (display n) (newline)) 'pp-width 20))
(check "cuneiform head uses define's style"
  "(𒁹 (f n)\n  (display n)\n  (newline))\n"
  (out (list (string->symbol "𒁹") '(f n) '(display n) '(newline)) 'pp-width 20))
(check "CJK counts 2 columns: does not fit"
  "(\"世界\"\n \"世界\")\n"
  (out '("世界" "世界") 'pp-width 14 'pp-code #f))
(check "CJK counts 2 columns: fits"
  "(\"世界\" \"世界\")\n"
  (out '("世界" "世界") 'pp-width 15 'pp-code #f))
(check "cuneiform signs count 1 column each: fits"
  "(\"𒀭𒂗𒀭𒂗\" \"𒀭𒂗𒀭𒂗\")\n"
  (out (list "𒀭𒂗𒀭𒂗" "𒀭𒂗𒀭𒂗") 'pp-width 15 'pp-code #f))
(check "cuneiform signs count 1 column each: does not fit"
  "(\"𒀭𒂗𒀭𒂗\"\n \"𒀭𒂗𒀭𒂗\")\n"
  (out (list "𒀭𒂗𒀭𒂗" "𒀭𒂗𒀭𒂗") 'pp-width 14 'pp-code #f))
(check "emoji count 2 columns: does not fit"
  "(\"🙂🙂\"\n \"🙂🙂\")\n"
  (out (list "🙂🙂" "🙂🙂") 'pp-width 14 'pp-code #f))
(check "emoji count 2 columns: fits"
  "(\"🙂🙂\" \"🙂🙂\")\n"
  (out (list "🙂🙂" "🙂🙂") 'pp-width 15 'pp-code #f))
(check "combining marks count 0: fits"
  "(\"ạ̄̂\" \"ạ̄̂\")\n"
  (out (list (string #\a #\x304 #\x302 #\x323) (string #\a #\x304 #\x302 #\x323)) 'pp-width 9 'pp-code #f))
(check "cuneiform symbols print raw"
  "(a 𒀭𒂗 \"𒀭\")\n"
  (out (list 'a (string->symbol "𒀭𒂗") "𒀭")))
(check "Akkadian transliteration symbols print raw"
  "(ḫiṭītu šumu)\n"
  (out '(ḫiṭītu šumu)))
;; Shared and circular structure get datum labels.
(check "circular list label" "#0=(1 2 3 . #0#)\n"
  (let ((c (list 1 2 3))) (set-cdr! (cddr c) c) (out c)))
(check "pp-graph labels shared structure" "(#0=(a b) #0#)\n"
  (let ((s (list 'a 'b))) (out (list s s) 'pp-graph #t)))

;; pp-color: color sequences go through pp-tint, text through pp-emit.
(let ((tints '()) (emits '()))
  (pp '(define x "hi") (open-output-string) 'pp-color #t
      'pp-tint (lambda (s p) (set! tints (cons s tints)))
      'pp-emit (lambda (s p) (set! emits (cons s emits))))
  (check "pp-color text goes through pp-emit" "(define x \"hi\")\n"
    (apply string-append (reverse emits)))
  (check "pp-color sequences go through pp-tint" #t (pair? tints)))
(let ((tints '()))
  (pp '(define x "hi") (open-output-string) 'pp-tint (lambda (s p) (set! tints (cons s tints))))
  (check "no pp-color means no tint" '() tints))

;; A skin-tone modifier attaches only to an emoji base: a CJK character
;; followed by one is 2 + 2 columns.
(check "CJK + skin tone modifier counts 4 columns: fits"
  "(\"中\x1F3FD;\" \"中\x1F3FD;\")\n"
  (out (list "中\x1F3FD;" "中\x1F3FD;") 'pp-width 15 'pp-code #f))
(check "CJK + skin tone modifier counts 4 columns: does not fit"
  "(\"中\x1F3FD;\"\n \"中\x1F3FD;\")\n"
  (out (list "中\x1F3FD;" "中\x1F3FD;") 'pp-width 14 'pp-code #f))
;; A ZWJ before a non-emoji joins nothing.
(check "ZWJ before a letter does not merge it"
  "(\"a\x200D;b\" \"a\x200D;b\")\n"
  (out (list "a\x200D;b" "a\x200D;b") 'pp-width 11 'pp-code #f))

;; A mapper that returns #f or a bad index means "no color", not an error.
(check "custom palette mapper returning #f"
  ""
  (semantic-color->start-string 'string
    (make-semantic-color-mapper default-asb-palette (lambda (sc) #f)) 0))

(display "SRFI-272 layout tests: ") (display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
