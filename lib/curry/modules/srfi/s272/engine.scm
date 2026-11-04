;;; SRFI-272 shared engine: the real pretty-printing machinery every
;;; (srfi 272 ...) sub-library is built on. Not one of the SRFI's own
;;; public library names -- an internal implementation detail, exported
;;; broadly (a much wider surface than any one public library needs) so
;;; each public .sld shim can re-export exactly its own documented subset
;;; without duplicating logic.
;;;
;;; https://srfi.schemers.org/srfi-272/ -- pretty printing, layered:
;;; (srfi 272) minimalist -> basic -> intermediate -> advanced -> fancy,
;;; plus independent colorize/measure libraries and a show bridge to
;;; SRFI 166. Status: FINAL (2026-11-01).
;;;
;;; ── Pipeline ────────────────────────────────────────────────────────
;;; 1. Label assignment: two eq?-hash-table scans find shared objects
;;;    (graph mode) or genuine cycles (circle mode); simple mode scans
;;;    nothing.
;;; 2. Doc building: the datum becomes a small tree, independent of every
;;;    layout parameter (only pp-level/pp-length/labels/radix shape it):
;;;      (atom segs kind obj)       segs = ((semantic-color-or-#f . text) ...)
;;;      (group open close kind kids...)   kind: list | vector | hook
;;;      (prefixed segs . doc)      a read macro: 'x `x ,x ,@x
;;;      (dot-tail . doc)           an improper tail, printed ". x"
;;;      (labeled n . doc)          "#n=" before a shared/circular object
;;;      (labelref . n)             "#n#"
;;; 3. Layout: a recursive fits-or-break pass. A group that fits the
;;;    remaining width (counting the closing delimiters that must follow
;;;    it on the same line) prints flat. Otherwise it breaks according to
;;;    its style: a form whose head symbol has a style in pp-styles (the
;;;    grammar of the SRFI: body, fill, dc*/ec*/fc*/lc*, i?, ...) uses
;;;    it; any other form with a symbol head lays out as a procedure
;;;    application; everything else is a data list, one element per line.
;;;    A form's head is canonicalized with lang-translate first, so the
;;;    Akkadian and cuneiform spellings of a special form use the style
;;;    of the English one.
;;; 4. Output: layout produces a list of pieces, (emit . text) for
;;;    ordinary output and (tint . text) for color sequences, which is
;;;    what pp-lines truncation and make-pprint-generator work on, and
;;;    what is finally replayed through pp-emit / pp-tint.
;;;
;;; Every width is measured with %string-width ((srfi 272 measure)): a
;;; terminal-column count that treats cuneiform and Akkadian
;;; transliteration correctly (signs are 1 column, combining marks 0),
;;; East Asian wide characters and emoji as 2, and keeps ZWJ sequences
;;; and flags together.

(define-library (srfi s272 engine)
  (import (scheme base) (scheme write) (scheme inexact)
          (srfi s69 hash-tables)
          (srfi s158 generators-and-accumulators)
          (srfi s272 measure-impl))
  (export
    ;; Parameters (basic tier)
    pp-width pp-graph pp-circle
    ;; Parameters (intermediate tier)
    pp-radix pp-level pp-length
    ;; Parameters (advanced tier)
    pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
    pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
    pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
    pp-styles pp-hooks
    ;; Style registry
    add-pp-style lookup-pp-style pretty-style
    ;; Hook registry + constructors
    add-pp-hook lookup-pp-hook pretty-hook
    glst-pp-hook bvec-pp-hook atom-pp-hook rmac-pp-hook pp-hook?
    ;; Core rendering entry points
    %pp-to-port %pp-lines-list %current-mode %render-to-string
    ;; Keyword-argument helper (shared by every pp/pprint variant)
    %pp-with-kwargs
    ;; Colorize integration, and the default style registry
    %set-default-color-mapper! %default-styles
    ;; Width of a string in terminal columns
    %string-width)
  (begin

    ;; ── Parameters ───────────────────────────────────────────────────────────

    (define pp-width (make-parameter 79))
    (define pp-graph (make-parameter #f))
    (define pp-circle (make-parameter #t))

    (define pp-radix (make-parameter 10))
    (define pp-level (make-parameter #f))
    (define pp-length (make-parameter #f))

    (define pp-lines (make-parameter #f))
    (define pp-level-stub (make-parameter "..."))
    (define pp-length-stub (make-parameter "..."))
    (define pp-lines-stub (make-parameter #f))
    (define pp-newline (make-parameter #t))
    (define pp-inline-width (make-parameter #f))
    (define pp-miser-width (make-parameter 40))
    (define pp-indent (make-parameter 0))
    (define pp-tab (make-parameter 1))
    (define pp-max-tab (make-parameter 10))
    (define pp-code (make-parameter #t))
    (define pp-brackets (make-parameter #f))
    (define pp-pretty (make-parameter #t))
    (define pp-color (make-parameter #f))
    (define pp-decorate (make-parameter #t))
    (define pp-emit (make-parameter (lambda (s port) (write-string s port))))
    (define pp-tint (make-parameter (lambda (s port) (write-string s port))))

    ;; ── Style registry ───────────────────────────────────────────────────────
    ;; A style registry is an alist (symbol . style); functional update
    ;; per the spec ("returns a new registry"). pp-styles starts out as
    ;; %default-styles, the conventions for the R7RS special forms; the
    ;; grammar is the SRFI's:
    ;;   style     ::= (_ . fmt-tail)
    ;;   fmt-tail  ::= body | fill | dc* | ec* | fc* | lc*
    ;;               | (i? . fmt-tail) | (fmt . fmt-tail)
    ;; A code before the tail is a distinguished argument kept on the
    ;; form's first line (i identifier, d datum, e expression, f formals,
    ;; l literals, h definition head, dc/ec/fc/lc one clause, dc*/ec*/
    ;; fc*/lc* a list of clauses). The tail lays out the remaining
    ;; arguments: body indents them, fill packs them, and a starred
    ;; clause code lines them up as clauses.

    (define %default-styles
      '((define . (_ h . body))
        (define-values . (_ f . body))
        (define-syntax . (_ i . body))
        (define-record-type . (_ h e . body))
        (define-library . (_ e . body))
        (lambda . (_ f . body))
        (named-lambda . (_ h . body))
        (let . (_ i? fc* . body))
        (let* . (_ fc* . body))
        (letrec . (_ fc* . body))
        (letrec* . (_ fc* . body))
        (let-values . (_ fc* . body))
        (let*-values . (_ fc* . body))
        (let-syntax . (_ fc* . body))
        (letrec-syntax . (_ fc* . body))
        (parameterize . (_ fc* . body))
        (if . (_ . body))
        (when . (_ e . body))
        (unless . (_ e . body))
        (begin . (_ . body))
        (or . (_ . fill))
        (and . (_ . fill))
        (do . (_ fc* ec . body))
        (cond . (_ . ec*))
        (case . (_ e . lc*))
        (guard . (_ (f . ec*) . body))
        (case-lambda . (_ . fc*))))

    (define pp-styles (make-parameter %default-styles))

    (define (add-pp-style registry symbol style)
      (let ((without (let loop ((r registry) (acc '()))
                       (cond ((null? r) (reverse acc))
                             ((eq? (caar r) symbol) (loop (cdr r) acc))
                             (else (loop (cdr r) (cons (car r) acc)))))))
        (if (not style) without (cons (cons symbol style) without))))

    (define (lookup-pp-style registry symbol)
      (let ((hit (assq symbol registry))) (and hit (cdr hit))))

    ;; (pretty-style sym) reads and (pretty-style sym style) sets the
    ;; style of sym in the current pp-styles binding.
    (define (pretty-style symbol . maybe-style)
      (if (pair? maybe-style)
          (begin (pp-styles (add-pp-style (pp-styles) symbol (car maybe-style))) (if #f #f))
          (lookup-pp-style (pp-styles) symbol)))

    ;; ── Hook registry ────────────────────────────────────────────────────────
    ;; A hook registry is an alist (test . hook); "new associations added
    ;; to the beginning; existing same-test associations replaced in
    ;; place", implemented literally.

    (define-record-type <pp-hook>
      (%make-pp-hook kind render-proc extra) pp-hook? (kind %hook-kind) (render-proc %hook-render) (extra %hook-extra))

    (define pp-hooks (make-parameter '()))

    (define (add-pp-hook registry test hook)
      (if (not hook)
          (let loop ((r registry) (acc '()))
            (cond ((null? r) (reverse acc))
                  ((eq? (caar r) test) (append (reverse acc) (cdr r)))
                  (else (loop (cdr r) (cons (car r) acc)))))
          (let ((existing (assq test registry)))
            (if existing
                (map (lambda (kv) (if (eq? (car kv) test) (cons test hook) kv)) registry)
                (cons (cons test hook) registry)))))

    (define (lookup-pp-hook registry test)
      (let ((hit (assq test registry))) (if hit (cdr hit) #f)))

    (define (pretty-hook test . maybe-hook)
      (if (pair? maybe-hook)
          (begin (pp-hooks (add-pp-hook (pp-hooks) test (car maybe-hook))) (if #f #f))
          (lookup-pp-hook (pp-hooks) test)))

    ;; Hook constructors. Each produces an opaque <pp-hook> whose
    ;; render-proc turns a matching object into a doc, given a procedure
    ;; that builds the doc of a sub-object.

    (define (glst-pp-hook prefix cf xf suffix)
      (%make-pp-hook 'glst (lambda (obj build) (%doc-group prefix suffix 'hook (map build (cf obj)))) xf))
    (define (bvec-pp-hook prefix lf rf suffix)
      (%make-pp-hook 'bvec
        (lambda (obj build)
          (%doc-group prefix suffix 'hook
            (let loop ((i 0) (acc '()))
              (if (>= i (lf obj)) (reverse acc) (loop (+ i 1) (cons (build (rf obj i)) acc))))))
        #f))
    ;; wf (printed width) is accepted but unused: widths are measured
    ;; from the strings df returns. df returns strings, each optionally
    ;; preceded by a semantic color symbol.
    (define (atom-pp-hook shareable? wf df)
      (%make-pp-hook 'atom
        (lambda (obj build) (list 'atom (%df->segs (df obj (pp-radix))) 'hook obj))
        shareable?))
    (define (rmac-pp-hook prefix ef xf)
      (%make-pp-hook 'rmac
        (lambda (obj build) (list* 'prefixed (list (cons 'meta prefix)) (build (ef obj))))
        xf))

    (define (%df->segs items)
      (let loop ((items items) (sc #f) (acc '()))
        (cond ((null? items) (reverse acc))
              ((symbol? (car items)) (loop (cdr items) (car items) acc))
              (else (loop (cdr items) #f (cons (cons sc (car items)) acc))))))

    ;; ── Cycle/sharing detection ──────────────────────────────────────────────
    ;;
    ;; All traversals are explicit work-stack loops. This library is a
    ;; define-library body, tree-walked via eval() (see CLAUDE.md's
    ;; stack-depth guard), so plain car/cdr recursion would overflow it on
    ;; any ordinary list longer than a few hundred elements. Every `loop`
    ;; call is a tail call, so stack depth stays O(1) in the input length.

    (define (%shareable? obj) (or (pair? obj) (vector? obj)))

    (define (%children o)
      (if (pair? o) (list (car o) (cdr o)) (vector->list o)))

    ;; DFS with post-order 'leave markers so on-stack means "currently
    ;; being visited" (genuine cycles) rather than "ever visited".
    (define (%scan-cycles! obj on-stack done cyclic)
      (let loop ((stack (list (cons 'visit obj))))
        (if (null? stack)
            (if #f #f)
            (let ((tag (caar stack)) (o (cdar stack)) (rest (cdr stack)))
              (cond
                ((eq? tag 'leave)
                 (hash-table-delete! on-stack o)
                 (hash-table-set! done o #t)
                 (loop rest))
                ((not (%shareable? o)) (loop rest))
                ((hash-table-exists? cyclic o) (loop rest))
                ((hash-table-exists? on-stack o) (hash-table-set! cyclic o #t) (loop rest))
                ((hash-table-exists? done o) (loop rest))
                (else
                 (hash-table-set! on-stack o #t)
                 (loop (append (map (lambda (c) (cons 'visit c)) (%children o))
                               (cons (cons 'leave o) rest)))))))))

    (define (%scan-refcounts! obj counts done)
      (let loop ((stack (list obj)))
        (if (null? stack)
            (if #f #f)
            (let ((o (car stack)) (rest (cdr stack)))
              (cond
                ((not (%shareable? o)) (loop rest))
                (else
                 (hash-table-update!/default counts o (lambda (n) (+ n 1)) 0)
                 (if (hash-table-exists? done o)
                     (loop rest)
                     (begin
                       (hash-table-set! done o #t)
                       (loop (append (%children o) rest))))))))))

    ;; mode is 'graph, 'circle, or 'simple. Returns an eq?-hash-table from
    ;; object -> label number, for exactly the objects that need one.
    (define (%assign-labels obj mode)
      (let ((labels (make-hash-table eq?)))
        (if (eq? mode 'simple)
            labels
            (let* ((counts (make-hash-table eq?))
                   (done1 (make-hash-table eq?))
                   (cyclic (make-hash-table eq?)))
              (case mode
                ((graph) (%scan-refcounts! obj counts done1))
                ((circle) (%scan-cycles! obj (make-hash-table eq?) done1 cyclic)))
              (let ((needs-label?
                      (case mode
                        ((graph) (lambda (o) (> (hash-table-ref/default counts o 0) 1)))
                        ((circle) (lambda (o) (hash-table-ref/default cyclic o #f)))
                        (else (lambda (o) #f))))
                    (seen (make-hash-table eq?))
                    (next 0))
                (let loop ((stack (list obj)))
                  (if (null? stack)
                      labels
                      (let ((o (car stack)) (rest (cdr stack)))
                        (cond
                          ((or (not (%shareable? o)) (hash-table-exists? seen o)) (loop rest))
                          (else
                           (hash-table-set! seen o #t)
                           (when (needs-label? o)
                             (hash-table-set! labels o next)
                             (set! next (+ next 1)))
                           (loop (append (%children o) rest))))))))))))

    ;; ── Doc constructors ─────────────────────────────────────────────────────

    (define (list* . args)
      (let loop ((args args))
        (cond ((null? args) '())
              ((null? (cdr args)) (car args))
              (else (cons (car args) (loop (cdr args)))))))

    (define (%doc-atom text kind obj) (list 'atom (list (cons #f text)) kind obj))
    (define (%doc-group open close kind kids) (list* 'group open close kind kids))
    (define (%doc-dot-tail doc) (cons 'dot-tail doc))
    (define (%doc-labeled n doc) (list* 'labeled n doc))
    (define (%doc-labelref n) (cons 'labelref n))
    (define (%tag? d t) (and (pair? d) (eq? (car d) t)))
    (define (%doc-atom? d) (%tag? d 'atom))
    (define (%doc-group? d) (%tag? d 'group))
    (define (%doc-dot-tail? d) (%tag? d 'dot-tail))
    (define (%doc-labeled? d) (%tag? d 'labeled))
    (define (%doc-labelref? d) (%tag? d 'labelref))
    (define (%doc-prefixed? d) (%tag? d 'prefixed))

    (define (%group-open g) (cadr g))
    (define (%group-close g) (caddr g))
    (define (%group-kind g) (cadddr g))
    (define (%group-kids g) (cddddr g))
    (define (%atom-segs a) (cadr a))
    (define (%atom-kind a) (caddr a))
    (define (%atom-obj a) (cadddr a))

    ;; ── Atom text ────────────────────────────────────────────────────────────
    ;; Numbers honor pp-radix; everything else, and decimal numbers, go
    ;; through `write`, which also honors current-number-notation (so a
    ;; sexagesimal or cuneiform numeral prints in the reader's own
    ;; notation).

    (define (%write->string obj)
      (let ((p (open-output-string))) (write obj p) (get-output-string p)))

    (define (%atom-text obj)
      (if (and (number? obj) (exact? obj) (not (= (pp-radix) 10)))
          (string-append (case (pp-radix) ((2) "#b") ((8) "#o") ((16) "#x") (else ""))
                         (number->string obj (pp-radix)))
          (%write->string obj)))

    (define (%atom-kind-of obj)
      (cond ((symbol? obj) 'symbol) ((string? obj) 'string) ((char? obj) 'char)
            ((number? obj) 'number) (else 'literal)))

    ;; A pp-hooks entry is (test . hookval). If hookval is a hook, test is
    ;; an ordinary predicate; if it is #t, test itself returns a hook (or
    ;; #f) for the object.
    (define (%find-matching-hook obj)
      (let loop ((hs (pp-hooks)))
        (if (null? hs)
            #f
            (let* ((test (caar hs)) (hookval (cdar hs)))
              (if (eq? hookval #t)
                  (let ((h (test obj))) (if (pp-hook? h) h (loop (cdr hs))))
                  (if (test obj) hookval (loop (cdr hs))))))))

    ;; ── Doc building ─────────────────────────────────────────────────────────

    (define (%read-macro-prefix obj)
      (and (pair? obj) (pair? (cdr obj)) (null? (cddr obj))
           (case (car obj)
             ((quote) "'") ((quasiquote) "`") ((unquote) ",") ((unquote-splicing) ",@")
             (else #f))))

    (define (%build-doc obj depth labels printed mode)
      (let ((hook (%find-matching-hook obj)))
        (cond
          (hook ((%hook-render hook) obj (lambda (sub) (%build-doc sub (+ depth 1) labels printed mode))))
          ;; The root is level 0; a component at a level equal to or
          ;; exceeding pp-level is stubbed.
          ((and (pp-level) (>= depth (pp-level)) (%shareable? obj))
           (%level-stub obj))
          ((and (not (eq? mode 'simple)) (%shareable? obj) (hash-table-exists? labels obj))
           (let ((n (hash-table-ref labels obj)))
             (if (hash-table-exists? printed obj)
                 (%doc-labelref n)
                 (begin (hash-table-set! printed obj #t)
                        (%doc-labeled n (%build-doc-body obj depth labels printed mode))))))
          ((%shareable? obj) (%build-doc-body obj depth labels printed mode))
          (else (%doc-atom (%atom-text obj) (%atom-kind-of obj) obj)))))

    ;; pp-level-stub #t is the "shell" form: the object keeps its
    ;; delimiters and its contents become the length stub.
    (define (%level-stub obj)
      (if (eq? (pp-level-stub) #t)
          (%doc-group (if (vector? obj) "#(" "(") ")" (if (vector? obj) 'vector 'list)
                      (list (%doc-atom (pp-length-stub) 'stub #f)))
          (%doc-atom (pp-level-stub) 'stub #f)))

    (define (%build-doc-body obj depth labels printed mode)
      (cond
        ((vector? obj)
         (%doc-group "#(" ")" 'vector
           (%build-doc-elements (vector->list obj) (+ depth 1) labels printed mode)))
        ((and (%read-macro-prefix obj)
              (not (and (not (eq? mode 'simple)) (hash-table-exists? labels (cdr obj)))))
         (list* 'prefixed (list (cons 'meta (%read-macro-prefix obj)))
                (%build-doc (cadr obj) (+ depth 1) labels printed mode)))
        (else (%build-doc-pair obj (+ depth 1) labels printed mode))))

    (define (%build-doc-elements lst depth labels printed mode)
      (let ((maxlen (pp-length)))
        (let loop ((lst lst) (n 0) (acc '()))
          (cond
            ((and maxlen (>= n maxlen) (pair? lst)) (reverse (cons (%doc-atom (pp-length-stub) 'stub #f) acc)))
            ((null? lst) (reverse acc))
            (else (loop (cdr lst) (+ n 1) (cons (%build-doc (car lst) depth labels printed mode) acc)))))))

    (define (%build-doc-pair obj depth labels printed mode)
      (let ((maxlen (pp-length)))
        (%doc-group "(" ")" 'list
          (let loop ((node obj) (n 0) (acc '()))
            (cond
              ((and maxlen (>= n maxlen) (pair? node)) (reverse (cons (%doc-atom (pp-length-stub) 'stub #f) acc)))
              ((null? node) (reverse acc))
              ((and (pair? node) (> n 0) (not (eq? mode 'simple)) (hash-table-exists? labels node))
               (reverse (cons (%doc-dot-tail (%build-doc node depth labels printed mode)) acc)))
              ((pair? node) (loop (cdr node) (+ n 1) (cons (%build-doc (car node) depth labels printed mode) acc)))
              (else (reverse (cons (%doc-dot-tail (%build-doc node depth labels printed mode)) acc))))))))

    ;; ── Colors ───────────────────────────────────────────────────────────────
    ;; pp-color #t selects the default semantic mapper, which
    ;; (srfi 272 colorize) installs; any other true value is a mapper
    ;; procedure (semantic-color start?) -> string.

    (define %default-color-mapper (list #f))
    (define (%set-default-color-mapper! m) (set-car! %default-color-mapper m))

    (define (%color-mapper)
      (let ((c (pp-color)))
        (cond ((not c) #f)
              ((eq? c #t) (car %default-color-mapper))
              ((procedure? c) c)
              (else #f))))

    (define (%atom-semantic-color kind role)
      (case role
        ((keyword) 'keyword)
        ((defined) 'defined)
        ((formal) 'formal)
        (else
         (case kind
           ((string) 'string) ((char) 'char) ((number) 'number)
           ((symbol) 'variable) ((stub) 'meta) (else 'literal)))))

    ;; ── Widths ───────────────────────────────────────────────────────────────

    (define (%segs-width segs)
      (let loop ((s segs) (n 0))
        (if (null? s) n (loop (cdr s) (+ n (%string-width (cdar s)))))))

    (define (%flat-width doc)
      (cond
        ((%doc-atom? doc) (%segs-width (%atom-segs doc)))
        ((%doc-labelref? doc) (+ 2 (string-length (number->string (cdr doc)))))
        ((%doc-dot-tail? doc) (+ 3 (%flat-width (cdr doc))))
        ((%doc-labeled? doc)
         (+ 2 (string-length (number->string (cadr doc))) (%flat-width (cddr doc))))
        ((%doc-prefixed? doc) (+ (%segs-width (cadr doc)) (%flat-width (cddr doc))))
        ((%doc-group? doc)
         (let ((kids (%group-kids doc)))
           (+ (%string-width (%group-open doc)) (%string-width (%group-close doc))
              ;; One separator per gap between kids, except before a
              ;; dot-tail, whose own " . " already covers the gap.
              (let loop ((ks kids) (first #t) (n 0))
                (cond ((null? ks) n)
                      (first (loop (cdr ks) #f n))
                      ((%doc-dot-tail? (car ks)) (loop (cdr ks) #f n))
                      (else (loop (cdr ks) #f (+ n 1)))))
              (apply + (map %flat-width kids)))))
        (else 0)))

    ;; ── Style helpers ────────────────────────────────────────────────────────

    (define (%symbol-atom? d) (and (%doc-atom? d) (eq? (%atom-kind d) 'symbol)))

    ;; The style registered for a form headed by atom `head`: first under
    ;; the symbol as written, then under its canonical English spelling.
    (define (%style-for head)
      (let ((sym (%atom-obj head)))
        (or (lookup-pp-style (pp-styles) sym)
            (let ((canon (lang-translate sym)))
              (and (not (eq? canon sym)) (lookup-pp-style (pp-styles) canon))))))

    (define (%code->role code)
      (case code
        ((i f) 'formal)
        ((h) 'head)
        ((dc ec fc lc) 'clause)
        ((dc* ec* fc* lc*) 'clauses)
        (else #f)))

    (define (%clause-tail? t) (memq t '(dc* ec* fc* lc*)))

    ;; ── Layout ───────────────────────────────────────────────────────────────
    ;; %layout returns the list of pieces for doc starting at column
    ;; start-col. All the layout helpers close over the piece accumulator.

    (define (%layout doc start-col)
      (let ((pieces '())
            (width (pp-width))
            (inline (pp-inline-width))
            (miser (pp-miser-width))
            (tab (pp-tab))
            (max-tab (pp-max-tab))
            (code? (pp-code))
            (brackets? (and (pp-code) (pp-brackets)))
            (pretty? (pp-pretty))
            (mapper (%color-mapper)))

        (define (emit! s) (set! pieces (cons (cons 'emit s) pieces)))
        (define (tint! s) (unless (string=? s "") (set! pieces (cons (cons 'tint s) pieces))))
        (define (newline-to! col)
          (emit! "\n")
          (when (> col 0) (emit! (make-string col #\space))))

        ;; Emit text wrapped in the color sequences of semantic color sc.
        (define (colored! sc text)
          (if (and mapper sc)
              (begin (tint! (mapper sc #t)) (emit! text) (tint! (mapper sc #f)))
              (emit! text)))

        (define (open-text g role)
          (if (and brackets? (eq? role 'clause) (eq? (%group-kind g) 'list)) "[" (%group-open g)))
        (define (close-text g role)
          (if (and brackets? (eq? role 'clause) (eq? (%group-kind g) 'list)) "]" (%group-close g)))
        (define (delim-sc text) (if (or (string=? text "[") (string=? text "]")) 'bracket 'paren))

        ;; Atom text carries its own color unless the layout assigned a
        ;; role (a keyword head, a formal, a defined name).
        (define (atom! a role)
          (for-each (lambda (seg)
                      (colored! (or (car seg) (%atom-semantic-color (%atom-kind a) role)) (cdr seg)))
                    (%atom-segs a)))

        ;; ·· Flat printing ··
        (define (flat! doc role)
          (cond
            ((%doc-atom? doc) (atom! doc role))
            ((%doc-labelref? doc) (colored! 'meta (string-append "#" (number->string (cdr doc)) "#")))
            ((%doc-dot-tail? doc) (emit! " . ") (flat! (cdr doc) #f))
            ((%doc-labeled? doc)
             (colored! 'meta (string-append "#" (number->string (cadr doc)) "="))
             (flat! (cddr doc) role))
            ((%doc-prefixed? doc)
             (for-each (lambda (seg) (colored! (car seg) (cdr seg))) (cadr doc))
             (flat! (cddr doc) #f))
            ((%doc-group? doc)
             (let ((open (open-text doc role)) (close (close-text doc role)))
               (colored! (delim-sc open) open)
               (let loop ((kids (%group-kids doc)) (first #t) (i 0))
                 (when (pair? kids)
                   (if (and (not first) (not (%doc-dot-tail? (car kids)))) (emit! " "))
                   (flat! (car kids) (flat-kid-role doc role i))
                   (loop (cdr kids) #f (+ i 1))))
               (colored! (delim-sc close) close)))))

        ;; The role of kid i of a group printed with the given role: the
        ;; parts of a definition head and of a formals list are colored
        ;; as such; the first element of a list of clauses is a clause.
        (define (flat-kid-role g role i)
          (case role
            ((head) (if (= i 0) 'defined 'formal))
            ((formal) 'formal)
            ((clauses) 'clause)
            (else (if (and code? (= i 0) (eq? (%group-kind g) 'list) (%symbol-atom? (car (%group-kids g)))
                           (%style-for (car (%group-kids g))))
                      'keyword
                      #f))))

        (define (fits? doc col trail)
          (let ((fw (%flat-width doc)))
            (and (or (not width) (<= (+ col fw trail) width))
                 (or (not inline) (<= fw inline)))))

        ;; ·· Layout proper ··
        ;; Returns the column after printing doc. trail is the number of
        ;; columns of closing delimiters that must follow doc on its last line.
        (define (lay! doc col trail role)
          (cond
            ((%doc-atom? doc) (atom! doc role) (+ col (%flat-width doc)))
            ((%doc-labelref? doc) (flat! doc role) (+ col (%flat-width doc)))
            ((%doc-dot-tail? doc)
             (emit! ". ")
             (lay! (cdr doc) (+ col 2) trail #f))
            ((%doc-labeled? doc)
             (let ((prefix (string-append "#" (number->string (cadr doc)) "=")))
               (colored! 'meta prefix)
               (lay! (cddr doc) (+ col (string-length prefix)) trail role)))
            ((%doc-prefixed? doc)
             (for-each (lambda (seg) (colored! (car seg) (cdr seg))) (cadr doc))
             (lay! (cddr doc) (+ col (%segs-width (cadr doc))) trail #f))
            ((%doc-group? doc)
             (if (or (not pretty?) (null? (%group-kids doc)) (fits? doc col trail))
                 (begin (flat! doc role) (+ col (%flat-width doc)))
                 (lay-group! doc col trail role)))
            (else col)))

        (define (misering? col) (and miser width (< miser width) (> col (- width miser))))

        ;; The group does not fit: choose a layout from its head.
        (define (lay-group! g col trail role)
          (let* ((kids (%group-kids g))
                 (head (car kids))
                 (head-sym? (and code? (eq? (%group-kind g) 'list) (%symbol-atom? head)
                                 (not (ormap-dot-tail? (cdr kids)))))
                 (style (and head-sym? (%style-for head))))
            (cond
              ((eq? role 'clauses) (lay-data! g col trail role))
              (style (lay-styled! g col trail role style))
              ((and head-sym? (pair? (cdr kids))) (lay-app! g col trail role))
              (else (lay-data! g col trail role)))))

        (define (ormap-dot-tail? ds)
          (cond ((null? ds) #f) ((%doc-dot-tail? (car ds)) #t) (else (ormap-dot-tail? (cdr ds)))))

        ;; Data: every element on its own line, aligned after the delimiter.
        (define (lay-data! g col trail role)
          (let* ((open (open-text g role)) (close (close-text g role))
                 (ow (%string-width open)) (cw (%string-width close))
                 (c0 (+ col ow))
                 (kid-role (if (eq? role 'clauses) 'clause #f)))
            (colored! (delim-sc open) open)
            (let loop ((kids (%group-kids g)) (first #t) (c c0))
              (if (null? kids)
                  (begin (colored! (delim-sc close) close) (+ c cw))
                  (let* ((last? (null? (cdr kids)))
                         (c1 (if first c (begin (newline-to! c0) c0))))
                    (loop (cdr kids) #f
                          (lay! (car kids) c1 (if last? (+ trail cw) 0) kid-role)))))))

        ;; Procedure application: (head arg ...), arguments aligned under
        ;; the first one, or indented by pp-tab when the head is long or
        ;; the line is near the margin.
        (define (lay-app! g col trail role)
          (let* ((open (open-text g role)) (close (close-text g role))
                 (ow (%string-width open)) (cw (%string-width close))
                 (kids (%group-kids g)) (head (car kids))
                 (hw (%flat-width head))
                 (c-head (+ col ow))
                 (align (+ c-head hw 1))
                 (indent (+ col ow tab))
                 (aligned? (and (<= (+ hw 1) (+ max-tab 1)) (not (misering? col)))))
            (colored! (delim-sc open) open)
            (atom! head #f)
            (let loop ((args (cdr kids)) (first #t) (c (+ c-head hw)))
              (if (null? args)
                  (begin (colored! (delim-sc close) close) (+ c cw))
                  (let* ((last? (null? (cdr args)))
                         (tr (if last? (+ trail cw) 0))
                         (c1 (cond ((and first aligned?) (emit! " ") (+ c 1))
                                   (aligned? (newline-to! align) align)
                                   (else (newline-to! indent) indent))))
                    (loop (cdr args) #f (lay! (car args) c1 tr #f)))))))

        ;; A form with a registered style. Distinguished arguments stay on
        ;; the head's line; the tail lays out the rest.
        (define (lay-styled! g col trail role style)
          (let* ((open (open-text g role)) (close (close-text g role))
                 (ow (%string-width open)) (cw (%string-width close))
                 (kids (%group-kids g)) (head (car kids))
                 (hw (%flat-width head))
                 (body-col (if (misering? col) (+ col ow) (+ col ow tab)))
                 (fmt (if (pair? style) (cdr style) 'body)))
            (colored! (delim-sc open) open)
            (atom! head 'keyword)
            ;; Peel the leading codes off fmt, pairing them with arguments.
            (let peel ((fmt fmt) (args (cdr kids)) (dist '()))
              (cond
                ((and (pair? fmt) (eq? (car fmt) 'i?))
                 (if (and (pair? args) (%symbol-atom? (car args)))
                     (peel (cdr fmt) (cdr args) (cons (cons (car args) 'formal) dist))
                     (peel (cdr fmt) args dist)))
                ((and (pair? fmt) (pair? args))
                 (peel (cdr fmt) (cdr args) (cons (cons (car args) (%code->role (car fmt))) dist)))
                ((pair? fmt) (peel '() args dist))
                (else
                 (let ((tail (if (null? fmt) 'body fmt)))
                   (finish-styled! g col trail (+ col ow hw) cw close body-col
                                   (reverse dist) tail args)))))))

        (define (finish-styled! g col trail c-after-head cw close body-col dist tail rest)
          ;; Distinguished arguments. The first stays on the head line.
          ;; A later one joins it only if the previous one printed flat
          ;; and it fits; otherwise it starts a line under the first
          ;; (do's bindings and test clause line up this way).
          (let dloop ((ds dist) (c c-after-head) (i 0) (prev-flat? #t) (first-col #f))
            (if (pair? ds)
                (let* ((arg (caar ds)) (arole (cdar ds))
                       (tr (if (and (null? (cdr ds)) (null? rest)) (+ trail cw) 0))
                       (same? (or (= i 0) (and prev-flat? (fits? arg (+ c 1) tr))))
                       (acol (if same? (+ c 1) first-col)))
                  (if same? (emit! " ") (newline-to! first-col))
                  (dloop (cdr ds) (lay! arg acol tr arole) (+ i 1)
                         (fits? arg acol tr) (or first-col acol)))
                (let ((c-line c))
                  (case tail
                    ((body)
                     (let loop ((args rest) (c c-line))
                       (if (null? args)
                           (begin (colored! (delim-sc close) close) (+ c cw))
                           (begin
                             (newline-to! body-col)
                             (loop (cdr args)
                                   (lay! (car args) body-col (if (null? (cdr args)) (+ trail cw) 0) #f))))))
                    ((fill)
                     (let* ((align (+ c-after-head 1))
                            (align (if (> (- align col) (+ max-tab 2)) body-col align)))
                       (let loop ((args rest) (c c-line) (first #t))
                         (if (null? args)
                             (begin (colored! (delim-sc close) close) (+ c cw))
                             (let* ((a (car args)) (last? (null? (cdr args)))
                                    (tr (if last? (+ trail cw) 0))
                                    (fw (%flat-width a)))
                               (cond
                                 ((and first (null? dist) (fits-after? c fw tr)) (emit! " ") (loop (cdr args) (lay! a (+ c 1) tr #f) #f))
                                 ((and (not first) (fits-after? c (+ 1 fw) tr)) (emit! " ") (loop (cdr args) (lay! a (+ c 1) tr #f) #f))
                                 (else (newline-to! align) (loop (cdr args) (lay! a align tr #f) #f))))))))
                    (else
                     ;; Clauses (dc* ec* fc* lc*): aligned under the first
                     ;; clause when the form has nothing else on its first
                     ;; line, otherwise indented as a body.
                     (let* ((same-line? (null? dist))
                            (align (+ c-after-head 1))
                            (indent (if same-line? align body-col)))
                       (let loop ((args rest) (c c-line) (first #t))
                         (if (null? args)
                             (begin (colored! (delim-sc close) close) (+ c cw))
                             (let* ((a (car args)) (tr (if (null? (cdr args)) (+ trail cw) 0)))
                               (if (and first same-line?)
                                   (begin (emit! " ") (loop (cdr args) (lay! a (+ c 1) tr 'clause) #f))
                                   (begin (newline-to! indent) (loop (cdr args) (lay! a indent tr 'clause) #f)))))))))))))

        (define (fits-after? c w tr)
          (or (not width) (<= (+ c w tr) width)))

        (lay! doc start-col 0 #f)
        (reverse pieces)))

    ;; ── Pieces: line limit, text, replay ────────────────────────────────────

    (define (%newline-piece? p) (and (eq? (car p) 'emit) (string=? (cdr p) "\n")))

    ;; Truncates pieces to pp-lines lines; the stub follows the last kept
    ;; line, on a line of its own.
    (define (%apply-lines-limit pieces)
      (let ((maxlines (pp-lines)))
        (if (not maxlines)
            pieces
            (let loop ((ps pieces) (n 0) (acc '()))
              (cond
                ((null? ps) pieces)
                ((%newline-piece? (car ps))
                 (if (= (+ n 1) maxlines)
                     (append (reverse (cons (car ps) acc))
                             (list (cons 'emit (or (pp-lines-stub) (pp-length-stub)))))
                     (loop (cdr ps) (+ n 1) (cons (car ps) acc))))
                (else (loop (cdr ps) n (cons (car ps) acc))))))))

    (define (%pieces->string pieces)
      (let ((out (open-output-string)))
        (for-each (lambda (p) (write-string (cdr p) out)) pieces)
        (get-output-string out)))

    (define (%split-lines s)
      (let ((len (string-length s)))
        (let loop ((i 0) (start 0) (acc '()))
          (cond
            ((>= i len) (reverse (if (= start i) acc (cons (substring s start i) acc))))
            ((char=? (string-ref s i) #\newline) (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
            (else (loop (+ i 1) start acc))))))

    ;; ── Entry points ─────────────────────────────────────────────────────────

    (define (%render-pieces obj mode)
      (let* ((labels (%assign-labels obj mode))
             (printed (make-hash-table eq?))
             (doc (%build-doc obj 0 labels printed mode)))
        (%apply-lines-limit (%layout doc (pp-indent)))))

    (define (%render-to-string obj mode) (%pieces->string (%render-pieces obj mode)))

    ;; Lines of the output, each with its terminating newline.
    (define (%pp-lines-list obj mode)
      (map (lambda (l) (string-append l "\n")) (%split-lines (%render-to-string obj mode))))

    ;; pp's own mode selection: pp-graph wins over pp-circle.
    (define (%current-mode)
      (cond ((pp-graph) 'graph) ((pp-circle) 'circle) (else 'simple)))

    (define (%pp-to-port obj port mode)
      (let ((emit (pp-emit)) (tint (pp-tint)))
        (for-each (lambda (p) (if (eq? (car p) 'tint) (tint (cdr p) port) (emit (cdr p) port)))
                  (%render-pieces obj mode))
        (if (and (pp-newline) (pp-pretty)) (emit "\n" port))))

    ;; ── Keyword-argument dispatch (intermediate tier onward) ────────────────
    ;; Scans (key value) ... pairs left to right against a table of
    ;; (keyword . parameter); the first match for a keyword wins.
    ;; Unrecognized keywords are ignored. A parameter object may stand in
    ;; for its keyword symbol.
    (define %kwarg-table
      (list (cons 'pp-width pp-width) (cons 'pp-graph pp-graph) (cons 'pp-circle pp-circle)
            (cons 'pp-radix pp-radix) (cons 'pp-level pp-level) (cons 'pp-length pp-length)
            (cons 'pp-lines pp-lines) (cons 'pp-level-stub pp-level-stub) (cons 'pp-length-stub pp-length-stub)
            (cons 'pp-lines-stub pp-lines-stub) (cons 'pp-newline pp-newline)
            (cons 'pp-inline-width pp-inline-width) (cons 'pp-miser-width pp-miser-width)
            (cons 'pp-indent pp-indent) (cons 'pp-tab pp-tab) (cons 'pp-max-tab pp-max-tab)
            (cons 'pp-code pp-code) (cons 'pp-brackets pp-brackets) (cons 'pp-pretty pp-pretty)
            (cons 'pp-color pp-color) (cons 'pp-decorate pp-decorate)
            (cons 'pp-emit pp-emit) (cons 'pp-tint pp-tint) (cons 'pp-styles pp-styles) (cons 'pp-hooks pp-hooks)))

    (define (%kwarg->parameter key)
      (if (procedure? key) key (let ((hit (assq key %kwarg-table))) (and hit (cdr hit)))))

    ;; Runs thunk with each (key value) pair applied; for a repeated key
    ;; the leftmost wins.
    (define (%pp-with-kwargs kvs thunk)
      (let loop ((kvs kvs) (params '()) (vals '()))
        (if (or (null? kvs) (null? (cdr kvs)))
            (%parameterize-list params vals thunk)
            (let ((p (%kwarg->parameter (car kvs))))
              (if (and p (not (memq p params)))
                  (loop (cddr kvs) (cons p params) (cons (cadr kvs) vals))
                  (loop (cddr kvs) params vals))))))

    ;; parameterize needs a syntactic binding list; this does the same
    ;; for a runtime-sized list of (parameter . value) pairs.
    (define (%parameterize-list params vals thunk)
      (if (null? params)
          (thunk)
          (let ((p (car params)) (v (car vals)) (old #f))
            (dynamic-wind
              (lambda () (set! old (p)) (p v))
              (lambda () (%parameterize-list (cdr params) (cdr vals) thunk))
              (lambda () (p old))))))))
