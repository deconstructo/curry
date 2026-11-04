;;; SRFI-166 (Monadic Formatting) base library implementation.
;;; https://srfi.schemers.org/srfi-166/
;;;
;;; ── Model ───────────────────────────────────────────────────────────
;;; A formatter is an opaque record holding a procedure of one argument,
;;; the formatting state. Applying a formatter writes text through the
;;; state's `output` and updates the state's row and column. A state
;;; variable is a first-class object; the state maps variables to values.
;;;
;;;   <fstate>   vars   an alist (variable . value), newest first; `with`
;;;                     conses onto it and puts the old list back after
;;;              row    the current row (0-based), a field for speed
;;;              col    the current column, a field for speed
;;;
;;; The reference implementation uses first-class continuations for
;;; `columnar` and `trimmed/lazy`. curry has escape-only continuations, so
;;; neither does here. A column is run to completion into a string, and
;;; an infinite column or trimmed/lazy stops by escaping out of its
;;; formatter once enough output exists (see %run-until).
;;;
;;; `forked` runs its first formatter on the current variable bindings and
;;; then restores them before running the second, so variable changes made
;;; by the first (with!) are discarded while its output, and the row and
;;; column that output reached, are kept. The SRFI does not say whether
;;; the first formatter's output appears; this reading keeps the output
;;; and the position consistent.
;;;
;;; `written` and the number formatting inside it use the SRFI-272 pretty
;;; printer's engine to render pairs and vectors on one line, with datum
;;; labels for cycles (written), all shared structure (written-shared) or
;;; none (written-simply).
;;;
;;; Macros: curry's syntax-rules is not hygienic across define-library
;;; boundaries, so every helper a macro expands into (%fn, %fn-collect,
;;; %with, %with!) is exported along with the macro.

