;;; SRFI-272 fancy-tier implementation: everything from advanced, plus a
;;; pprint-file that keeps the source's comments and blank lines, reads
;;; file-local settings, and a pprint-file/html that marks up colors.
;;; https://srfi.schemers.org/srfi-272/
;;;
;;; pprint-file (with pp-decorate true) does not use `read`, which
;;; discards comments. It parses the source text itself into the engine's
;;; doc tree: each token keeps the text it was written with, own-line
;;; comments become elements of the list they sit in, and a comment that
;;; trails an element on its line stays with that element. A list holding
;;; a comment breaks (a line comment cannot be flat). With pp-decorate
;;; false the comments are dropped and forms are separated by one blank
;;; line. Because atoms keep their source text, pp-radix and pp-hooks do
;;; not apply to a file's contents; pp-level and pp-length do.
;;;
;;; File-local settings, both read from the file itself:
;;;   - the first Emacs-style "-*- key: value; ... -*-" line in the
;;;     header (leading blank, comment and #! lines). pp-width and
;;;     fill-column set pp-width; pp-inline-width, pp-miser-width, pp-tab,
;;;     pp-max-tab and pp-brackets set the parameter of that name. Values
;;;     are numbers, t, or nil. They override the parameters' current
;;;     values and are overridden by keyword arguments to pprint-file.
;;;   - top-level line comments of the form ";... * pp-styles: A := B; C := (_ ...)",
;;;     where the right side is an already-styled symbol or a style
;;;     starting with _. They apply to the whole file.

(define-library (srfi s272 fancy-impl)
  (import (scheme base) (scheme file) (scheme char) (scheme read)
          (srfi s272 engine) (srfi s272 advanced-impl))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file pprint-file/html
    pp-width pp-graph pp-circle pp-radix pp-level pp-length
    pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
    pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
    pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
    pp-styles pretty-style add-pp-style lookup-pp-style
    pp-hooks pretty-hook add-pp-hook lookup-pp-hook
    glst-pp-hook bvec-pp-hook atom-pp-hook rmac-pp-hook
    make-pprint-generator)
  (begin

    (define (%read-whole-file path)
      (call-with-input-file path
        (lambda (p)
          (let ((out (open-output-string)))
            (let loop ()
              (let ((c (read-char p)))
                (unless (eof-object? c) (write-char c out) (loop))))
            (get-output-string out)))))

    ;; ── Small string helpers ────────────────────────────────────────────────

    (define (%string-index-from s start pred)
      (let ((len (string-length s)))
        (let loop ((i start)) (cond ((>= i len) #f) ((pred (string-ref s i)) i) (else (loop (+ i 1)))))))

    (define (%find-substring haystack needle)
      (let ((hlen (string-length haystack)) (nlen (string-length needle)))
        (let loop ((i 0))
          (cond ((> (+ i nlen) hlen) #f)
                ((string=? (substring haystack i (+ i nlen)) needle) i)
                (else (loop (+ i 1)))))))

    (define (%trim s)
      (let* ((len (string-length s))
             (a (let loop ((i 0)) (if (and (< i len) (char-whitespace? (string-ref s i))) (loop (+ i 1)) i)))
             (b (let loop ((j len)) (if (and (> j a) (char-whitespace? (string-ref s (- j 1)))) (loop (- j 1)) j))))
        (substring s a b)))

    (define (%split-on s ch)
      (let ((len (string-length s)))
        (let loop ((i 0) (start 0) (acc '()))
          (cond ((>= i len) (reverse (cons (substring s start i) acc)))
                ((char=? (string-ref s i) ch) (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
                (else (loop (+ i 1) start acc))))))

    (define (%read-from-string s)
      (read (open-input-string s)))

    ;; ── Source parser ───────────────────────────────────────────────────────
    ;; Produces engine docs from source text. Each parse function takes
    ;; the text and an index and returns (values doc next-index).

    (define (%delimiter? c)
      (or (char-whitespace? c) (memv c '(#\( #\) #\[ #\] #\" #\;))))

    (define (%atom-end text i)
      (let ((len (string-length text)))
        (let loop ((i i))
          (cond ((>= i len) i)
                ((char=? (string-ref text i) #\|)
                 (let skip ((j (+ i 1)))
                   (cond ((>= j len) j)
                         ((char=? (string-ref text j) #\\) (skip (+ j 2)))
                         ((char=? (string-ref text j) #\|) (loop (+ j 1)))
                         (else (skip (+ j 1))))))
                ((%delimiter? (string-ref text i)) i)
                (else (loop (+ i 1)))))))

    (define (%string-end text i)       ; i is just past the opening quote
      (let ((len (string-length text)))
        (let loop ((i i))
          (cond ((>= i len) len)
                ((char=? (string-ref text i) #\\) (loop (+ i 2)))
                ((char=? (string-ref text i) #\") (+ i 1))
                (else (loop (+ i 1)))))))

    ;; End of a nested #| ... |# comment starting at i (just past "#|").
    (define (%block-comment-end text i)
      (let ((len (string-length text)))
        (let loop ((i i) (depth 1))
          (cond ((>= (+ i 1) len) len)
                ((and (char=? (string-ref text i) #\|) (char=? (string-ref text (+ i 1)) #\#))
                 (if (= depth 1) (+ i 2) (loop (+ i 2) (- depth 1))))
                ((and (char=? (string-ref text i) #\#) (char=? (string-ref text (+ i 1)) #\|))
                 (loop (+ i 2) (+ depth 1)))
                (else (loop (+ i 1) depth))))))

    (define (%line-end text i)
      (or (%string-index-from text i (lambda (c) (char=? c #\newline))) (string-length text)))

    (define (%token-kind tok)
      (let ((c (string-ref tok 0)))
        (cond ((char=? c #\") 'string)
              ((and (> (string-length tok) 1) (char=? c #\#) (char=? (string-ref tok 1) #\\)) 'char)
              ((member tok '("#t" "#f" "#true" "#false")) 'literal)
              ((string->number tok) 'number)
              ((char=? c #\#) 'literal)
              (else 'symbol))))

    (define (%token->atom tok)
      (let ((kind (%token-kind tok)))
        (%doc-atom tok kind
                   (case kind
                     ((symbol) (if (%find-substring tok "|") (%read-from-string tok) (string->symbol tok)))
                     (else #f)))))

    (define (%parse-error what text i)
      (error (string-append "pprint-file: " what) i))

    ;; Parses one datum at i (text[i] starts a datum).
    (define (%parse-datum text i)
      (let ((len (string-length text)) (c (string-ref text i)))
        (cond
          ((or (char=? c #\() (char=? c #\[))
           (%parse-list text (+ i 1) "(" ")" 'list))
          ((char=? c #\")
           (let ((e (%string-end text (+ i 1)))) (values (%token->atom (substring text i e)) e)))
          ((char=? c #\')  (%parse-prefixed text i 1 "'"))
          ((char=? c #\`)  (%parse-prefixed text i 1 "`"))
          ((char=? c #\,)
           (if (and (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\@))
               (%parse-prefixed text i 2 ",@")
               (%parse-prefixed text i 1 ",")))
          ((and (char=? c #\#) (< (+ i 1) len))
           (let ((d (string-ref text (+ i 1))))
             (cond
               ((char=? d #\() (%parse-list text (+ i 2) "#(" ")" 'vector))
               ((and (char=? d #\u) (< (+ i 3) len) (char=? (string-ref text (+ i 2)) #\8)
                     (char=? (string-ref text (+ i 3)) #\())
                (%parse-list text (+ i 4) "#u8(" ")" 'hook))
               ((char=? d #\;) (%parse-prefixed text i 2 "#;"))
               ((char=? d #\\)
                ;; #\ takes one character whatever it is, then any
                ;; non-delimiter characters (#\space, #\x41, #\( ...).
                (let ((e (%atom-end text (min len (+ i 3)))))
                  (values (%token->atom (substring text i e)) e)))
               (else (let ((e (%atom-end text i))) (values (%token->atom (substring text i e)) e))))))
          (else
           (let ((e (%atom-end text i)))
             (if (= e i)
                 (%parse-error "unexpected character" text i)
                 (values (%token->atom (substring text i e)) e)))))))

    ;; A reader prefix (' ` , ,@ #;) followed by the datum it applies to.
    (define (%parse-prefixed text i n prefix)
      (let ((j (%skip-trivia text (+ i n))))
        (if (>= j (string-length text))
            (%parse-error "prefix with nothing after it" text i)
            (call-with-values (lambda () (%parse-datum text j))
              (lambda (child e) (values (%doc-prefixed prefix child) e))))))

    ;; Skips whitespace and comments (a prefix's operand may follow them).
    (define (%skip-trivia text i)
      (let ((len (string-length text)))
        (let loop ((i i))
          (cond ((>= i len) i)
                ((char-whitespace? (string-ref text i)) (loop (+ i 1)))
                ((char=? (string-ref text i) #\;) (loop (%line-end text i)))
                ((and (char=? (string-ref text i) #\#) (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\|))
                 (loop (%block-comment-end text (+ i 2))))
                (else i)))))

    ;; Parses list elements from i up to the closing delimiter.
    (define (%parse-list text i open close kind)
      (let ((len (string-length text)))
        (let loop ((i i) (kids '()) (newline? #f) (dot? #f))
          (cond
            ((>= i len) (%parse-error "unterminated list" text i))
            (else
             (let ((c (string-ref text i)))
               (cond
                 ((char=? c #\newline) (loop (+ i 1) kids #t dot?))
                 ((char-whitespace? c) (loop (+ i 1) kids newline? dot?))
                 ((or (char=? c #\)) (char=? c #\]))
                  (values (%doc-group open close kind (reverse kids)) (+ i 1)))
                 ((char=? c #\;)
                  (let ((e (%line-end text i)))
                    (loop e (cons (%doc-comment (%trim (substring text i e)) (and (pair? kids) (not newline?))) kids)
                          newline? dot?)))
                 ((and (char=? c #\#) (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\|))
                  (let ((e (%block-comment-end text (+ i 2))))
                    (loop e (cons (%doc-comment (substring text i e) (and (pair? kids) (not newline?))) kids)
                          newline? dot?)))
                 ((and (char=? c #\.) (or (>= (+ i 1) len) (%delimiter? (string-ref text (+ i 1)))))
                  (loop (+ i 1) kids #f #t))
                 (else
                  (call-with-values (lambda () (%parse-datum text i))
                    (lambda (d e)
                      (loop e (cons (if dot? (%doc-dot-tail d) d) kids) #f #f)))))))))))

    ;; ── Top-level items ─────────────────────────────────────────────────────
    ;; A list of (blank-lines kind doc eol-comment): kind is 'comment or
    ;; 'form; eol-comment is a trailing comment's text, or #f.

    (define (%parse-source text)
      (let ((len (string-length text)))
        (let loop ((i 0) (blanks 0) (items '()))
          (cond
            ((>= i len) (reverse items))
            (else
             (let ((c (string-ref text i)))
               (cond
                 ((char=? c #\newline) (loop (+ i 1) (+ blanks 1) items))
                 ((char-whitespace? c) (loop (+ i 1) blanks items))
                 ((char=? c #\;)
                  (let ((e (%line-end text i)))
                    (loop e 0 (cons (list (max 0 (- blanks 1)) 'comment (%doc-comment (%trim (substring text i e)) #f) #f) items))))
                 ((and (char=? c #\#) (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\|))
                  (let ((e (%block-comment-end text (+ i 2))))
                    (loop e 0 (cons (list (max 0 (- blanks 1)) 'comment (%doc-comment (substring text i e) #f) #f) items))))
                 ((and (char=? c #\#) (< (+ i 1) len) (char=? (string-ref text (+ i 1)) #\!))
                  (let ((e (%line-end text i)))
                    (loop e 0 (cons (list (max 0 (- blanks 1)) 'comment (%doc-comment (substring text i e) #f) #f) items))))
                 (else
                  (call-with-values (lambda () (%parse-datum text i))
                    (lambda (d e)
                      ;; A comment on the same line after the form trails it.
                      (let* ((j (let skip ((j e)) (if (and (< j len) (memv (string-ref text j) '(#\space #\tab))) (skip (+ j 1)) j)))
                             (trail? (and (< j len) (char=? (string-ref text j) #\;)))
                             (te (and trail? (%line-end text j))))
                        (loop (if trail? te e) 0
                              (cons (list (max 0 (- blanks 1)) 'form d (and trail? (%trim (substring text j te)))) items)))))))))))))

    ;; ── pp-level / pp-length on a parsed doc ────────────────────────────────

    (define (%limit-doc doc depth)
      (let ((level (pp-level)) (maxlen (pp-length)))
        (cond
          ((not (and (pair? doc) (eq? (car doc) 'group))) doc)
          ((and level (>= depth level))
           (if (eq? (pp-level-stub) #t)
               (%doc-group (cadr doc) (caddr doc) (cadddr doc) (list (%doc-atom (pp-length-stub) 'stub #f)))
               (%doc-atom (pp-level-stub) 'stub #f)))
          (else
           (let loop ((kids (cddddr doc)) (n 0) (acc '()))
             (cond
               ((null? kids) (%doc-group (cadr doc) (caddr doc) (cadddr doc) (reverse acc)))
               ((and maxlen (>= n maxlen) (not (and (pair? (car kids)) (eq? (caar kids) 'comment))))
                (%doc-group (cadr doc) (caddr doc) (cadddr doc) (reverse (cons (%doc-atom (pp-length-stub) 'stub #f) acc))))
               (else (loop (cdr kids) (if (and (pair? (car kids)) (eq? (caar kids) 'comment)) n (+ n 1))
                           (cons (let ((k (car kids)))
                                   (cond ((and (pair? k) (eq? (car k) 'group)) (%limit-doc k (+ depth 1)))
                                         ((and (pair? k) (eq? (car k) 'prefixed))
                                          (cons 'prefixed (cons (cadr k) (%limit-doc (cddr k) (+ depth 1)))))
                                         (else k)))
                                 acc)))))))))

    ;; ── File-local settings ─────────────────────────────────────────────────

    ;; Header lines: leading blank, comment and #! lines.
    (define (%header-lines text)
      (let loop ((lines (%split-on text #\newline)) (acc '()))
        (if (null? lines)
            (reverse acc)
            (let ((t (%trim (car lines))))
              (if (or (string=? t "") (char=? (string-ref t 0) #\;)
                      (and (> (string-length t) 1) (char=? (string-ref t 0) #\#) (char=? (string-ref t 1) #\!)))
                  (loop (cdr lines) (cons (car lines) acc))
                  (reverse acc))))))

    ;; The text between the first -*- ... -*- in the header, or #f.
    (define (%local-variables-text text)
      (let loop ((lines (%header-lines text)))
        (if (null? lines)
            #f
            (let ((a (%find-substring (car lines) "-*-")))
              (if (not a)
                  (loop (cdr lines))
                  (let* ((rest (substring (car lines) (+ a 3) (string-length (car lines))))
                         (b (%find-substring rest "-*-")))
                    (if b (substring rest 0 b) (loop (cdr lines)))))))))

    (define %local-variable-params
      (list (cons "pp-width" pp-width) (cons "fill-column" pp-width)
            (cons "pp-inline-width" pp-inline-width) (cons "pp-miser-width" pp-miser-width)
            (cons "pp-tab" pp-tab) (cons "pp-max-tab" pp-max-tab) (cons "pp-brackets" pp-brackets)))

    (define (%local-variable-value s)
      (let ((t (%trim s)))
        (cond ((string=? t "t") (cons #t #t))
              ((string=? t "nil") (cons #t #f))
              ((string->number t) => (lambda (n) (cons #t n)))
              (else (cons #f #f)))))

    ;; Returns (params . values): the file-local parameters to bind.
    (define (%file-local-bindings text)
      (let ((body (%local-variables-text text)))
        (let loop ((pairs (if body (%split-on body #\;) '())) (params '()) (vals '()))
          (if (null? pairs)
              (cons params vals)
              (let* ((pair (car pairs)) (colon (%string-index-from pair 0 (lambda (c) (char=? c #\:)))))
                (if (not colon)
                    (loop (cdr pairs) params vals)
                    (let* ((key (string-downcase (%trim (substring pair 0 colon))))
                           (param (assoc key %local-variable-params))
                           (v (%local-variable-value (substring pair (+ colon 1) (string-length pair)))))
                      (if (and param (car v) (not (memq (cdr param) params)))
                          (loop (cdr pairs) (cons (cdr param) params) (cons (cdr v) vals))
                          (loop (cdr pairs) params vals)))))))))

    ;; Style assignments from ";... * pp-styles: A := B; C := (_ ...)" comments.
    (define (%style-comment-body comment-text)
      (let ((i (let skip ((i 0)) (if (and (< i (string-length comment-text)) (char=? (string-ref comment-text i) #\;)) (skip (+ i 1)) i))))
        (let* ((rest (%trim (substring comment-text i (string-length comment-text)))))
          (and (> (string-length rest) 0) (char=? (string-ref rest 0) #\*)
               (let ((r (%trim (substring rest 1 (string-length rest)))))
                 (and (>= (string-length r) 10) (string=? (substring r 0 10) "pp-styles:")
                      (substring r 10 (string-length r))))))))

    (define (%apply-style-assignments registry body)
      (let loop ((parts (%split-on body #\;)) (registry registry))
        (if (null? parts)
            registry
            (let ((a (%find-substring (car parts) ":=")))
              (if (not a)
                  (loop (cdr parts) registry)
                  (let* ((name (%trim (substring (car parts) 0 a)))
                         (rhs (%trim (substring (car parts) (+ a 2) (string-length (car parts))))))
                    (if (or (string=? name "") (string=? rhs ""))
                        (loop (cdr parts) registry)
                        (let* ((sym (string->symbol name))
                               (style (if (char=? (string-ref rhs 0) #\()
                                          (%read-from-string rhs)
                                          (lookup-pp-style registry (string->symbol rhs)))))
                          (loop (cdr parts) (if style (add-pp-style registry sym style) registry))))))))))

    (define (%file-styles registry items)
      (let loop ((items items) (registry registry))
        (cond ((null? items) registry)
              ((eq? (cadr (car items)) 'comment)
               (let ((body (%style-comment-body (cadr (caddr (car items))))))
                 (loop (cdr items) (if body (%apply-style-assignments registry body) registry))))
              (else (loop (cdr items) registry)))))

    ;; ── pprint-file ─────────────────────────────────────────────────────────

    (define (%parameterize-bindings params vals thunk)
      (if (null? params)
          (thunk)
          (let ((p (car params)) (v (car vals)) (old #f))
            (dynamic-wind
              (lambda () (set! old (p)) (p v))
              (lambda () (%parameterize-bindings (cdr params) (cdr vals) thunk))
              (lambda () (p old))))))

    (define (%emit-blank-lines n out)
      (let loop ((i 0)) (when (< i (min n 2)) (write-char #\newline out) (loop (+ i 1)))))

    (define (%pprint-items items out)
      (let ((decorate? (and (pp-decorate) #t)))
        (let loop ((items items) (first? #t))
          (when (pair? items)
            (let* ((blanks (car (car items))) (kind (cadr (car items)))
                   (doc (caddr (car items))) (trail (cadddr (car items))))
              (cond
                ((eq? kind 'comment)
                 (when decorate?
                   (unless first? (%emit-blank-lines blanks out))
                   (%pp-doc-to-port doc out))
                 (loop (cdr items) (and first? (not decorate?))))
                (else
                 (unless first? (if decorate? (%emit-blank-lines blanks out) (newline out)))
                 (let ((d (if decorate? (%limit-doc doc 0) (%strip-comments (%limit-doc doc 0)))))
                   (if (and decorate? trail)
                       (%pp-doc-to-port d out (string-append "  " trail))
                       (%pp-doc-to-port d out)))
                 (loop (cdr items) #f))))))))

    ;; pp-decorate false: forms only, comments removed from inside them too.
    (define (%strip-comments doc)
      (cond
        ((and (pair? doc) (eq? (car doc) 'group))
         (%doc-group (cadr doc) (caddr doc) (cadddr doc)
                     (let loop ((kids (cddddr doc)) (acc '()))
                       (cond ((null? kids) (reverse acc))
                             ((and (pair? (car kids)) (eq? (caar kids) 'comment)) (loop (cdr kids) acc))
                             (else (loop (cdr kids) (cons (%strip-comments (car kids)) acc)))))))
        ((and (pair? doc) (eq? (car doc) 'prefixed))
         (cons 'prefixed (cons (cadr doc) (%strip-comments (cddr doc)))))
        ((and (pair? doc) (eq? (car doc) 'dot-tail)) (cons 'dot-tail (%strip-comments (cdr doc))))
        (else doc)))

    ;; wrap, if given, is called with a thunk and runs it in an extra
    ;; dynamic extent inside the file-local settings and the keyword
    ;; arguments (pprint-file/html binds its output hooks there, where it
    ;; can see the final pp-color).
    (define (%pprint-file-to-port infile out kvs wrap)
      (let* ((text (%read-whole-file infile))
             (items (%parse-source text))
             (local (%file-local-bindings text)))
        ;; Parameters from the file sit under keyword arguments.
        (%parameterize-bindings (car local) (cdr local)
          (lambda ()
            (%pp-with-kwargs kvs
              (lambda ()
                (let ((registry (%file-styles (pp-styles) items)))
                  (%parameterize-bindings (list pp-styles) (list registry)
                    (lambda () (wrap (lambda () (%pprint-items items out))))))))))))

    ;; outfile is a filename string per the spec; an already-open output
    ;; port is also accepted (used as is, never closed).
    (define (%split-outfile-and-kvs args)
      (cond
        ((and (pair? args) (string? (car args))) (values (open-output-file (car args)) #t (cdr args)))
        ((and (pair? args) (port? (car args))) (values (car args) #f (cdr args)))
        (else (values (current-output-port) #f args))))

    (define (pprint-file infile . args)
      (call-with-values (lambda () (%split-outfile-and-kvs args))
        (lambda (out owns-out? kvs)
          (%pprint-file-to-port infile out kvs (lambda (thunk) (thunk)))
          (when owns-out? (close-port out)))))

    ;; ── HTML ─────────────────────────────────────────────────────────────────

    (define (%html-escape s)
      (let ((out (open-output-string)))
        (string-for-each
          (lambda (c)
            (case c
              ((#\<) (write-string "&lt;" out))
              ((#\>) (write-string "&gt;" out))
              ((#\&) (write-string "&amp;" out))
              (else (write-char c out))))
          s)
        (get-output-string out)))

    ;; A semantic color mapper producing <span class="pp-CATEGORY"> markup.
    (define (%html-color-mapper sc start?)
      (if start?
          (string-append "<span class=\"pp-" (symbol->string sc) "\">")
          "</span>"))

    ;; Text is escaped; the mapper's markup is not. With pp-color true the
    ;; HTML mapper is used; a procedure given as pp-color is used as is.
    (define (pprint-file/html infile . args)
      (call-with-values (lambda () (%split-outfile-and-kvs args))
        (lambda (out owns-out? kvs)
          (write-string "<pre>" out)
          (%pprint-file-to-port infile out kvs
            (lambda (thunk)
              (%parameterize-bindings
                (list pp-emit pp-tint pp-color)
                (list (lambda (s p) (write-string (%html-escape s) p))
                      (lambda (s p) (write-string s p))
                      (let ((c (pp-color))) (if (eq? c #t) %html-color-mapper c)))
                thunk)))
          (write-string "</pre>" out)
          (when owns-out? (close-port out)))))))
