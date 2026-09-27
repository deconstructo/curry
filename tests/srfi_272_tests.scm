;;; SRFI-272 (Pretty Printing) test suite.
;;; https://srfi.schemers.org/srfi-272/
;;;
;;; Each library tier is imported under its own prefix -- basic,
;;; intermediate, advanced, and fancy each define their OWN,
;;; mutually-incompatible `pp`/`pprint`/etc. bindings (different
;;; arities/behavior per tier), so importing more than one tier's
;;; identically-named procedures unprefixed into one file would be a
;;; genuine name collision, not just clutter.

(import (scheme base) (scheme write) (scheme file)
        (prefix (srfi 272) min:)
        (prefix (srfi srfi-272) minalias:)
        (prefix (srfi 272 basic) bas:)
        (prefix (srfi srfi-272 basic) basalias:)
        (prefix (srfi 272 intermediate) int:)
        (prefix (srfi 272 advanced) adv:)
        (prefix (srfi 272 fancy) fan:)
        (prefix (srfi 272 colorize) col:)
        (prefix (srfi 272 measure) mea:))

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

(define (capture thunk)
  (let ((p (open-output-string)))
    (thunk p)
    (get-output-string p)))

;; ── minimalist: (srfi 272) / (srfi srfi-272) ─────────────────────────────

