;;; SRFI-272 fancy tier: pprint-file keeps comments and blank lines, reads
;;; the -*- header and ";; * pp-styles:" comments, and pprint-file/html
;;; marks up colors. Input files are written under /tmp, as the other
;;; SRFI-272 file tests do.
;;; https://srfi.schemers.org/srfi-272/

(import (scheme base) (scheme write) (scheme file) (srfi 272 fancy))

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

(define path "/tmp/curry-srfi272-fancy-test.scm")

(define (with-source text thunk)
  (call-with-output-file path (lambda (p) (write-string text p)))
  (thunk))

(define (run text . kv)
  (with-source text
    (lambda ()
      (let ((o (open-output-string))) (apply pprint-file path o kv) (get-output-string o)))))

(define (run/html text . kv)
  (with-source text
    (lambda ()
      (let ((o (open-output-string))) (apply pprint-file/html path o kv) (get-output-string o)))))

(check "comments, blank lines and trailing comments survive"
  ";; top\n\n(define (f n)  ; doc\n  ;; body\n  (+ n 1))  ; end\n\n\n;; tail\n(g)\n"
  (run ";; top\n\n(define (f n) ; doc\n  ;; body\n  (+ n 1)) ; end\n\n\n\n;; tail\n(g)\n"))
(check "-*- fill-column sets pp-width"
  ";;; -*- mode: scheme; fill-column: 20 -*-\n(foo aaaa\n     bbbb\n     cccc\n     dddd\n     eeee)\n"
  (run ";;; -*- mode: scheme; fill-column: 20 -*-\n(foo aaaa bbbb cccc dddd eeee)\n"))
(check "keyword arguments override the -*- header"
  ";;; -*- fill-column: 20 -*-\n(foo aaaa bbbb cccc dddd eeee)\n"
  (run ";;; -*- fill-column: 20 -*-\n(foo aaaa bbbb cccc dddd eeee)\n" 'pp-width 40))
(check "-*- header reads t and numbers"
  ";; -*- pp-brackets: t; pp-tab: 3 -*-\n(let ([a 1] [b 2])\n    (cond [(> a b) a]\n          [else b])\n    (display a)\n    (display b))\n"
  (run ";; -*- pp-brackets: t; pp-tab: 3 -*-\n(let ((a 1) (b 2)) (cond ((> a b) a) (else b)) (display a) (display b))\n" 'pp-width 30))
(check "-*- header overrides a parameter's current value (nil)"
  ";; -*- pp-brackets: nil -*-\n(let ((a 1) (b 2)) (cond ((> a b) a) (else b)))\n"
  (parameterize ((pp-brackets #t))
    (run ";; -*- pp-brackets: nil -*-\n(let ((a 1) (b 2)) (cond ((> a b) a) (else b)))\n" 'pp-width 80)))
(check "a parameter is used when the header does not set it"
  "(let ([a 1] [b 2]) (cond [(> a b) a] [else b]))\n"
  (parameterize ((pp-brackets #t))
    (run "(let ((a 1) (b 2)) (cond ((> a b) a) (else b)))\n" 'pp-width 80)))
(check "pp-styles comment: alias and explicit style"
  ";; * pp-styles: my-let := let; my-form := (_ e . body)\n(my-let ((a 1)\n         (b 2))\n  (display a))\n(my-form x y z)\n"
  (run ";; * pp-styles: my-let := let; my-form := (_ e . body)\n(my-let ((a 1) (b 2)) (display a))\n(my-form x y z)\n" 'pp-width 15))
(check "pp-decorate #f drops comments, one blank line between forms"
  "(a b)\n\n(c d)\n\n(e)\n"
  (run ";; gone\n(a b) ; gone\n(c ; gone\n d)\n\n\n(e)\n" 'pp-decorate #f))
(check "block and datum comments are kept"
  "(a  #| inner |#\n  b\n  #;(hidden)\n  c)\n#| top\n block |#\n(d)\n"
  (run "(a #| inner |# b #;(hidden) c)\n#| top\n block |#\n(d)\n"))
(check "atoms keep their source text"
  "(list 'a `(b ,c ,@d) #(1 2) #\\( #\\space \"s;t\" (x . y) |a b|)\n"
  (run "(list 'a `(b ,c ,@d) #(1 2) #\\( #\\space \"s;t\" (x . y) |a b|)\n"))
(check "adjacent cuneiform atoms stay separate"
  "(𒁹 𒌋 ḫiṭītu \"𒀭𒂗\")  ; 𒀭\n"
  (run "(𒁹 𒌋 ḫiṭītu \"𒀭𒂗\") ; 𒀭\n"))
(check "pp-level applies to file contents"
  "(a (b ...))\n"
  (run "(a (b (c (d))))\n" 'pp-level 2))
(check "pp-length applies to file contents"
  "(1 2 ...)\n"
  (run "(1 2 3 4 5)\n" 'pp-length 2))
(check "html escapes text and wraps colors in spans"
  "<pre><span class=\"pp-comment\">;; &lt;c&gt;</span>\n<span class=\"pp-paren\">(</span><span class=\"pp-keyword\">define</span> <span class=\"pp-variable\">x</span> <span class=\"pp-string\">\"a&lt;b\"</span><span class=\"pp-paren\">)</span>\n</pre>"
  (run/html ";; <c>\n(define x \"a<b\")\n" 'pp-color #t))
(check "html without color has no spans"
  "<pre>(define x \"a&lt;b\")\n</pre>"
  (run/html "(define x \"a<b\")\n"))

;; An unterminated list is an error, not silent truncation.
(check "unterminated list raises" #t
  (guard (e (#t #t)) (run "(a (b\n") #f))

;; The parameters are restored after a file with a -*- header.
(run ";; -*- fill-column: 20 -*-\n(a)\n")
(check "header settings do not leak" 79 (pp-width))

(display "SRFI-272 fancy tests: ") (display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
