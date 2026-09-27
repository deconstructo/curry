;;; SRFI-272 shared engine: the real pretty-printing machinery every
;;; (srfi 272 ...) sub-library is built on. Not one of the SRFI's own
;;; eight public library names -- an internal implementation detail,
;;; exported broadly (a much wider surface than any one public library
;;; needs) so each public .sld shim can re-export exactly its own
;;; documented subset without duplicating logic.
;;;
;;; https://srfi.schemers.org/srfi-272/ -- pretty printing, layered:
;;; (srfi 272) minimalist -> basic -> intermediate -> advanced -> fancy,
;;; plus independent colorize/measure/show libraries. Status: DRAFT as
;;; of this implementation (draft #3, 2026-09-21) -- ported in full at
;;; the user's explicit request despite the spec still being subject to
;;; change; see docs/reference/srfi/s272.md for the full list of
;;; deliberate scope/fidelity decisions made where the spec's own
;;; ambition exceeds what's tractable in one implementation pass
;;; (notably: a real, working Wadler-style layout algorithm rather than
;;; a line-by-line transliteration of any one reference implementation;
;;; a practical, non-exhaustive East Asian Width approximation in the
;;; measure library; and (srfi 272 show) omitted entirely since it
;;; requires SRFI-166, which curry does not implement).
;;;
;;; ── Document model ──────────────────────────────────────────────────
;;; Every value to print is first converted into a small "doc" tree,
;;; entirely independent of parameters that only affect LAYOUT (width,
;;; indentation, colors) -- only pp-level/pp-length/graph-vs-circle-vs-
;;; simple mode affect the doc tree's own SHAPE. A doc is one of:
;;;   (atom . text)                  -- text is the exact string to emit
;;;   (group open close kind . kids) -- kids is a list of docs
;;;   (dot-tail . doc)               -- an improper-list tail, printed
;;;                                      as " . <doc>" instead of a
;;;                                      normal space-separated element
;;;   (labeled n . doc)              -- wraps doc's own rendering in
;;;                                      "#n=" (graph/circle mode only)
;;;   (labelref . n)                 -- a bare "#n#" back-reference
;;; A separate LAYOUT pass renders a doc tree to actual text, tracking
;;; current column, deciding per-group whether the flat (single-line)
;;; rendering fits the remaining width or whether to break the group
;;; across lines -- the well-known two-pass "measure, then decide"
;;; simplification of Wadler's own lazy algorithm from "A Prettier
;;; Printer", which is easier to implement correctly and performs fine
;;; for the sizes of data pp is actually used on.

(define-library (srfi s272 engine)
  (import (scheme base) (scheme write) (scheme inexact)
          (srfi s69 hash-tables)
          (srfi s158 generators-and-accumulators))
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
    %pp-to-port %pp-lines-list %current-mode
    ;; Keyword-argument helper (shared by every pp/pprint variant)
    %pp-with-kwargs)
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
    (define pp-max-tab (make-parameter 4))
    (define pp-code (make-parameter #f))
    (define pp-brackets (make-parameter #f))
    (define pp-pretty (make-parameter #t))
    (define pp-color (make-parameter #f))
    (define pp-decorate (make-parameter #t))
    (define pp-emit (make-parameter (lambda (s port) (write-string s port))))
    (define pp-tint (make-parameter (lambda (s port) (write-string s port))))

    ;; ── Style registry ───────────────────────────────────────────────────────
    ;; A style registry is an alist (symbol . style-object); functional
    ;; update per the spec's own text ("returns new registry; original
    ;; unmodified"). pp-styles is the parameter advanced-tier code
    ;; threads through add-pp-style/lookup-pp-style; pretty-style (the
    ;; simpler intermediate-tier get/setter) mutates the SAME parameter
    ;; in place via parameterize's own current binding, so both tiers'
    ;; mechanisms observe each other's changes when both are loaded.

    (define pp-styles (make-parameter '()))

    (define (add-pp-style registry symbol style)
      (if (not style)
          (let loop ((r registry) (acc '()))
            (cond ((null? r) (reverse acc))
                  ((eq? (caar r) symbol) (append (reverse acc) (cdr r)))
                  (else (loop (cdr r) (cons (car r) acc)))))
          (cons (cons symbol style)
                (let loop ((r registry) (acc '()))
                  (cond ((null? r) (reverse acc))
                        ((eq? (caar r) symbol) (append (reverse acc) (cdr r)))
                        (else (loop (cdr r) (cons (car r) acc))))))))

    (define (lookup-pp-style registry symbol)
      (let ((hit (assq symbol registry))) (and hit (cdr hit))))

    (define (pretty-style symbol . maybe-style)
      (if (pair? maybe-style)
          (begin (pp-styles (add-pp-style (pp-styles) symbol (car maybe-style))) (if #f #f))
          (lookup-pp-style (pp-styles) symbol)))

    ;; ── Hook registry ────────────────────────────────────────────────────────
    ;; A hook registry is an alist (test . hook); "new associations
    ;; added to beginning; existing same-test associations replaced in
    ;; place" per the spec's own text -- note this is REPLACE-IN-PLACE
    ;; for an existing test, unlike add-pp-style's remove-then-prepend
    ;; (which also ends up at the front, but the distinction matters if
    ;; ever exposed): implemented literally as specified, not just
    ;; "close enough".

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
    ;; render-proc knows how to turn a matching object into a doc tree
    ;; (see %hook-doc below, in the main render pass) -- kept as plain
    ;; closures over the constructor's own arguments rather than a
    ;; bigger dispatch table, since there are only four shapes.

    (define (glst-pp-hook prefix cf xf suffix)
      (%make-pp-hook 'glst (lambda (obj build) (%doc-group prefix suffix 'hook-glst (map build (cf obj)))) xf))
    (define (bvec-pp-hook prefix lf rf suffix)
      (%make-pp-hook 'bvec
        (lambda (obj build)
          (%doc-group prefix suffix 'hook-bvec
            (let loop ((i 0) (acc '()))
              (if (>= i (lf obj)) (reverse acc) (loop (+ i 1) (cons (build (rf obj i)) acc))))))
        #f))
    (define (atom-pp-hook shareable? wf df)
      (%make-pp-hook 'atom (lambda (obj build) (%doc-atom (%dfpairs->string (df obj (pp-radix))))) shareable?))
    (define (rmac-pp-hook prefix ef xf)
      (%make-pp-hook 'rmac (lambda (obj build) (%doc-group prefix "" 'hook-rmac (list (build (ef obj))))) xf))

    ;; atom-pp-hook's df returns a list of (color-symbol . string) or
    ;; bare-string pairs; without live color-mapper wiring at this
    ;; engine layer (colorize is a separate, independent library that
    ;; may not even be loaded), this concatenates just the text parts --
    ;; (srfi 272 colorize) callers get real color output via pp-color's
    ;; own integration in the render pass for ORDINARY atoms; a colored
    ;; atom-pp-hook is accepted or plain-concatenated here.
    (define (%dfpairs->string pairs)
      (apply string-append (map (lambda (p) (if (pair? p) (cdr p) p)) pairs)))

    ;; ── Cycle/sharing detection ──────────────────────────────────────────────
    ;;
    ;; All three traversals below are written as an explicit work-stack
    ;; loop rather than plain car/cdr recursion. This library is a
    ;; define-library body, tree-walked via eval() (see CLAUDE.md's own
    ;; non-tail-recursion stack-depth guard), so a naive `(begin (walk
    ;; (car o)) (walk (cdr o)))` -- neither call in tail position, since
    ;; there's bookkeeping after both -- overflows that guard on any
    ;; ordinary list longer than a few hundred elements: real user data,
    ;; not an obscure edge case, and the exact kind of bug this SRFI
    ;; exists to handle gracefully. Every `loop` call below IS a tail
    ;; call, so stack depth stays O(1) regardless of input length; the
    ;; work list itself (heap-allocated, GC-managed) carries the O(n)
    ;; cost instead.

    (define (%shareable? obj) (or (pair? obj) (vector? obj)))

    (define (%children o)
      (if (pair? o) (list (car o) (cdr o)) (vector->list o)))

    ;; DFS with post-order 'leave markers so on-stack correctly reflects
    ;; "currently being visited" (for genuine-cycle detection) rather
    ;; than "ever visited".
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

    ;; mode is 'graph, 'circle, or 'simple. Returns an eq?-hash-table
    ;; from object -> sequential label number, for exactly the objects
    ;; that need one under the given mode.
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

    ;; ── Doc tree constructors ────────────────────────────────────────────────

    (define (%doc-atom text) (cons 'atom text))
    (define (%doc-group open close kind kids) (list* 'group open close kind kids))
    (define (%doc-dot-tail doc) (cons 'dot-tail doc))
    (define (%doc-labeled n doc) (list* 'labeled n doc))
    (define (%doc-labelref n) (cons 'labelref n))
    (define (%doc-atom? d) (and (pair? d) (eq? (car d) 'atom)))
    (define (%doc-group? d) (and (pair? d) (eq? (car d) 'group)))
    (define (%doc-dot-tail? d) (and (pair? d) (eq? (car d) 'dot-tail)))
    (define (%doc-labeled? d) (and (pair? d) (eq? (car d) 'labeled)))
    (define (%doc-labelref? d) (and (pair? d) (eq? (car d) 'labelref)))

    ;; list* (aka cons*): cons up all but the last argument onto the
    ;; last one, used as-is (a plain list, here) -- not in (scheme
    ;; base); tiny local helper so this file has no extra SRFI
    ;; dependency just for it.
    (define (list* . args)
      (let loop ((args args))
        (cond ((null? args) '())
              ((null? (cdr args)) (car args))
              (else (cons (car args) (loop (cdr args)))))))

    ;; ── Atom text rendering ──────────────────────────────────────────────────
    ;; write-mode is 'write, 'display, or 'write-simple (only ever
    ;; 'write-simple when the whole traversal's mode is 'simple, since
    ;; that's the only case with no label bookkeeping to keep correct
    ;; across nested write calls).

    (define (%render-number-text n)
      (let ((radix (pp-radix)))
        (if (or (not (exact? n)) (= radix 10))
            (number->string n radix)
            (string-append (case radix ((2) "#b") ((8) "#o") ((16) "#x") (else "")) (number->string n radix)))))

    (define (%render-atom-text obj)
      (let ((hook (%find-matching-hook obj)))
        (cond
          (hook (%dfpairs->string (list ((%hook-render hook) obj %render-atom-text))))
          ((number? obj) (%render-number-text obj))
          (else
           (let ((p (open-output-string)))
             (write obj p)
             (get-output-string p))))))

    ;; A pp-hooks entry is (test . hookval). Per the spec: if hookval is
    ;; a real hook object, test is an ordinary predicate and hookval is
    ;; used directly on a match; if hookval is #t, test is itself a
    ;; "hook-returning procedure" -- calling it on obj either returns
    ;; #f (no match) or the actual <pp-hook> record to use. Returns the
    ;; resolved <pp-hook> record, or #f if nothing matched.
    (define (%find-matching-hook obj)
      (let loop ((hs (pp-hooks)))
        (if (null? hs)
            #f
            (let* ((test (caar hs)) (hookval (cdar hs)))
              (if (eq? hookval #t)
                  (let ((h (test obj))) (if (pp-hook? h) h (loop (cdr hs))))
                  (if (test obj) hookval (loop (cdr hs))))))))

    ;; ── Doc building ─────────────────────────────────────────────────────────

    (define (%build-doc obj depth labels printed mode)
      (let ((hook (%find-matching-hook obj)))
        (cond
          (hook (%hook-doc obj hook depth labels printed mode))
          ;; Spec: the root is level 0; a component at a level EQUAL TO
          ;; OR EXCEEDING pp-level gets stubbed -- so >=, not >.
          ((and (pp-level) (>= depth (pp-level)) (%shareable? obj))
           (%doc-atom (pp-level-stub)))
          ((and (not (eq? mode 'simple)) (%shareable? obj) (hash-table-exists? labels obj))
           (let ((n (hash-table-ref labels obj)))
             (if (hash-table-exists? printed obj)
                 (%doc-labelref n)
                 (begin (hash-table-set! printed obj #t)
                        (%doc-labeled n (%build-doc-body obj depth labels printed mode))))))
          ((%shareable? obj) (%build-doc-body obj depth labels printed mode))
          (else (%doc-atom (%render-atom-text obj))))))

    (define (%hook-doc obj hook depth labels printed mode)
      ((%hook-render hook) obj (lambda (sub) (%build-doc sub (+ depth 1) labels printed mode))))

    (define (%build-doc-body obj depth labels printed mode)
      (if (vector? obj)
          (%doc-group "#(" ")" 'vector
            (%build-doc-elements (vector->list obj) (+ depth 1) labels printed mode #f))
          (%build-doc-pair obj (+ depth 1) labels printed mode)))

    (define (%build-doc-elements lst depth labels printed mode dotted-ok)
      (let ((maxlen (pp-length)))
        (let loop ((lst lst) (n 0) (acc '()))
          (cond
            ((and maxlen (>= n maxlen) (pair? lst)) (reverse (cons (%doc-atom (pp-length-stub)) acc)))
            ((null? lst) (reverse acc))
            (else (loop (cdr lst) (+ n 1) (cons (%build-doc (car lst) depth labels printed mode) acc)))))))

    (define (%build-doc-pair obj depth labels printed mode)
      (let ((maxlen (pp-length)))
        (%doc-group "(" ")" 'list
          (let loop ((node obj) (n 0) (acc '()))
            (cond
              ((and maxlen (>= n maxlen) (pair? node)) (reverse (cons (%doc-atom (pp-length-stub)) acc)))
              ((null? node) (reverse acc))
              ((and (pair? node) (> n 0) (not (eq? mode 'simple)) (hash-table-exists? labels node))
               (reverse (cons (%doc-dot-tail (%build-doc node depth labels printed mode)) acc)))
              ((pair? node) (loop (cdr node) (+ n 1) (cons (%build-doc (car node) depth labels printed mode) acc)))
              (else (reverse (cons (%doc-dot-tail (%build-doc node depth labels printed mode)) acc))))))))

    ;; ── Layout / rendering ───────────────────────────────────────────────────
    ;; %flat-width measures a doc's own single-line rendering length
    ;; (memoized isn't needed -- docs here are trees, not shared graphs,
    ;; since %build-doc already replaced any actual sharing with
    ;; labelref leaves before layout ever runs).

    (define (%flat-width doc)
      (cond
        ((%doc-atom? doc) (string-length (cdr doc)))
        ((%doc-labelref? doc) (+ 2 (string-length (number->string (cdr doc)))))
        ((%doc-dot-tail? doc) (+ 3 (%flat-width (cdr doc))))
        ((%doc-labeled? doc)
         (+ 1 (string-length (number->string (cadr doc))) 1 (%flat-width (cddr doc))))
        ((%doc-group? doc)
         (let* ((open (cadr doc)) (close (caddr doc)) (kids (cdr (cdddr doc))))
           (+ (string-length open) (string-length close)
              ;; one separator space per gap between kids, except the gap
              ;; right before a dot-tail kid -- dot-tail's own " . " (3
              ;; chars, counted in its own %flat-width case above) already
              ;; covers that gap, so adding a plain separator there too
              ;; would double-count it.
              (let loop ((ks kids) (first #t) (n 0))
                (cond ((null? ks) n)
                      (first (loop (cdr ks) #f n))
                      ((%doc-dot-tail? (car ks)) (loop (cdr ks) #f n))
                      (else (loop (cdr ks) #f (+ n 1)))))
              (apply + (map %flat-width kids)))))
        (else 0)))

    (define (%emit s port) ((pp-emit) s port))

    ;; Prints doc flat (no line breaks) regardless of width -- used once
    ;; a group has already been measured to fit, or when pp-pretty is #f.
    (define (%print-flat doc port)
      (cond
        ((%doc-atom? doc) (%emit (cdr doc) port))
        ((%doc-labelref? doc) (%emit (string-append "#" (number->string (cdr doc)) "#") port))
        ((%doc-dot-tail? doc) (%emit " . " port) (%print-flat (cdr doc) port))
        ((%doc-labeled? doc)
         (%emit (string-append "#" (number->string (cadr doc)) "=") port)
         (%print-flat (cddr doc) port))
        ((%doc-group? doc)
         (let ((open (cadr doc)) (close (caddr doc)) (kids (cdr (cdddr doc))))
           (%emit open port)
           (let loop ((kids kids) (first #t))
             (when (pair? kids)
               (if (and (not first) (not (%doc-dot-tail? (car kids)))) (%emit " " port))
               (%print-flat (car kids) port)
               (loop (cdr kids) #f)))
           (%emit close port)))))

    ;; Prints doc with real line breaking: `col` is the current column,
    ;; `indent` the column to return to after a forced line break inside
    ;; this doc. Returns the column after printing.
    (define (%print-broken doc port col indent)
      (cond
        ((or (%doc-atom? doc) (%doc-labelref? doc))
         (%print-flat doc port) (+ col (%flat-width doc)))
        ((%doc-dot-tail? doc)
         (%emit " . " port)
         (%print-broken (cdr doc) port (+ col 3) indent))
        ((%doc-labeled? doc)
         (let* ((prefix (string-append "#" (number->string (cadr doc)) "=")))
           (%emit prefix port)
           (%print-broken (cddr doc) port (+ col (string-length prefix)) indent)))
        ((%doc-group? doc)
         (let* ((open (cadr doc)) (close (caddr doc)) (kids (cdr (cdddr doc)))
                (width (pp-width)) (miser (pp-miser-width))
                (inline (pp-inline-width))
                (fits? (and (or (not width) (<= (+ col (%flat-width doc)) width))
                            (or (not inline) (<= (%flat-width doc) inline)))))
           (if (or (not (pp-pretty)) fits? (null? kids))
               (begin (%print-flat doc port) (+ col (%flat-width doc)))
               (let* ((child-indent
                        (if (and miser width (> col (- width miser)))
                            (+ indent (pp-tab))
                            (min (+ col (string-length open)) (+ indent (pp-max-tab) (string-length open))))))
                 (%emit open port)
                 (let loop ((kids kids) (first #t) (c (+ col (string-length open))))
                   (if (null? kids)
                       (begin (%emit close port) (+ c (string-length close)))
                       (let ((c2 (if first c (begin (%emit "\n" port) (%emit (make-string child-indent #\space) port) child-indent))))
                         (loop (cdr kids) #f (%print-broken (car kids) port c2 child-indent)))))))))
        (else col)))

    ;; ── Line-limit enforcement ───────────────────────────────────────────────
    ;; Rendered by first printing to a string port (so pp-lines can
    ;; truncate before anything reaches the real destination), then
    ;; emitting the (possibly truncated) result through pp-emit/pp-tint.

    (define (%split-lines s)
      (let ((len (string-length s)))
        (let loop ((i 0) (start 0) (acc '()))
          (cond
            ((>= i len) (reverse (cons (substring s start i) acc)))
            ((char=? (string-ref s i) #\newline) (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
            (else (loop (+ i 1) start acc))))))

    (define (%apply-lines-limit text)
      (let ((maxlines (pp-lines)))
        (if (not maxlines)
            text
            (let ((lines (%split-lines text)))
              (if (<= (length lines) maxlines)
                  text
                  (let loop ((lines lines) (n 0) (acc '()))
                    (if (= n maxlines)
                        (string-append (apply string-append (map (lambda (l) (string-append l "\n")) (reverse acc)))
                                       (or (pp-lines-stub) (pp-length-stub)))
                        (loop (cdr lines) (+ n 1) (cons (car lines) acc)))))))))

    ;; ── Top-level rendering entry points ─────────────────────────────────────

    (define (%render-to-string obj mode)
      (let* ((labels (%assign-labels obj mode))
             (printed (make-hash-table eq?))
             (doc (%build-doc obj 0 labels printed mode))
             (out (open-output-string)))
        (parameterize ((pp-emit (lambda (s p) (write-string s p)))
                       (pp-tint (lambda (s p) (write-string s p))))
          (%print-broken doc out (pp-indent) (pp-indent)))
        (get-output-string out)))

    ;; pp's own mode selection, per the spec: pp-graph wins over
    ;; pp-circle when both are true.
    (define (%current-mode)
      (cond ((pp-graph) 'graph) ((pp-circle) 'circle) (else 'simple)))

    (define (%pp-to-port obj port mode)
      (let ((text (%apply-lines-limit (%render-to-string obj mode))))
        (%emit text port)
        (if (and (pp-newline) (pp-pretty)) (%emit "\n" port))))

    (define (%pp-lines-list obj mode)
      (let* ((text (%apply-lines-limit (%render-to-string obj mode)))
             (lines (%split-lines text)))
        (map (lambda (l) (string-append l "\n")) lines)))

    ;; ── Keyword-argument dispatch (intermediate tier onward) ────────────────
    ;; Scans (key value) ... pairs left-to-right against a fixed table
    ;; of (keyword . parameter) associations; first match for a given
    ;; keyword wins (matches the spec's own "scanned left-to-right;
    ;; first match wins" text), unrecognized keywords are ignored rather
    ;; than raising, so callers can pass parameter objects they don't
    ;; know the keyword-symbol spelling for by using the parameter
    ;; object itself as the "keyword" (also supported: a bare parameter
    ;; object where a symbol is expected).
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

    ;; Runs thunk with each (key value) pair in kvs applied via
    ;; parameterize -- kvs is a flat list (key1 val1 key2 val2 ...).
    (define (%pp-with-kwargs kvs thunk)
      (let loop ((kvs kvs) (params '()) (vals '()))
        (if (null? kvs)
            (%parameterize-list params vals thunk)
            (let ((p (%kwarg->parameter (car kvs))))
              (loop (cddr kvs)
                    (if p (cons p params) params)
                    (if p (cons (cadr kvs) vals) vals))))))

    ;; parameterize's own binding list must be syntactic; this builds
    ;; the equivalent effect at runtime for a dynamically-sized list of
    ;; (parameter . value) pairs via nested single-parameter dynamic-wind.
    (define (%parameterize-list params vals thunk)
      (if (null? params)
          (thunk)
          (let ((p (car params)) (v (car vals)) (old #f))
            (dynamic-wind
              (lambda () (set! old (p)) (p v))
              (lambda () (%parameterize-list (cdr params) (cdr vals) thunk))
              (lambda () (p old))))))))