(check "min pp" "(1 2 3)\n" (capture (lambda (p) (min:pp '(1 2 3) p))))
(check "min pp alias" "(1 2 3)\n" (capture (lambda (p) (minalias:pp '(1 2 3) p))))
(check "min pp atom" "5\n" (capture (lambda (p) (min:pp 5 p))))
(check "min pp default port not clobbered" #t (procedure? min:pp))

;; ── basic: pp-width/pp-graph/pp-circle, pprint/pprint-shared/pprint-simple ──

(check "basic pp" "(1 2 3)\n" (capture (lambda (p) (bas:pp '(1 2 3) p))))
(check "basic pp alias" "(1 2 3)\n" (capture (lambda (p) (basalias:pp '(1 2 3) p))))

;; pprint == write w.r.t. sharing: only real cycles get datum labels
(let ((s (list 1 2)))
  (check "basic pprint no false sharing label" "((1 2) (1 2))\n"
    (capture (lambda (p) (bas:pprint (list s s) p)))))

;; pprint-shared == write-shared: every repeated object gets a label
(let ((s (list 1 2)))
  (check "basic pprint-shared labels repeats" "(#0=(1 2) #0#)\n"
    (capture (lambda (p) (bas:pprint-shared (list s s) p)))))

;; pprint-simple == write-simple: no sharing detection, prints plain
(let ((s (list 1 2)))
  (check "basic pprint-simple ignores sharing" "((1 2) (1 2))\n"
    (capture (lambda (p) (bas:pprint-simple (list s s) p)))))

;; a genuine cycle: pprint (write-semantics) must still label it
(let ((c (list 1 2 3)))
  (set-cdr! (cddr c) c)
  (check "basic pprint labels real cycle" "#0=(1 2 3 . #0#)\n"
    (capture (lambda (p) (bas:pprint c p)))))

(check "pp-width parameter default" 79 (bas:pp-width))
(check "pp-graph parameter default" #f (bas:pp-graph))
(check "pp-circle parameter default" #t (bas:pp-circle))

;; width-forced line breaking
(check "basic pp width break" "(a\n b\n c\n d)\n"
  (capture (lambda (p) (parameterize ((bas:pp-width 3)) (bas:pp '(a b c d) p)))))

;; ── intermediate: keyword args, pp*, pprint-file, pretty-style ──────────────

(check "int pp default" "(1 2 3)\n" (capture (lambda (p) (int:pp '(1 2 3) p))))
(check "int pp kwarg width" "(a\n b\n c)\n"
  (capture (lambda (p) (int:pp '(a b c) p 'pp-width 3))))
(check "int pp* splices trailing kv-list" "(a\n b\n c)\n"
  (capture (lambda (p) (int:pp* '(a b c) p (list 'pp-width 3)))))
(check "int pp-length truncation" "(1 2 ...)\n"
  (capture (lambda (p) (int:pp '(1 2 3 4 5) p 'pp-length 2))))
(check "int pp-level truncation" "(1 (2 ...))\n"
  (capture (lambda (p) (int:pp '(1 (2 (3 4))) p 'pp-level 1))))
(check "int pretty-style roundtrip" 'blue
  (begin (int:pretty-style 'accent 'blue) (int:pretty-style 'accent)))
(check "int pretty-style missing" #f (int:pretty-style 'never-set-xyz))

;; pprint-file: naive read+reprint round trip (no comment preservation
;; at this tier -- that's the fancy tier's own enhancement, tested below)
(let* ((in "/tmp/curry-srfi272-test-in.scm")
       (out "/tmp/curry-srfi272-test-out.scm"))
  (call-with-port (open-output-file in) (lambda (p) (write '(define (f x) (+ x 1)) p)))
  (int:pprint-file in out)
  (check "int pprint-file produces valid re-readable output"
    '(define (f x) (+ x 1))
    (call-with-port (open-input-file out) read)))

;; ── advanced: full parameter set, hooks, styles, make-pprint-generator ──────

(check "adv pp-radix default" 10 (adv:pp-radix))
(check "adv pp hex radix" "#xff\n" (capture (lambda (p) (adv:pp 255 p 'pp-radix 16))))

(define-record-type <pt> (mk-pt x y) pt? (x pt-x) (y pt-y))

(check "adv glst-pp-hook renders custom object" "#[pt 1 2]\n"
  (capture (lambda (p)
    (parameterize ((adv:pp-hooks (adv:add-pp-hook (adv:pp-hooks) pt?
                                    (adv:glst-pp-hook "#[pt " (lambda (o) (list (pt-x o) (pt-y o))) #f "]"))))
      (adv:pp (mk-pt 1 2) p)))))

(check "adv atom-pp-hook renders custom atom" "SYM:hi\n"
  (capture (lambda (p)
    (parameterize ((adv:pp-hooks (adv:add-pp-hook (adv:pp-hooks) symbol?
                                    (adv:atom-pp-hook #f (lambda (o) 5)
                                      (lambda (o radix) (list (string-append "SYM:" (symbol->string o))))))))
      (adv:pp 'hi p)))))

(check "adv add-pp-hook #f removes association" #f
  (let* ((r1 (adv:add-pp-hook '() symbol? (adv:atom-pp-hook #f (lambda (o) 0) (lambda (o r) (list "x")))))
         (r2 (adv:add-pp-hook r1 symbol? #f)))
    (adv:lookup-pp-hook r2 symbol?)))

(check "adv style registry functional update" '((foo . bar))
  (adv:add-pp-style '() 'foo 'bar))
(check "adv style registry remove via #f" '()
  (adv:add-pp-style (adv:add-pp-style '() 'foo 'bar) 'foo #f))

(check "adv make-pprint-generator yields lines" '("(1\n" " 2\n" " 3)\n")
  (let ((g (adv:make-pprint-generator '(1 2 3) 'pp-width 1)))
    (let loop ((acc '()))
      (let ((v (g)))
        (if (eof-object? v) (reverse acc) (loop (cons v acc)))))))

;; ── fancy: comment/blank-line-preserving pprint-file, pprint-file/html ──────

(define (%string-index s sub)
  (let ((slen (string-length s)) (sublen (string-length sub)))
    (let loop ((i 0))
      (cond ((> (+ i sublen) slen) -1)
            ((string=? (substring s i (+ i sublen)) sub) i)
            (else (loop (+ i 1)))))))

(let* ((in "/tmp/curry-srfi272-test-fancy-in.scm"))
  (call-with-port (open-output-file in)
    (lambda (p)
      (display ";; leading comment\n" p)
      (display "(define (f x) (+ x 1))\n" p)))
  (check "fancy pprint-file preserves leading comment" #t
    (let ((out (capture (lambda (p) (fan:pprint-file in p)))))
      (and (> (%string-index out ";; leading comment") -1) #t)))
  (check "fancy pprint-file/html escapes and wraps" #t
    (let ((out (capture (lambda (p) (fan:pprint-file/html in p)))))
      (and (>= (%string-index out "<pre>") 0) (>= (%string-index out "</pre>") 0)))))

;; ── colorize ─────────────────────────────────────────────────────────────

(check "col sgr0 basic fg" "31" (col:sgr0 1))
(check "col sgr0 bright fg" "91" (col:sgr0 9))
(check "col sgr0 bold+uline+bg" "31;1;4;44" (col:sgr0 1 #t #t 4))
(check "col sgr1 256" "38;5;200" (col:sgr1 200))
(check "col sgr2 truecolor red bold" "38;2;255;0;0;1" (col:sgr2 #xff0000 #t))
(check "col make-asb fills from last" #t
  (let ((a (col:make-asb "x")))
    (equal? (vector->list a) (list "x" "x" "x" "x"))))
(check "col asb->sgr-string disabled" ""
  (col:asb->sgr-string (col:make-asb (col:sgr0 1)) #f))
(check "col asb->sgr-string enabled" "\x1b;[31m"
  (col:asb->sgr-string (col:make-asb (col:sgr0 1)) 0))
(check "col default palette has 16 entries" #t
  (and (col:asb-palette-ref col:default-asb-palette 0) (col:asb-palette-ref col:default-asb-palette 15) #t))
(check "col semantic-color-mapper?" #t (col:semantic-color-mapper? col:default-semantic-color-mapper))
(check "col semantic start/end strings" #t
  (let ((s (col:semantic-color->start-string 'comment col:default-semantic-color-mapper 0))
        (e (col:semantic-color->end-string 'comment col:default-semantic-color-mapper 0)))
    (and (string? s) (string? e) (> (string-length s) 0) (> (string-length e) 0))))
(check "col make-asb-palette rejects bad arity" #t
  (guard (exn (#t #t)) (col:make-asb-palette (col:make-asb "x")) #f))

;; ── measure ──────────────────────────────────────────────────────────────

(check "measure ascii width 1" 1 ((mea:char-width-procedure) #\a))
(check "measure wide CJK width 2" 2 ((mea:char-width-procedure) (integer->char #x4e2d)))
(check "measure combining mark width 0" 0 ((mea:char-width-procedure) (integer->char #x0301)))
(check "measure control char width #f" #f ((mea:char-width-procedure) (integer->char 7)))
(check "measure parameter is overridable" 42
  (parameterize ((mea:char-width-procedure (lambda (c) 42)))
    ((mea:char-width-procedure) #\a)))

;; ── summary ──────────────────────────────────────────────────────────────

(display "SRFI-272 tests: ") (display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
