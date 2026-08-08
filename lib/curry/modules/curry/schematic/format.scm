;;; (curry schematic format) — a Scheme source reindenter.
;;;
;;; Ported from Evan Hanson's BSD-licensed schematic
;;; (https://git.foldling.org/schematic/), specifically src/schematic/
;;; format.scm — see docs/reference/module-schematic.md for the scope
;;; of the port. `format-scheme` reads Scheme source from an input
;;; port, reindents it according to a fairly conventional set of rules,
;;; and writes the result to an output port.
;;;
;;; This is NOT a pretty-printer: it never introduces line breaks or
;;; changes intraline spacing, only line indentation — the same
;;; distinction upstream draws in its own header comment.
;;;
;;; curry has no case-lambda, so every one of upstream's case-lambda
;;; procedures below (string-ref*, string-read, string-nth-read-index,
;;; read-indent, display-indent, format-scheme itself) is rewritten to
;;; take its optional trailing arguments via a `. rest` list instead,
;;; the same convention (curry csv)/(curry toml) already use.
;;;
;;; Still TODO, matching upstream's own outstanding item: `=>` in `cond`
;;; forms should not open a new indentation level.

(define-library (curry schematic format)
  (import (scheme base) (scheme read) (scheme write) (scheme char))
  (export
    format-scheme keyword-indent
    bracket-closure? bracket-parentheses? tabstop-length)
  (begin

;; When #t, a single closing bracket ("]") makes format-scheme insert
;; closing parentheses for all open forms before continuing.
(define bracket-closure? (make-parameter #f))

;; When #t, format-scheme treats brackets as though they were parens.
(define bracket-parentheses? (make-parameter #f))

;; When non-#f, indent with tabs of this width first, then spaces for
;; any remaining columns.
(define tabstop-length (make-parameter #f))

;; Runs body, returning the result of evaluating `default` on ANY error.
(define-syntax %guard/value
  (syntax-rules ()
    ((_ default . body) (let ((v default)) (guard (_ (#t v)) . body)))))

(define (%call-with-input-string s f) (call-with-port (open-input-string s) f))

(define (%constant? o) ; Everything except symbol?.
  (or (boolean? o) (char? o) (number? o) (string? o) (vector? o) (bytevector? o)))

;; substring, but returns the original string (no copy) when possible.
(define (%substring* str start end)
  (if (and (zero? start) (= (string-length str) end)) str (string-copy str start end)))

;; Trims char-whitespace? from the start of str only.
(define (%string-trim-left str)
  (let ((len (string-length str)))
    (let loop ((i 0))
      (cond ((or (= i len) (not (char-whitespace? (string-ref str i)))) (if (= i 0) str (string-copy str i len)))
            (else (loop (+ i 1)))))))

(define (%string-every? str pred?)
  (let ((len (string-length str)))
    (let loop ((i 0)) (or (= i len) (and (pred? (string-ref str i)) (loop (+ i 1)))))))

;; string-ref, returning `default` (if given, else #f) when `i` is
;; (positively) out of bounds.
(define (%string-ref* s i . default)
  (if (<= (string-length s) i) (if (pair? default) (car default) #f) (string-ref s i)))

;; Reads a single Scheme datum from str[start,end).
(define (%string-read str . opts)
  (let* ((start (if (pair? opts) (car opts) 0))
         (end   (if (and (pair? opts) (pair? (cdr opts))) (cadr opts) (string-length str))))
    (%call-with-input-string (%substring* str start end) read)))

;; Returns the byte index of the nth Scheme datum in str[start,end), or
;; `default` (#f unless given) at end of input, or #f on a read error.
;; NB not efficient for large n -- it reads with `read` n times, seeks
;; to the next token, then compares the remaining substring's length to
;; the original's (upstream's own caveat, not something the port could
;; improve on without changing the algorithm).
(define (%string-nth-read-index str n . opts)
  (let* ((start   (if (pair? opts) (car opts) 0))
         (opts    (if (pair? opts) (cdr opts) '()))
         (end     (if (pair? opts) (car opts) (string-length str)))
         (opts    (if (pair? opts) (cdr opts) '()))
         (default (if (pair? opts) (car opts) #f)))
    (%call-with-input-string
      (%substring* str start end)
      (lambda (s)
        (and
          (%guard/value #f
            (let loop ((n n)) (unless (zero? n) (read s) (loop (- n 1)))) #t)
          (let loop ()
            (let ((c (peek-char s)))
              (cond ((or (eof-object? c) (not (char-whitespace? c))) #t)
                    (else (read-char s) (loop)))))
          (let ((c (peek-char s)))
            (if (or (eof-object? c) (char=? c #\;))
                default
                (- end (string-length (read-string 65535 s))))))))))

;; Reads whitespace from `input` (default: current-input-port), returning
;; a numerical indent level in spaces (tabs count as (tabstop-length),
;; default 8).
(define (%read-indent . opts)
  (let ((input (if (pair? opts) (car opts) (current-input-port)))
        (ts (or (tabstop-length) 8)))
    (let loop ((n 0))
      (case (peek-char input)
        ((#\space) (read-char input) (loop (+ n 1)))
        ((#\tab)   (read-char input) (loop (+ n ts)))
        (else n)))))

;; Writes `indent` spaces (or a tab/space mix if (tabstop-length) is
;; set) to `output` (default: current-output-port).
(define (%display-indent indent . opts)
  (let ((output (if (pair? opts) (car opts) (current-output-port)))
        (ts (tabstop-length)))
    (cond ((<= indent 0) #t)
          ((not ts) (display (make-string indent #\space) output))
          (else
           (display (make-string (quotient indent ts) #\tab) output)
           (display (make-string (remainder indent ts) #\space) output)))))

;; Determines the horizontal alignment of a keyword's subforms: a
;; numerical offset from the keyword's own position (or a list of
;; offsets, applied to the form's data in order, the last persisting
;; until the form closes), or #f for no special treatment. `eol?` is
;; whether the keyword is the final token on its own line.
(define (keyword-indent sym eol?)
  (case sym
    ((begin cond) (and eol? 1))
    ((call-with-port) 0)
    ((case) 1)
    ((cond-expand) 1)
    ((define define-values) '(3 1))
    ((define-library) 1)
    ((define-record-type) 1)
    ((define-syntax syntax-rules) 1)
    ((do) '(3 3 1))
    ((guard) 1)
    ((lambda case-lambda) 1)
    ((let let*) 1)
    ((let-syntax letrec-syntax) 1)
    ((let-values let*-values) 1)
    ((letrec letrec*) 1)
    ((parameterize) 1)
    ((set!) 1)
    ((when unless) 1)
    ((with-exception-handler) 0)
    ((with-input-from-file call-with-input-file) 0)
    ((with-output-to-file call-with-output-file) 0)
    (else #f)))

;; A keyword may push a queue of indentation offsets onto the form
;; stack, repeatedly shifted to obtain the next indent level until it's
;; exhausted, at which point the final value applies to everything after.
(define (%shift-indent forms)
  (cond ((number? (car forms)) forms)        ; No queue.
        ((null? (cdar forms)) forms)         ; Exhausted.
        (else (cons (cdar forms) (cdr forms))))) ; Shift.

;; (format-scheme)                                     -> stdin, stdout, default keyword-indent
;; (format-scheme input)                                -> given input port, stdout
;; (format-scheme input output)                         -> both ports given
;; (format-scheme input output custom-keyword-indent)   -> custom indent-rule procedure, same
;;                                                          signature as keyword-indent above
(define (format-scheme . opts)
  (let* ((input  (if (pair? opts) (car opts) (current-input-port)))
         (opts   (if (pair? opts) (cdr opts) '()))
         (output (if (pair? opts) (car opts) (current-output-port)))
         (opts   (if (pair? opts) (cdr opts) '()))
         (custom-keyword-indent (if (pair? opts) (car opts) keyword-indent)))
    (let ((initial-indent (list (%read-indent input)))
          (display-line (lambda (line indent)
                          (unless (%string-every? line char-whitespace?)
                            (%display-indent indent output)
                            (display line output))
                          (newline output))))
      (let loop ((forms initial-indent))
        (unless (eof-object? (peek-char input))
          (let ((line (read-line input)))
            (let ((form (car forms)))
              (let-values (((line indent)
                            (if (symbol? form)
                                (values line 0)
                                (values (%string-trim-left line)
                                        (if (number? form) form (car form))))))
                (let ((len (string-length line)))
                  (let scan ((f forms) (i 0))
                    (define (open i)
                      (let* ((a (%guard/value #f (%string-read line i len)))
                             (k (%string-nth-read-index line 1 i len #f))
                             (f (%shift-indent f)))
                        (cond
                          ((%constant? a)
                           (scan (cons (+ i indent) f) i))
                          ((custom-keyword-indent a (not k))
                           => (lambda (offset)
                                (scan (if (number? offset)
                                          (cons (+ i offset indent) f)
                                          (cons (map (lambda (o) (+ i o indent)) offset) f))
                                      (or k len))))
                          ((not k)
                           (scan (cons (+ i indent) f) i))
                          (else
                           (scan (cons (+ k indent) f) k)))))
                    (define (close i) (scan (cdr f) i))
                    (define (datum i n)
                      (let ((k (%string-nth-read-index line 1 i len len)))
                        (if (not k)
                            (scan f (+ i n))
                            (scan (%shift-indent f) k))))
                    (cond
                      ((null? f) ; The front fell off.
                       (display-line line indent)
                       (loop initial-indent))
                      ((>= i len) ; End of line.
                       (display-line line indent)
                       (loop f))
                      (else
                       (case (car f)
                         ((comment)
                          (case (string-ref line i)
                            ((#\|) (case (%string-ref* line (+ i 1) #f)
                                     ((#\#) (scan (cdr f) (+ i 2)))
                                     (else  (scan f (+ i 1)))))
                            ((#\#) (case (%string-ref* line (+ i 1) #f)
                                     ((#\|) (scan (cons 'comment f) (+ i 2)))
                                     (else  (scan f (+ i 1)))))
                            (else  (scan f (+ i 1)))))
                         ((string)
                          (case (string-ref line i)
                            ((#\") (scan (cdr f) (+ i 1)))
                            ((#\\) (scan f (+ i 2)))
                            (else  (scan f (+ i 1)))))
                         ((symbol)
                          (case (string-ref line i)
                            ((#\|) (scan (cdr f) (+ i 1)))
                            ((#\\) (scan f (+ i 2)))
                            (else  (scan f (+ i 1)))))
                         (else
                          (case (string-ref line i)
                            ((#\;) (scan f len)) ; Skip to the end.
                            ((#\") (scan (cons 'string f) (+ i 1)))
                            ((#\|) (scan (cons 'symbol f) (+ i 1)))
                            ((#\() (open (+ i 1)))
                            ((#\)) (close (+ i 1)))
                            ((#\[) (cond
                                     ((bracket-parentheses?) (open (+ i 1)))
                                     (else (scan f (+ i 1)))))
                            ((#\]) (cond
                                     ((bracket-parentheses?) (close (+ i 1)))
                                     ((bracket-closure?)
                                      (set! line ; Insert closing parens for all of f.
                                        (string-append (%substring* line 0 i)
                                                        (make-string (length (cdr f)) #\))
                                                        (%substring* line (+ i 1) len)))
                                      (set! len (string-length line))
                                      (scan initial-indent i))
                                     (else (scan f (+ i 1)))))
                            ((#\#) (case (%string-ref* line (+ i 1) #f)
                                     ((#\|) (scan (cons 'comment f) (+ i 2)))
                                     ((#\;) (datum i 2))
                                     ((#\\) (datum i 3))
                                     (else  (datum i 1))))
                            (else  (datum i 1)))))))))))))))))

  )) ;; end begin, define-library