(define-library (srfi s166 base-impl)
  (import (scheme base) (scheme char) (scheme write) (scheme file) (scheme inexact)
          (srfi s272 engine))
  (export
    ;; top level and composition
    show each each-in-list fn with with! forked call-with-output
    make-state-variable nothing
    %fn %fn-collect %with %with! formatter?
    ;; objects
    displayed written written-shared written-simply escaped maybe-escaped
    ;; numbers
    numeric numeric/comma numeric/si numeric/fitted
    ;; space
    nl fl space-to tab-to
    ;; joining
    joined joined/prefix joined/suffix joined/last joined/dot joined/range
    ;; padding and trimming
    padded padded/right padded/both trimmed trimmed/right trimmed/both
    trimmed/lazy fitted fitted/right fitted/both
    ;; state variables
    port row col width output writer string-width substring/width
    substring/preserve pad-char ellipsis radix precision decimal-sep
    decimal-align sign-rule comma-rule comma-sep word-separator?
    ambiguous-is-wide? pretty-environment output-default
    ;; for the other 166 libraries
    %make-formatter %run %fmt %emit! %get %capture %make-fstate %set-vars!
    %fstate-vars %fstate-row %fstate-col %format-number %run-until
    %format-number-from-state %run-all
    %pad-string %split-lines)
  (begin

    ;; ── Records ──────────────────────────────────────────────────────────────

    (define-record-type <state-variable>
      (%make-state-variable name default immutable)
      state-variable?
      (name %sv-name) (default %sv-default %set-sv-default!) (immutable %sv-immutable))

    (define (make-state-variable name default . maybe-immutable)
      (%make-state-variable name default (and (pair? maybe-immutable) (car maybe-immutable) #t)))

    (define-record-type <formatter>
      (%make-formatter proc)
      formatter?
      (proc %formatter-proc))

    (define-record-type <fstate>
      (%make-fstate vars row col)
      fstate?
      (vars %fstate-vars %set-vars!)
      (row %fstate-row %set-row!)
      (col %fstate-col %set-col!))

    ;; ── Standard state variables ────────────────────────────────────────────

    (define port (make-state-variable "port" #f))
    (define row (make-state-variable "row" 0))
    (define col (make-state-variable "col" 0))
    (define width (make-state-variable "width" 78))
    (define output (make-state-variable "output" #f))
    (define writer (make-state-variable "writer" #f))
    (define string-width (make-state-variable "string-width" #f))
    (define substring/width (make-state-variable "substring/width" #f))
    (define substring/preserve (make-state-variable "substring/preserve" #f))
    (define pad-char (make-state-variable "pad-char" #\space))
    (define ellipsis (make-state-variable "ellipsis" ""))
    (define radix (make-state-variable "radix" 10))
    (define precision (make-state-variable "precision" #f))
    (define decimal-sep (make-state-variable "decimal-sep" #\.))
    (define decimal-align (make-state-variable "decimal-align" #f))
    (define sign-rule (make-state-variable "sign-rule" #f))
    (define comma-rule (make-state-variable "comma-rule" #f))
    (define comma-sep (make-state-variable "comma-sep" #\,))
    (define word-separator? (make-state-variable "word-separator?" char-whitespace?))
    (define ambiguous-is-wide? (make-state-variable "ambiguous-is-wide?" #f))
    (define pretty-environment (make-state-variable "pretty-environment" #f))

    ;; Defaults that are procedures defined further down are filled in
    ;; where they are defined (a record field cannot be forward-referenced).
    (define (%default-string-width str . se)
      (if (pair? se)
          (- (if (pair? (cdr se)) (cadr se) (string-length str)) (car se))
          (string-length str)))

    ;; ── State access ─────────────────────────────────────────────────────────

    (define (%get st var)
      (cond ((eq? var row) (%fstate-row st))
            ((eq? var col) (%fstate-col st))
            (else
             (let ((hit (assq var (%fstate-vars st))))
               (if hit (cdr hit) (%sv-default var))))))

    (define (%set! st var val)
      (cond ((eq? var row) (%set-row! st val))
            ((eq? var col) (%set-col! st val))
            (else (%set-vars! st (cons (cons var val) (%fstate-vars st))))))

    (define (%run f st) ((%formatter-proc f) st))

    ;; ── Output ───────────────────────────────────────────────────────────────

    ;; Moves the state's row and column over the text just written.
    (define (%track! st str)
      (let ((len (string-length str)) (sw (or (%get st string-width) %default-string-width)))
        (let loop ((i 0) (rows 0) (last-start 0))
          (cond
            ((>= i len)
             (if (= rows 0)
                 (%set-col! st (+ (%fstate-col st) (sw str)))
                 (begin
                   (%set-row! st (+ (%fstate-row st) rows))
                   (%set-col! st (sw (substring str last-start len))))))
            ((char=? (string-ref str i) #\newline) (loop (+ i 1) (+ rows 1) (+ i 1)))
            (else (loop (+ i 1) rows last-start))))))

    (define (%default-write! st str)
      (write-string str (%get st port))
      (%track! st str))

    (define (output-default str)
      (%make-formatter (lambda (st) (%default-write! st str))))

    (define (%emit! st str)
      (unless (string=? str "")
        (let ((o (%get st output)))
          (if (or (not o) (eq? o output-default))
              (%default-write! st str)
              (%run (o str) st)))))

    ;; ── Coercion, sequencing ─────────────────────────────────────────────────

    (define (%fmt x) (if (formatter? x) x (displayed x)))

    (define (displayed obj)
      (cond
        ((formatter? obj) obj)
        ((string? obj) (%make-formatter (lambda (st) (%emit! st obj))))
        ((char? obj) (%make-formatter (lambda (st) (%emit! st (string obj)))))
        ((procedure? obj) (error "displayed: not a formatter" obj))
        (else (%make-formatter
                (lambda (st) (%run ((or (%get st writer) written) obj) st))))))

    (define (each . fmts)
      (%make-formatter (lambda (st) (for-each (lambda (f) (%run (%fmt f) st)) fmts))))

    (define (each-in-list fmts) (apply each fmts))

    (define nothing (%make-formatter (lambda (st) #f)))

    (define (%run-all fmts st) (for-each (lambda (f) (%run (%fmt f) st)) fmts))

    (define (show dest . fmts)
      (let* ((str-port (and (not dest) (open-output-string)))
             (out-port (cond ((not dest) str-port) ((eq? dest #t) (current-output-port)) (else dest)))
             (st (%make-fstate (list (cons port out-port)) 0 0)))
        (%run-all fmts st)
        (and str-port (get-output-string str-port))))

    ;; ── fn, with, with! ──────────────────────────────────────────────────────

    (define (%fn vars proc)
      (%make-formatter
        (lambda (st) (%run (%fmt (apply proc (map (lambda (v) (%get st v)) vars))) st))))

    (define-syntax fn
      (syntax-rules ()
        ((fn (binding ...) body ...)
         (%fn-collect (binding ...) () () body ...))))

    ;; (id var) binds id to var's value; a bare id names the variable it is
    ;; bound to.
    (define-syntax %fn-collect
      (syntax-rules ()
        ((%fn-collect () (id ...) (var ...) body ...)
         (%fn (list var ...) (lambda (id ...) body ...)))
        ((%fn-collect ((id var) rest ...) (ids ...) (vars ...) body ...)
         (%fn-collect (rest ...) (ids ... id) (vars ... var) body ...))
        ((%fn-collect (id rest ...) (ids ...) (vars ...) body ...)
         (%fn-collect (rest ...) (ids ... id) (vars ... id) body ...))))

    (define (%with bindings fmts)
      (%make-formatter
        (lambda (st)
          (let ((saved (%fstate-vars st)))
            (for-each (lambda (b) (%set! st (car b) (cdr b))) bindings)
            (%run-all fmts st)
            (%set-vars! st saved)))))

    (define-syntax with
      (syntax-rules ()
        ((with ((var val) ...) fmt ...)
         (%with (list (cons var val) ...) (list fmt ...)))))

    (define (%with! bindings)
      (%make-formatter
        (lambda (st)
          (for-each
            (lambda (b)
              (when (and (state-variable? (car b)) (%sv-immutable (car b)))
                (error "with!: immutable state variable" (%sv-name (car b))))
              (%set! st (car b) (cdr b)))
            bindings))))

    (define-syntax with!
      (syntax-rules ()
        ((with! (var val) ...)
         (%with! (list (cons var val) ...)))))

    (define (forked a b)
      (%make-formatter
        (lambda (st)
          (let ((saved (%fstate-vars st)))
            (%run (%fmt a) st)
            (%set-vars! st saved)
            (%run (%fmt b) st)))))

    ;; Runs fmts on a copy of st's state with output going to a string.
    (define (%capture st fmts)
      (let* ((p (open-output-string))
             (st2 (%make-fstate (cons (cons port p) (%fstate-vars st)) (%fstate-row st) (%fstate-col st))))
        (%run-all fmts st2)
        (get-output-string p)))

    (define (call-with-output fmt mapper)
      (%make-formatter
        (lambda (st) (%run (%fmt (mapper (%capture st (list fmt)))) st))))

    ;; Runs fmts on a copy of st with `output` replaced by sink, a
    ;; (string) -> unit procedure. The sink may call escape (a one-argument
    ;; procedure) to abandon the run; the value passed to it is returned,
    ;; or #t when the formatters finished. This is how an infinite column
    ;; or trimmed/lazy stops without a re-entrant continuation.
    (define (%run-until st fmts make-sink)
      (call-with-current-continuation
        (lambda (escape)
          (let* ((sink (make-sink escape))
                 (st2 (%make-fstate (cons (cons output
                                               (lambda (str) (%make-formatter (lambda (st3) (sink str)))))
                                         (%fstate-vars st))
                                    (%fstate-row st) (%fstate-col st))))
            (%run-all fmts st2)
            #t))))

    ;; ── Strings ──────────────────────────────────────────────────────────────

    (define (%split-lines str)
      (let ((len (string-length str)))
        (let loop ((i 0) (start 0) (acc '()))
          (cond
            ((>= i len) (reverse (if (= start i) acc (cons (substring str start i) acc))))
            ((char=? (string-ref str i) #\newline) (loop (+ i 1) (+ i 1) (cons (substring str start i) acc)))
            (else (loop (+ i 1) start acc))))))

    (define (%join-strings strs sep)
      (cond ((null? strs) "")
            ((null? (cdr strs)) (car strs))
            (else (string-append (car strs) sep (%join-strings (cdr strs) sep)))))

    ;; n copies of the pad character (or string).
    (define (%pad-string pc n)
      (cond ((<= n 0) "")
            ((char? pc) (make-string n pc))
            (else (let loop ((i 0) (acc "")) (if (>= i n) acc (loop (+ i 1) (string-append acc pc)))))))

    (define (%as-string x) (if (char? x) (string x) x))

    ;; ── Objects ──────────────────────────────────────────────────────────────

    (define (%write->string obj)
      (let ((p (open-output-string))) (write obj p) (get-output-string p)))

    ;; pairs and vectors render on one line through the SRFI-272 engine,
    ;; with every number formatted by the current radix and precision.
    (define (%render-structure st obj mode)
      (let ((number-hook
              (cons number?
                    (atom-pp-hook #f #f
                      (lambda (n r) (list (%format-number-from-state st n)))))))
        (parameterize ((pp-pretty #f) (pp-newline #f) (pp-width #f) (pp-code #f)
                       (pp-hooks (cons number-hook (pp-hooks))))
          (%render-to-string obj mode))))

    (define (%written-string st obj mode)
      (cond
        ((number? obj) (%format-number-from-state st obj))
        ((or (pair? obj) (vector? obj)) (%render-structure st obj mode))
        (else (%write->string obj))))

    (define (%written mode)
      (lambda (obj)
        (%make-formatter (lambda (st) (%emit! st (%written-string st obj mode))))))

    (define written (%written 'circle))
    (define written-shared (%written 'graph))
    (define written-simply (%written 'simple))

    ;; (escaped str [quote-ch esc-ch renamer]): backslash-escapes the quote
    ;; and escape characters; with esc-ch #f, doubles the quote instead.
    (define (%escape-string str quote-ch esc-ch renamer)
      (let ((out (open-output-string)))
        (string-for-each
          (lambda (ch)
            (cond
              ((char=? ch quote-ch)
               (if esc-ch (write-char esc-ch out) (write-char quote-ch out))
               (write-char ch out))
              ((and esc-ch (char=? ch esc-ch))
               (write-char esc-ch out) (write-char ch out))
              ((and renamer (renamer ch))
               => (lambda (r) (when esc-ch (write-char esc-ch out)) (write-char r out)))
              (else (write-char ch out))))
          str)
        (get-output-string out)))

    (define (escaped str . opts)
      (let ((quote-ch (if (pair? opts) (car opts) #\"))
            (esc-ch (if (and (pair? opts) (pair? (cdr opts))) (cadr opts) #\\))
            (renamer (and (pair? opts) (pair? (cdr opts)) (pair? (cddr opts)) (caddr opts))))
        (displayed (%escape-string str quote-ch esc-ch renamer))))

    (define (maybe-escaped str pred . opts)
      (let ((quote-ch (if (pair? opts) (car opts) #\"))
            (esc-ch (if (and (pair? opts) (pair? (cdr opts))) (cadr opts) #\\))
            (renamer (and (pair? opts) (pair? (cdr opts)) (pair? (cddr opts)) (caddr opts))))
        (let ((special? (let loop ((i 0))
                          (and (< i (string-length str))
                               (let ((ch (string-ref str i)))
                                 (or (char=? ch quote-ch) (and esc-ch (char=? ch esc-ch)) (pred ch)
                                     (loop (+ i 1))))))))
          (displayed
            (if special?
                (string-append (string quote-ch) (%escape-string str quote-ch esc-ch renamer) (string quote-ch))
                str)))))

    ;; ── Numbers ──────────────────────────────────────────────────────────────

    (define (%rule->list rule)
      (cond ((not rule) #f) ((exact-integer? rule) (list rule)) ((pair? rule) rule) (else #f)))

    ;; Groups the digit string from the right: the first size applies to
    ;; the least significant digits, the last size repeats.
    (define (%group-digits digits rule sep)
      (let ((rule (%rule->list rule)))
        (if (or (not rule) (string=? digits ""))
            digits
            (let loop ((end (string-length digits)) (rule rule) (acc '()))
              (let* ((size (max 1 (car rule))) (start (max 0 (- end size)))
                     (piece (substring digits start end)))
                (if (= start 0)
                    (%join-strings (cons piece acc) sep)
                    (loop start (if (null? (cdr rule)) rule (cdr rule)) (cons piece acc))))))))

    (define (%left-pad str n pc)
      (let ((len (string-length str))) (if (>= len n) str (string-append (%pad-string pc (- n len)) str))))

    ;; The fractional digits of q (0 <= q < 1) in radix, up to max-digits.
    (define (%fraction-digits q radix max-digits)
      (let loop ((q q) (n 0) (acc '()))
        (if (or (zero? q) (>= n max-digits))
            (list->string (reverse acc))
            (let* ((scaled (* q radix)) (d (floor scaled)))
              (loop (- scaled d) (+ n 1) (cons (string-ref (number->string d radix) 0) acc))))))

    (define (%format-number num radix precision sign-rule comma-rule comma-sep decimal-sep decimal-align pc)
      (cond
        ((not (real? num)) (%write->string num))
        ((and (inexact? num) (or (nan? num) (infinite? num)))
         (%write->string num))
        (else
         (let* ((q (let ((e (if (exact? num) num (inexact->exact num)))) (abs e)))
                ;; a negative number that rounds to zero is not shown as -0.00
                (neg? (and (negative? num)
                           (or (not precision) (not (zero? (round (* q (expt radix precision))))))))
                (csep (%as-string comma-sep))
                (dsep (let ((d (%as-string decimal-sep)))
                        (if (and (string=? d ".") (string=? csep ".")) "," d)))
                (int+frac
                  (cond
                    (precision
                     (let* ((unit (expt radix precision)) (scaled (round (* q unit))))
                       (cons (number->string (quotient scaled unit) radix)
                             (if (= precision 0)
                                 ""
                                 (%left-pad (number->string (remainder scaled unit) radix) precision #\0)))))
                    ((integer? q) (cons (number->string q radix) ""))
                    ((and (exact? num) (= radix 10) (not (integer? num)))
                     ;; an exact rational with no precision stays a ratio
                     (cons (number->string q radix) 'ratio))
                    ((= radix 10)
                     (let* ((s (number->string (abs num))))
                       (let ((dot (let loop ((i 0)) (cond ((>= i (string-length s)) #f)
                                                          ((char=? (string-ref s i) #\.) i)
                                                          (else (loop (+ i 1)))))))
                         (if (and dot (not (let loop ((i 0)) (and (< i (string-length s))
                                                                  (or (char=? (string-ref s i) #\e) (loop (+ i 1)))))))
                             (cons (substring s 0 dot) (substring s (+ dot 1) (string-length s)))
                             (cons s 'raw)))))
                    (else
                     (let ((ip (floor q)))
                       (cons (number->string ip radix) (%fraction-digits (- q ip) radix 52))))))
                (int-part (car int+frac)) (frac-part (cdr int+frac)))
           (if (symbol? frac-part)
               ;; a ratio or an exponent form: no grouping, no decimal separator
               (string-append (if neg? "-" "") int-part)
               (let* ((grouped (%group-digits int-part comma-rule csep))
                      (aligned (if (and decimal-align (exact-integer? decimal-align))
                                   (%left-pad grouped decimal-align pc)
                                   grouped))
                      (digits (if (string=? frac-part "") aligned (string-append aligned dsep frac-part))))
                 (cond
                   (neg? (if (pair? sign-rule)
                             (string-append (car sign-rule) digits (cdr sign-rule))
                             (string-append "-" digits)))
                   ((eq? sign-rule #t) (string-append "+" digits))
                   (else digits))))))))

    (define (%format-number-from-state st n)
      (if (and (= (%get st radix) 10) (not (%get st precision)) (not (%get st comma-rule))
               (not (%get st sign-rule)) (not (%get st decimal-align)))
          (%write->string n)
          (%format-number n (%get st radix) (%get st precision) (%get st sign-rule)
                          (%get st comma-rule) (%get st comma-sep) (%get st decimal-sep)
                          (%get st decimal-align) (%get st pad-char))))

    ;; A positional argument that was not given (numeric/comma passes this
    ;; for the ones it skips), so the state variable's value is used.
    (define %unset (list 'unset))

    ;; (numeric num [radix precision sign-rule comma-rule comma-sep decimal-sep]):
    ;; an argument that is given replaces the state variable of that name.
    (define (numeric num . args)
      (%make-formatter
        (lambda (st)
          (define (arg i var)
            (if (and (> (length args) i) (not (eq? (list-ref args i) %unset)))
                (list-ref args i)
                (%get st var)))
          (%emit! st (%format-number num (arg 0 radix) (arg 1 precision) (arg 2 sign-rule)
                                     (arg 3 comma-rule) (arg 4 comma-sep) (arg 5 decimal-sep)
                                     (%get st decimal-align) (%get st pad-char))))))

    ;; (numeric/comma num [comma-rule radix precision sign-rule]); the
    ;; comma rule defaults to groups of 3.
    (define (numeric/comma num . args)
      (define (nth i) (if (> (length args) i) (list-ref args i) %unset))
      (numeric num (nth 1) (nth 2) (nth 3) (if (> (length args) 0) (car args) 3)))

    ;; SI prefixes: base 1000 has sub-unit prefixes, base 1024 does not.
    (define %si-up '#("k" "M" "G" "T" "P" "E" "Z" "Y"))
    (define %si-down '#("m" "µ" "n" "p" "f" "a" "z" "y"))
    (define %si-bin '#("Ki" "Mi" "Gi" "Ti" "Pi" "Ei" "Zi" "Yi"))

    (define (%strip-trailing-point s)
      (let ((len (string-length s)))
        (if (and (>= len 2) (string=? (substring s (- len 2) len) ".0")) (substring s 0 (- len 2)) s)))

    (define (numeric/si num . args)
      (let ((base (if (pair? args) (car args) 1000))
            (sep (if (and (pair? args) (pair? (cdr args))) (cadr args) "")))
        (unless (memv base '(1000 1024)) (error "numeric/si: base must be 1000 or 1024" base))
        (%make-formatter
          (lambda (st)
            (let* ((a (abs num))
                   (e0 (cond ((zero? a) 0)
                             (else (let loop ((e 0) (v (if (exact? a) a (inexact->exact a))))
                                     (cond ((and (>= v base) (< e 8)) (loop (+ e 1) (/ v base)))
                                           ((and (< v 1) (> e -8) (= base 1000)) (loop (- e 1) (* v base)))
                                           (else e))))))
                   (scaled (lambda (e) (let ((v (/ (if (exact? num) num (inexact->exact num)) (expt base e)))) v)))
                   ;; rounding to one decimal can reach base^(e+1): move up a prefix
                   (e (let ((v (abs (scaled e0))))
                        (if (and (>= (/ (round (* v 10)) 10) base) (< e0 8)) (+ e0 1) e0)))
                   (v (scaled e))
                   (text (if (and (= e 0) (integer? v))
                             (number->string (if (exact? v) v (inexact->exact v)))
                             (%strip-trailing-point
                               (%format-number v 10 1 #f #f #\, #\. #f #\space))))
                   (suffix (cond ((= e 0) "")
                                 ((= base 1024) (vector-ref %si-bin (- e 1)))
                                 ((> e 0) (vector-ref %si-up (- e 1)))
                                 (else (vector-ref %si-down (- (- e) 1))))))
              (%emit! st (string-append text (%as-string sep) suffix)))))))

    ;; (numeric/fitted width num . numeric-args): the number, or # for each
    ;; digit position when it does not fit.
    (define (numeric/fitted w num . args)
      (%make-formatter
        (lambda (st)
          (let* ((str (%capture st (list (apply numeric num args)))))
            (if (<= (string-length str) w)
                (%emit! st str)
                (let ((p (let ((hit (if (> (length args) 1) (list-ref args 1) (%get st precision)))) (or hit 0))))
                  (%emit! st
                    (if (> p 0)
                        (string-append (make-string (max 0 (- w p 1)) #\#) "." (make-string p #\#))
                        (make-string w #\#)))))))))

    ;; ── Space ────────────────────────────────────────────────────────────────

    (define nl (%make-formatter (lambda (st) (%emit! st "\n"))))

    (define fl
      (%make-formatter (lambda (st) (unless (= (%fstate-col st) 0) (%emit! st "\n")))))

    (define (space-to column)
      (%make-formatter
        (lambda (st)
          (%emit! st (%pad-string (%get st pad-char) (- column (%fstate-col st)))))))

    (define (tab-to . maybe-width)
      (let ((tw (if (pair? maybe-width) (car maybe-width) 8)))
        (%make-formatter
          (lambda (st)
            (let ((c (%fstate-col st)))
              (%emit! st (%pad-string (%get st pad-char) (modulo (- tw (modulo c tw)) tw))))))))

    ;; ── Joining ──────────────────────────────────────────────────────────────

    (define (joined mapper lst . maybe-sep)
      (let ((sep (if (pair? maybe-sep) (car maybe-sep) "")))
        (%make-formatter
          (lambda (st)
            (let loop ((l lst) (first? #t))
              (when (pair? l)
                (unless first? (%run (%fmt sep) st))
                (%run (%fmt (mapper (car l))) st)
                (loop (cdr l) #f)))))))

    (define (joined/prefix mapper lst . maybe-sep)
      (let ((sep (if (pair? maybe-sep) (car maybe-sep) "")))
        (%make-formatter
          (lambda (st)
            (for-each (lambda (x) (%run (%fmt sep) st) (%run (%fmt (mapper x)) st)) lst)))))

    (define (joined/suffix mapper lst . maybe-sep)
      (let ((sep (if (pair? maybe-sep) (car maybe-sep) "")))
        (%make-formatter
          (lambda (st)
            (for-each (lambda (x) (%run (%fmt (mapper x)) st) (%run (%fmt sep) st)) lst)))))

    (define (joined/last mapper last-mapper lst . maybe-sep)
      (let ((sep (if (pair? maybe-sep) (car maybe-sep) "")))
        (%make-formatter
          (lambda (st)
            (let loop ((l lst) (first? #t))
              (when (pair? l)
                (unless first? (%run (%fmt sep) st))
                (%run (%fmt ((if (null? (cdr l)) last-mapper mapper) (car l))) st)
                (loop (cdr l) #f)))))))

    (define (joined/dot mapper dot-mapper lst . maybe-sep)
      (let ((sep (if (pair? maybe-sep) (car maybe-sep) "")))
        (%make-formatter
          (lambda (st)
            (let loop ((l lst) (first? #t))
              (cond
                ((pair? l)
                 (unless first? (%run (%fmt sep) st))
                 (%run (%fmt (mapper (car l))) st)
                 (loop (cdr l) #f))
                ((null? l) #f)
                (else
                 (unless first? (%run (%fmt sep) st))
                 (%run (%fmt (dot-mapper l)) st))))))))

    (define (joined/range mapper start . opts)
      (let ((end (if (pair? opts) (car opts) #f))
            (sep (if (and (pair? opts) (pair? (cdr opts))) (cadr opts) "")))
        (%make-formatter
          (lambda (st)
            (let loop ((i start))
              (when (or (not end) (< i end))
                (unless (= i start) (%run (%fmt sep) st))
                (%run (%fmt (mapper i)) st)
                (loop (+ i 1))))))))

    ;; ── Padding and trimming ─────────────────────────────────────────────────

    (define (%sw st) (or (%get st string-width) %default-string-width))
    (define (%sub st) (or (%get st substring/width) (lambda (s from to) (substring s from to))))

    (define (%padded where target fmts)
      (%make-formatter
        (lambda (st)
          (let* ((str (%capture st fmts))
                 (n (- target ((%sw st) str)))
                 (pc (%get st pad-char)))
            (cond
              ((<= n 0) (%emit! st str))
              ((eq? where 'left) (%emit! st (%pad-string pc n)) (%emit! st str))
              ((eq? where 'right) (%emit! st str) (%emit! st (%pad-string pc n)))
              (else
               (let ((l (quotient n 2)))
                 (%emit! st (%pad-string pc l))
                 (%emit! st str)
                 (%emit! st (%pad-string pc (- n l))))))))))

    (define (padded n . fmts) (%padded 'left n fmts))
    (define (padded/right n . fmts) (%padded 'right n fmts))
    (define (padded/both n . fmts) (%padded 'both n fmts))

    ;; Trims to target columns. where says which end is cut: left cuts the
    ;; start (keeping the end), right cuts the end, both cuts evenly with
    ;; the odd column from the right. Cut text is passed to
    ;; substring/preserve, and the ellipsis counts toward target.
    (define (%trimmed where target fmts)
      (%make-formatter
        (lambda (st)
          (let* ((str (%capture st fmts)) (sw (%sw st)) (total (sw str)))
            (if (<= total target)
                (%emit! st str)
                (let* ((ell (%as-string (%get st ellipsis)))
                       (ew (sw ell))
                       ;; substring/width may include a wide character that
                       ;; straddles the start column; move the start right
                       ;; until the piece fits the columns being kept.
                       (sub (let ((raw (%sub st)))
                              (lambda (s from to)
                                (let loop ((from from))
                                  (let ((piece (raw s from to)))
                                    (if (or (<= (sw piece) (- to from)) (>= from to))
                                        piece
                                        (loop (+ from 1))))))))
                       (pres (%get st substring/preserve))
                       (preserve (lambda (from to) (when pres (%emit! st (pres str from to))))))
                  (when (< target (if (eq? where 'both) (* 2 ew) ew))
                    (error "trimmed: width is smaller than the ellipsis" target))
                  (case where
                    ((left)
                     (let ((keep (- target ew)))
                       (preserve 0 (- total keep))
                       (%emit! st ell)
                       (%emit! st (sub str (- total keep) total))))
                    ((right)
                     (let ((keep (- target ew)))
                       (%emit! st (sub str 0 keep))
                       (%emit! st ell)
                       (preserve keep total)))
                    (else
                     (let* ((keep (- target (* 2 ew))) (cut (- total keep))
                            (left-cut (quotient cut 2)))
                       (preserve 0 left-cut)
                       (%emit! st ell)
                       (%emit! st (sub str left-cut (+ left-cut keep)))
                       (%emit! st ell)
                       (preserve (+ left-cut keep) total))))))))))

    (define (trimmed n . fmts) (%trimmed 'left n fmts))
    (define (trimmed/right n . fmts) (%trimmed 'right n fmts))
    (define (trimmed/both n . fmts) (%trimmed 'both n fmts))

    ;; Stops as soon as more than n columns have been produced, so it is
    ;; safe on infinite output. No ellipsis.
    (define (trimmed/lazy n . fmts)
      (%make-formatter
        (lambda (st)
          (let* ((buf (open-output-string)) (sw (%sw st)) (used 0))
            (%run-until st fmts
              (lambda (escape)
                (lambda (str)
                  (write-string str buf)
                  (set! used (+ used (sw str)))
                  (when (> used n) (escape #f)))))
            (let ((s (get-output-string buf)))
              (%emit! st (if (> (sw s) n) ((%sub st) s 0 n) s)))))))

    (define (%fitted where target fmts)
      (%make-formatter
        (lambda (st)
          (let ((str (%capture st fmts)))
            (%run (if (> ((%sw st) str) target)
                      (%trimmed where target (list str))
                      (%padded where target (list str)))
                  st)))))

    (define (fitted n . fmts) (%fitted 'left n fmts))
    (define (fitted/right n . fmts) (%fitted 'right n fmts))
    (define (fitted/both n . fmts) (%fitted 'both n fmts))

    ;; The state variables whose defaults are procedures defined above.
    (%set-sv-default! output output-default)
    (%set-sv-default! writer written)
    (%set-sv-default! string-width %default-string-width)
    (%set-sv-default! substring/width (lambda (s from to) (substring s from to)))))
