;;; SRFI-272 colorize library implementation: ANSI SGR ("Select
;;; Graphic Rendition") escape-sequence generation across four
;;; fidelity tiers (0: 16-color, 1: 256-color, 2: 24-bit truecolor,
;;; 3: truecolor + extended attributes), ASB (ANSI SGR Bundle) objects
;;; bundling one SGR parameter string per tier so a single color choice
;;; degrades gracefully on a less-capable terminal, palettes mapping
;;; semantic categories to ASBs, and environment-variable-based tier
;;; auto-detection. https://srfi.schemers.org/srfi-272/
;;;
;;; Scope note: sgr3's `uline` argument supports the numeric/symbolic/
;;; character-shorthand styles (off/straight/double/curly/dotted/
;;; dashed) and the `(style . fgtrue)` colored-underline extension via
;;; the well-established (if non-standard) SGR 4:N / 58;2;r;g;b
;;; sequences used by kitty/foot/wezterm/iTerm2; a terminal without
;;; extended underline support simply ignores the codes it doesn't
;;; recognize, per how SGR parameters have always degraded.

(define-library (srfi s272 colorize-impl)
  (import (scheme base) (scheme char) (srfi s272 engine))
  (export
    detect-sgr-tier sgr-support-tier sgr0 sgr1 sgr2 sgr3
    make-asb asb->sgr-string make-asb-palette asb-palette-ref default-asb-palette
    semantic-color-mapper? make-semantic-color-mapper default-semantic-color-mapper
    semantic-color->start-string semantic-color->end-string)
  (begin

    ;; ── Tier detection ───────────────────────────────────────────────────────

    (define (%env name) (get-environment-variable name))
    (define (%env-nonempty? name) (let ((v (%env name))) (and v (> (string-length v) 0))))

    (define (detect-sgr-tier)
      (cond
        ((%env-nonempty? "NO_COLOR") #f)
        ((%env-nonempty? "CLICOLOR_FORCE")
         (let ((ct (%env "COLORTERM")))
           (cond ((and ct (or (string=? ct "truecolor") (string=? ct "24bit"))) 2)
                 (else 0))))
        (else
         (let ((term (%env "TERM")) (ct (%env "COLORTERM")))
           (cond
             ((not term) #f)
             ((string=? term "dumb") #f)
             ((and ct (or (string=? ct "truecolor") (string=? ct "24bit"))) 2)
             ((%contains? term "256color") 1)
             ((or (%contains? term "xterm") (%contains? term "screen") (%contains? term "vt100")
                  (%contains? term "ansi") (%contains? term "linux") (%contains? term "rxvt")) 0)
             (else #f))))))

    (define (%contains? s sub)
      (let ((slen (string-length s)) (sublen (string-length sub)))
        (let loop ((i 0))
          (cond ((> (+ i sublen) slen) #f)
                ((string=? (substring s i (+ i sublen)) sub) #t)
                (else (loop (+ i 1)))))))

    (define sgr-support-tier (make-parameter (detect-sgr-tier)))

    ;; ── Low-level SGR string builders ────────────────────────────────────────

    (define (%join-semi codes) (%join codes ";"))
    (define (%join codes sep)
      (cond ((null? codes) "")
            ((null? (cdr codes)) (car codes))
            (else (string-append (car codes) sep (%join (cdr codes) sep)))))

    (define (%n->s n) (number->string n))

    (define (%fg16-code n) (if (< n 8) (%n->s (+ 30 n)) (%n->s (+ 90 (- n 8)))))
    (define (%bg16-code n) (if (< n 8) (%n->s (+ 40 n)) (%n->s (+ 100 (- n 8)))))

    (define (sgr0 fg16 . rest)
      (let* ((bold? (and (pair? rest) (car rest)))
             (uline? (and (pair? rest) (pair? (cdr rest)) (cadr rest)))
             (bg16 (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (caddr rest))))
        (%join-semi
          (append (list (%fg16-code fg16))
                  (if bold? (list "1") '())
                  (if uline? (list "4") '())
                  (if bg16 (list (%bg16-code bg16)) '())))))

    (define (sgr1 fg256 . rest)
      (let* ((bold? (and (pair? rest) (car rest)))
             (uline? (and (pair? rest) (pair? (cdr rest)) (cadr rest)))
             (bg256 (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (caddr rest))))
        (%join-semi
          (append (list "38" "5" (%n->s fg256))
                  (if bold? (list "1") '())
                  (if uline? (list "4") '())
                  (if bg256 (list "48" "5" (%n->s bg256)) '())))))

    (define (%rgb-codes prefix rgb)
      (list prefix "2" (%n->s (quotient rgb 65536)) (%n->s (modulo (quotient rgb 256) 256)) (%n->s (modulo rgb 256))))

    (define (sgr2 fgtrue . rest)
      (let* ((bold? (and (pair? rest) (car rest)))
             (uline? (and (pair? rest) (pair? (cdr rest)) (cadr rest)))
             (italic? (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (caddr rest)))
             (strike? (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (pair? (cdddr rest)) (cadddr rest)))
             (bgtrue (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (pair? (cdddr rest))
                          (pair? (cddddr rest)) (car (cddddr rest)))))
        (%join-semi
          (append (%rgb-codes "38" fgtrue)
                  (if bold? (list "1") '())
                  (if uline? (list "4") '())
                  (if italic? (list "3") '())
                  (if strike? (list "9") '())
                  (if bgtrue (%rgb-codes "48" bgtrue) '())))))

    (define (%uline-type-codes uline)
      (cond
        ((not uline) '())
        ((or (eq? uline 0) (eq? uline 'off)) '())
        ((or (eq? uline 1) (eq? uline #t) (eq? uline 'straight) (eqv? uline #\_)) (list "4"))
        ((or (eq? uline 2) (eq? uline 'double) (eqv? uline #\=)) (list "4:2"))
        ((or (eq? uline 3) (eq? uline 'curly) (eqv? uline #\~)) (list "4:3"))
        ((or (eq? uline 4) (eq? uline 'dotted) (eqv? uline #\:)) (list "4:4"))
        ((or (eq? uline 5) (eq? uline 'dashed) (eqv? uline #\-)) (list "4:5"))
        ((pair? uline) (append (%uline-type-codes (car uline)) (list "58" "2"
                                (%n->s (quotient (cdr uline) 65536))
                                (%n->s (modulo (quotient (cdr uline) 256) 256))
                                (%n->s (modulo (cdr uline) 256)))))
        (else '())))

    (define (sgr3 fgtrue . rest)
      (let* ((bold? (and (pair? rest) (car rest)))
             (dim? (and (pair? rest) (pair? (cdr rest)) (cadr rest)))
             (uline (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (caddr rest)))
             (italic? (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (pair? (cdddr rest)) (cadddr rest)))
             (strike? (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (pair? (cdddr rest))
                           (pair? (cddddr rest)) (car (cddddr rest))))
             (bgtrue (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (pair? (cdddr rest))
                          (pair? (cddddr rest)) (pair? (cdr (cddddr rest))) (cadr (cddddr rest)))))
        (%join-semi
          (append (%rgb-codes "38" fgtrue)
                  (if bold? (list "1") '())
                  (if dim? (list "2") '())
                  (%uline-type-codes uline)
                  (if italic? (list "3") '())
                  (if strike? (list "9") '())
                  (if bgtrue (%rgb-codes "48" bgtrue) '())))))

    ;; ── ASB (ANSI SGR Bundle) ────────────────────────────────────────────────
    ;; A 4-element vector, one SGR parameter string per tier 0-3.

    (define (make-asb s0 . rest)
      (let* ((s1 (if (pair? rest) (car rest) s0))
             (s2 (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) s1))
             (s3 (if (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest))) (caddr rest) s2)))
        (vector s0 s1 s2 s3)))

    (define (asb->sgr-string asb . maybe-tier)
      (let ((tier (if (pair? maybe-tier) (car maybe-tier) (sgr-support-tier))))
        (if (not tier)
            ""
            (string-append "\x1b;[" (vector-ref asb tier) "m"))))

    ;; ── Palettes ─────────────────────────────────────────────────────────────
    ;; A palette is a 16-element vector of ASBs.

    (define (make-asb-palette . asbs)
      (let ((n (length asbs)))
        (cond
          ((= n 16) (list->vector asbs))
          ((= n 8) (list->vector (append asbs asbs)))
          (else (error "make-asb-palette: expected 8 or 16 ASBs" n)))))

    (define (asb-palette-ref palette i) (vector-ref palette i))

    (define default-asb-palette
      (make-asb-palette
        (make-asb (sgr0 0)) (make-asb (sgr0 1)) (make-asb (sgr0 2)) (make-asb (sgr0 3))
        (make-asb (sgr0 4)) (make-asb (sgr0 5)) (make-asb (sgr0 6)) (make-asb (sgr0 7))
        (make-asb (sgr0 8)) (make-asb (sgr0 9)) (make-asb (sgr0 10)) (make-asb (sgr0 11))
        (make-asb (sgr0 12)) (make-asb (sgr0 13)) (make-asb (sgr0 14)) (make-asb (sgr0 15))))

    ;; ── Semantic color mappers ───────────────────────────────────────────────
    ;; A semantic-color-mapper is a procedure of (sc start?) -> string.
    ;; Tagged via a one-element wrapper closure trick isn't needed here:
    ;; curry procedures aren't otherwise distinguishable, so
    ;; semantic-color-mapper? is necessarily approximate (any procedure
    ;; of the right arity passes) -- documented, not hidden.

    (define %default-palette-mapping
      '((comment . 8) (string . 2) (keyword . 4) (number . 5) (symbol . 6)
        (error . 1) (warning . 3) (special-form . 4) (variable . 7) (constant . 5)))

    (define (semantic-color-mapper? obj) (procedure? obj))

    (define (make-semantic-color-mapper palette . maybe-palette-mapper)
      (let ((palette-mapper
              (if (pair? maybe-palette-mapper)
                  (car maybe-palette-mapper)
                  (lambda (sc) (let ((hit (assq sc %default-palette-mapping))) (if hit (cdr hit) 7))))))
        (lambda (sc start?)
          (let ((asb (asb-palette-ref palette (palette-mapper sc))))
            (if start? (asb->sgr-string asb) "\x1b;[0m")))))

    (define default-semantic-color-mapper (make-semantic-color-mapper default-asb-palette))

    (define (semantic-color->start-string sc . rest)
      (let ((mapper (if (pair? rest) (car rest) default-semantic-color-mapper))
            (tier (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) (sgr-support-tier))))
        (parameterize ((sgr-support-tier tier)) (mapper sc #t))))

    (define (semantic-color->end-string sc . rest)
      (let ((mapper (if (pair? rest) (car rest) default-semantic-color-mapper))
            (tier (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) (sgr-support-tier))))
        (parameterize ((sgr-support-tier tier)) (mapper sc #f))))))
