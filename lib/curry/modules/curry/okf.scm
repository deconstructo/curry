;;; (curry okf) — Open Knowledge Format (OKF v0.2) bundle support.
;;;
;;; OKF represents knowledge as a directory of markdown files with YAML
;;; frontmatter ("concepts"); a directory tree of them is a "bundle". This
;;; module loads, queries, links, and writes such bundles. See
;;; docs/thoughts/okf-module.md for the design rationale.
;;;
;;; Frontmatter parsing/serialization is delegated entirely to (curry
;;; yaml) — OKF frontmatter is a strict subset of YAML and (curry yaml)
;;; already covers it in full, so no separate parser lives here. The only
;;; genuinely OKF-specific string handling is splitting a file's leading
;;; `---`-delimited frontmatter block from its markdown body.
;;;
;;; API:
;;;   (okf-load-bundle root)                 -> <okf-bundle>
;;;   (okf-load-concept root path)           -> <okf-concept>
;;;   (okf-read-concept port id path)        -> <okf-concept>
;;;   (okf-concept-type/title/description/resource/tags/sources/
;;;     generated/verified/status/stale-after c)
;;;   (okf-concept-field c key)
;;;   (okf-trust-tier c)                     -> 'unverified | 'machine-confirmed | 'human-reviewed
;;;   (okf-stale? c)
;;;   (okf-bundle-concepts bundle) / (okf-bundle-ref bundle id)
;;;   (okf-concepts-by-type/tag/trust-tier bundle x) / (okf-concepts-stale bundle)
;;;   (okf-attested-computation? c)
;;;   (okf-concept-links c) / (okf-bundle->graph bundle) / (okf-graph-backlinks graph)
;;;   (okf-resolve-link bundle c target) / (okf-bundle-broken-links bundle)
;;;   (okf-computation-runtime/parameters/inline-sql c)
;;;   (okf-validate-concept c) / (okf-validate-bundle bundle)
;;;   (make-okf-concept #:type ... #:body ...)
;;;   (okf-write-concept-port c port) / (okf-write-concept c root id)
;;;   (okf-generate-index bundle prefix) / (okf-log-append bundle dir kind text)

(define-library (curry okf)
  (import (scheme base))
  (import (curry yaml))
  (import (curry posix))
  (import (curry regex))
  (import (srfi 19))
  (export
    okf-concept? okf-concept-id okf-concept-path okf-concept-frontmatter okf-concept-body
    okf-bundle? okf-bundle-root okf-bundle-table
    okf-split-frontmatter
    okf-read-concept okf-load-concept okf-load-bundle
    okf-concept-type okf-concept-title okf-concept-description okf-concept-resource
    okf-concept-tags okf-concept-sources okf-concept-generated okf-concept-verified
    okf-concept-status okf-concept-stale-after okf-concept-field
    okf-trust-tier okf-stale?
    okf-bundle-concepts okf-bundle-ref
    okf-concepts-by-type okf-concepts-by-tag okf-concepts-by-trust-tier okf-concepts-stale
    okf-attested-computation?
    okf-concept-links okf-bundle->graph okf-graph-backlinks okf-resolve-link
    okf-bundle-broken-links
    okf-computation-runtime okf-computation-parameters okf-computation-inline-sql
    okf-validate-concept okf-validate-bundle
    make-okf-concept okf-write-concept-port okf-write-concept
    okf-generate-index okf-log-append)
  (begin

;;; =========================================================================
;;; Small local helpers (kept independent of (srfi s1 lists), matching the
;;; convention (curry yaml)/(curry toml) already use)
;;; =========================================================================

(define (%string-prefix? pre s)
  (let ((lp (string-length pre)) (ls (string-length s)))
    (and (>= ls lp) (string=? (substring s 0 lp) pre))))

(define (%string-suffix? suf s)
  (let ((ls (string-length s)) (lf (string-length suf)))
    (and (>= ls lf) (string=? (substring s (- ls lf) ls) suf))))

(define (%string-trim-right s)
  (let loop ((i (string-length s)))
    (if (and (> i 0) (char-whitespace? (string-ref s (- i 1))))
        (loop (- i 1))
        (substring s 0 i))))

(define (%string-last-index s ch)
  (let loop ((i (- (string-length s) 1)))
    (cond ((< i 0) #f)
          ((char=? (string-ref s i) ch) i)
          (else (loop (- i 1))))))

(define (%split-on s sep)
  (let ((n (string-length s)))
    (let loop ((i 0) (start 0) (acc '()))
      (cond ((>= i n) (reverse (cons (substring s start i) acc)))
            ((char=? (string-ref s i) sep)
             (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
            (else (loop (+ i 1) start acc))))))

(define (%split-lines s) (%split-on s #\newline))

(define (%join-with lines sep-char)
  (let ((out (open-output-string)))
    (let loop ((ls lines) (first #t))
      (unless (null? ls)
        (unless first (write-char sep-char out))
        (display (car ls) out)
        (loop (cdr ls) #f)))
    (get-output-string out)))

(define (%join-lines lines) (%join-with lines #\newline))

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

(define (%read-whole-file path)
  (call-with-input-file path %port->string))

(define (%any pred lst)
  (and (pair? lst) (or (pred (car lst)) (%any pred (cdr lst)))))

(define (%contains-char? s ch)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (cond ((>= i n) #f)
            ((char=? (string-ref s i) ch) #t)
            (else (loop (+ i 1)))))))

;; Guards every caller-supplied bundle-relative path fragment (concept id,
;; index prefix, log directory) before it's concatenated into a real
;; filesystem path. Without this, a ".."-laden id (plausible input for a
;; module meant to be driven by an LLM agent's tool call) lets
;; okf-write-concept/okf-generate-index/okf-log-append write outside the
;; bundle root; an embedded NUL byte would otherwise survive Scheme-level
;; validation only to truncate the path at the underlying C call.
(define (%safe-bundle-relative? s)
  (and (not (%contains-char? s (integer->char 0)))
       (not (and (> (string-length s) 0) (char=? (string-ref s 0) #\/)))
       (not (%any (lambda (seg) (string=? seg "..")) (%split-on s #\/)))))

(define (%strip-trailing-slash s)
  (if (and (%string-suffix? "/" s) (> (string-length s) 0))
      (%strip-trailing-slash (substring s 0 (- (string-length s) 1)))
      s))

(define (%filter-map f lst)
  (let loop ((l lst) (acc '()))
    (if (null? l)
        (reverse acc)
        (let ((v (f (car l))))
          (loop (cdr l) (if v (cons v acc) acc))))))

(define (%every pred lst)
  (or (null? lst) (and (pred (car lst)) (%every pred (cdr lst)))))

(define (%today) (date->string (current-date) "~Y-~m-~d"))

;;; =========================================================================
;;; Records
;;; =========================================================================

(define-record-type <okf-concept>
  (make-okf-concept% id path frontmatter body)
  okf-concept?
  (id          okf-concept-id)
  (path        okf-concept-path)
  (frontmatter okf-concept-frontmatter)
  (body        okf-concept-body))

(define-record-type <okf-bundle>
  (make-okf-bundle% root table)
  okf-bundle?
  (root  okf-bundle-root)
  (table okf-bundle-table))

;;; =========================================================================
;;; Frontmatter splitting (the one genuinely OKF-specific string operation —
;;; actual YAML parsing/serialization is (curry yaml)'s job, not this
;;; module's)
;;; =========================================================================

;; string -> (yaml-str . body-str). If the content doesn't open with a
;; `---` line, treats it as frontmatter-less: empty frontmatter, whole
;; content as body.
(define (okf-split-frontmatter content)
  (let ((lines (%split-lines content)))
    (if (or (null? lines) (not (string=? (%string-trim-right (car lines)) "---")))
        (cons "" content)
        (let loop ((rest (cdr lines)) (fm '()))
          (cond
            ((null? rest) (error "okf: unterminated frontmatter block"))
            ((string=? (%string-trim-right (car rest)) "---")
             (cons (%join-lines (reverse fm)) (%join-lines (cdr rest))))
            (else (loop (cdr rest) (cons (car rest) fm))))))))

(define (%parse-frontmatter fm-str)
  (let ((v (yaml-parse fm-str)))
    (if (yaml-null? v) '() v)))

;;; =========================================================================
;;; Loading
;;; =========================================================================

;; Port-native primitive: id/path are supplied by the caller since a port
;; has neither.
(define (okf-read-concept port id path)
  (let* ((content (%port->string port))
         (split (okf-split-frontmatter content))
         (fm (%parse-frontmatter (car split))))
    (make-okf-concept% id path fm (cdr split))))

(define (%path->id root path)
  (let* ((rootlen (string-length root))
         (rel (if (and (>= (string-length path) rootlen)
                       (string=? (substring path 0 rootlen) root))
                  (substring path rootlen (string-length path))
                  path))
         (rel (if (and (> (string-length rel) 0) (char=? (string-ref rel 0) #\/))
                  (substring rel 1 (string-length rel))
                  rel)))
    (if (%string-suffix? ".md" rel)
        (substring rel 0 (- (string-length rel) 3))
        rel)))

(define (okf-load-concept root path)
  (call-with-input-file path
    (lambda (port) (okf-read-concept port (%path->id root path) path))))

;; lstat (follow?=#f), not stat: a symlinked directory is walked into only
;; if the walker followed it, and a symlink cycle would then recurse
;; forever, so symlinks are skipped outright rather than followed. Any
;; entry that can't even be stat'd (permission error, race with a
;; concurrent delete) is skipped rather than aborting the whole bundle
;; load.
(define (%walk-dir root dir table)
  (for-each
    (lambda (name)
      (let* ((full (string-append dir "/" name))
             (fi (guard (e (#t #f)) (file-info full #f))))
        (when (and fi (not (file-info-symlink? fi)))
          (cond
            ((file-info-directory? fi) (%walk-dir root full table))
            ((and (%string-suffix? ".md" name)
                  (not (string=? name "index.md"))
                  (not (string=? name "log.md")))
             (let ((c (guard (e (#t #f)) (okf-load-concept root full))))
               (when c (hash-table-set! table (okf-concept-id c) c))))
            (else #f)))))
    (directory-files dir)))

(define (okf-load-bundle root)
  (let* ((root2 (if (%string-suffix? "/" root)
                     (substring root 0 (- (string-length root) 1))
                     root))
         (table (make-hash-table)))
    (%walk-dir root2 root2 table)
    (make-okf-bundle% root2 table)))

;;; =========================================================================
;;; Frontmatter accessors
;;; =========================================================================

(define (%fm-ref fm key default)
  (let ((p (assoc key fm)))
    (if p (cdr p) default)))

(define (okf-concept-type c)        (%fm-ref (okf-concept-frontmatter c) "type" #f))
(define (okf-concept-title c)       (%fm-ref (okf-concept-frontmatter c) "title" #f))
(define (okf-concept-description c) (%fm-ref (okf-concept-frontmatter c) "description" #f))
(define (okf-concept-resource c)    (%fm-ref (okf-concept-frontmatter c) "resource" #f))
(define (okf-concept-tags c)        (%fm-ref (okf-concept-frontmatter c) "tags" '()))
(define (okf-concept-sources c)     (%fm-ref (okf-concept-frontmatter c) "sources" '()))
(define (okf-concept-generated c)   (%fm-ref (okf-concept-frontmatter c) "generated" #f))
(define (okf-concept-status c)      (%fm-ref (okf-concept-frontmatter c) "status" "stable"))
(define (okf-concept-stale-after c) (%fm-ref (okf-concept-frontmatter c) "stale_after" #f))
(define (okf-concept-field c key)   (%fm-ref (okf-concept-frontmatter c) key #f))

;; A bare { by, at } map becomes a one-element list, per spec §5.2. A bare
;; map's own car is a (string . value) pair; a list-of-maps' car is itself
;; a whole alist (a list, not a string-keyed pair) — that's what tells
;; them apart. Malformed frontmatter (verified: true, verified: "x") isn't
;; a map or a list of maps at all — (not (pair? v)) below treats it as
;; absent rather than crashing on (car v).
(define (%normalize-verified v)
  (cond
    ((null? v) '())
    ((not (pair? v)) '())
    ((and (pair? (car v)) (string? (car (car v)))) (list v))
    (else v)))

(define (okf-concept-verified c)
  (%normalize-verified (%fm-ref (okf-concept-frontmatter c) "verified" '())))

;;; =========================================================================
;;; Trust and lifecycle
;;; =========================================================================

(define (%human-actor? by) (and (string? by) (%string-prefix? "human:" by)))

(define (okf-trust-tier c)
  (let ((vs (okf-concept-verified c)))
    (cond
      ((null? vs) 'unverified)
      ((%any (lambda (v) (%human-actor? (%fm-ref v "by" #f))) vs) 'human-reviewed)
      (else 'machine-confirmed))))

(define (okf-stale? c)
  (let ((sa (okf-concept-stale-after c)))
    (and sa (string>=? (%today) sa))))

;;; =========================================================================
;;; Querying
;;; =========================================================================

(define (okf-bundle-concepts bundle) (hash-table-values (okf-bundle-table bundle)))
(define (okf-bundle-ref bundle id)   (hash-table-ref (okf-bundle-table bundle) id #f))

(define (okf-concepts-by-type bundle type)
  (filter (lambda (c) (equal? (okf-concept-type c) type)) (okf-bundle-concepts bundle)))

(define (okf-concepts-by-tag bundle tag)
  (filter (lambda (c) (member tag (okf-concept-tags c))) (okf-bundle-concepts bundle)))

(define (okf-concepts-by-trust-tier bundle tier)
  (filter (lambda (c) (eq? (okf-trust-tier c) tier)) (okf-bundle-concepts bundle)))

(define (okf-concepts-stale bundle)
  (filter okf-stale? (okf-bundle-concepts bundle)))

(define (okf-attested-computation? c)
  (equal? (okf-concept-type c) "Attested Computation"))

;;; =========================================================================
;;; Graph
;;; =========================================================================

(define %okf-link-rx (regex-compile "\\[[^]]*\\]\\(([^)]*\\.md)\\)"))

(define (okf-concept-links c)
  (let loop ((s (okf-concept-body c)) (acc '()))
    (let ((m (regex-match-string %okf-link-rx s)))
      (if (not m)
          (reverse acc)
          (let* ((target (cadr m))
                 (offs (regex-match %okf-link-rx s))
                 (end (cdr (car offs))))
            (loop (substring s end (string-length s)) (cons target acc)))))))

(define (%dirname id)
  (let ((i (%string-last-index id #\/)))
    (if i (substring id 0 i) "")))

;; A ".." with an empty stack (more ".." segments than the path has depth)
;; is dropped rather than kept literal or treated as an error — the result
;; can never contain "..", so it can never resolve outside the bundle: the
;; only use of the result is a lookup against okf-bundle-table, which
;; contains nothing but real ids discovered by the directory walk. This is
;; intentional, not an oversight.
(define (%normalize-path p)
  (let loop ((parts (%split-on p #\/)) (stack '()))
    (cond
      ((null? parts) (%join-with (reverse stack) #\/))
      ((or (string=? (car parts) "") (string=? (car parts) "."))
       (loop (cdr parts) stack))
      ((string=? (car parts) "..")
       (loop (cdr parts) (if (pair? stack) (cdr stack) stack)))
      (else (loop (cdr parts) (cons (car parts) stack))))))

;; Resolves a raw `[text](target)` target string against a concept's own
;; location to a bundle id, or #f if it doesn't name a concept in the
;; bundle. Broken links are not errors (spec §6.1) — callers that care use
;; okf-bundle-broken-links to surface them.
(define (okf-resolve-link bundle c target)
  (let* ((t (if (%string-suffix? ".md" target)
                (substring target 0 (- (string-length target) 3))
                target))
         (candidate
           (if (and (> (string-length t) 0) (char=? (string-ref t 0) #\/))
               (substring t 1 (string-length t))
               (%normalize-path (string-append (%dirname (okf-concept-id c)) "/" t)))))
    (and (hash-table-exists? (okf-bundle-table bundle) candidate) candidate)))

(define (okf-bundle->graph bundle)
  (let ((g (make-hash-table)))
    (for-each
      (lambda (c)
        (hash-table-set! g (okf-concept-id c)
          (%filter-map (lambda (target) (okf-resolve-link bundle c target))
                       (okf-concept-links c))))
      (okf-bundle-concepts bundle))
    g))

(define (okf-graph-backlinks graph)
  (let ((back (make-hash-table)))
    (for-each (lambda (id) (hash-table-set! back id '())) (hash-table-keys graph))
    (for-each
      (lambda (id)
        (for-each
          (lambda (target)
            (hash-table-set! back target (cons id (hash-table-ref back target '()))))
          (hash-table-ref graph id '())))
      (hash-table-keys graph))
    back))

(define (okf-bundle-broken-links bundle)
  (%filter-map
    (lambda (c)
      (let ((broken (filter (lambda (target) (not (okf-resolve-link bundle c target)))
                             (okf-concept-links c))))
        (and (pair? broken) (cons (okf-concept-id c) broken))))
    (okf-bundle-concepts bundle)))

;;; =========================================================================
;;; Attested Computation helpers
;;; =========================================================================

;; OKF spec §10.2: runtime is REQUIRED at the concept's top level for
;; type: Attested Computation — never nested under executor (executor
;; only carries resource/receipt).
(define (okf-computation-runtime c)
  (%fm-ref (okf-concept-frontmatter c) "runtime" #f))

(define (okf-computation-parameters c)
  (%fm-ref (okf-concept-frontmatter c) "parameters" '()))

(define (%blank-line? line) (string=? (%string-trim-right line) ""))
(define (%indent4? line) (or (%string-prefix? "    " line) (%string-prefix? "\t" line)))
(define (%strip-indent4 line)
  (cond ((%string-prefix? "    " line) (substring line 4 (string-length line)))
        ((%string-prefix? "\t" line) (substring line 1 (string-length line)))
        (else line)))

;; acc accumulates newest-line-first; a trailing blank run in the source
;; is a leading blank run here, so trimming here is what drops it from
;; the final (reversed) result.
(define (%drop-leading-blanks lines)
  (if (and (pair? lines) (string=? (car lines) ""))
      (%drop-leading-blanks (cdr lines))
      lines))

;; Extracts the computation appearing under a "# Computation" heading, per
;; OKF spec §10.2/§10.3: either a fenced code block, or a plain 4-space/
;; tab-indented block (the spec's own worked example uses the latter).
;; Returns #f if there's no such heading, or no block before the next
;; top-level heading.
(define (okf-computation-inline-sql c)
  (let loop ((lines (%split-lines (okf-concept-body c)))
             (in-section #f) (mode #f) (acc '()))
    (cond
      ((eq? mode 'fence)
       (cond
         ((null? lines) #f)
         ((%string-prefix? "```" (car lines)) (%join-lines (reverse acc)))
         (else (loop (cdr lines) in-section mode (cons (car lines) acc)))))
      ((eq? mode 'indent)
       (if (and (pair? lines)
                (or (%blank-line? (car lines)) (%indent4? (car lines))))
           (loop (cdr lines) in-section mode
                 (cons (if (%blank-line? (car lines)) "" (%strip-indent4 (car lines))) acc))
           (if (pair? acc) (%join-lines (reverse (%drop-leading-blanks acc))) #f)))
      ((null? lines) #f)
      (in-section
       (cond
         ((%blank-line? (car lines)) (loop (cdr lines) in-section mode acc))
         ((%string-prefix? "```" (car lines)) (loop (cdr lines) in-section 'fence acc))
         ((%indent4? (car lines)) (loop (cdr lines) in-section 'indent (list (%strip-indent4 (car lines)))))
         ((%string-prefix? "# " (car lines)) #f)
         (else (loop (cdr lines) in-section mode acc))))
      ((string=? (%string-trim-right (car lines)) "# Computation")
       (loop (cdr lines) #t #f acc))
      (else (loop (cdr lines) in-section mode acc)))))

;;; =========================================================================
;;; Frontmatter validation
;;; =========================================================================
;;
;; Advisory, never enforced at load time: OKF's own conformance rule is that
;; a consumer ignoring unknown keys is fully conformant, and okf-load-bundle
;; plus the tolerant accessors above (okf-concept-verified's malformed-input
;; handling, okf-concept-type's missing-type tolerance) deliberately keep
;; loading working even when frontmatter doesn't match the spec's shapes.
;; okf-validate-concept is for a caller who wants to know about those shape
;; problems anyway — e.g. before trusting a concept's derived okf-trust-tier,
;; or before publishing agent-produced output — without making every other
;; caller pay for strict enforcement they didn't ask for. It reads the raw
;; frontmatter directly rather than through the tolerant accessors, since
;; those accessors' whole point is to paper over exactly what this is meant
;; to surface.

(define %okf-date-rx (regex-compile "^[0-9]{4}-[0-9]{2}-[0-9]{2}$"))
(define (%iso-date-shaped? s) (and (string? s) (regex-match %okf-date-rx s) #t))

;; A "map" here means: whatever (curry yaml) produces for a YAML mapping —
;; a list of (string . value) pairs. '() counts as a (trivially empty) map.
;; Every element is checked, not just the first: core assoc (used by
;; %map-has-nonempty-string? below) has no type guard on later list
;; elements, so a list that's a valid pair up front but degrades into a
;; bare non-pair scalar further along (adversarial/malformed frontmatter
;; is exactly the input this validator has to expect) would otherwise
;; reach assoc and crash rather than being reported as a clean issue.
(define (%pair-map-entry? e) (and (pair? e) (string? (car e))))
(define (%map-shaped? v)
  (and (list? v) (%every %pair-map-entry? v)))

(define (%list-of-strings? v) (and (list? v) (%every string? v)))
(define (%list-of-maps? v) (and (list? v) (%every %map-shaped? v)))

(define (%map-has-nonempty-string? m key)
  (let ((v (%fm-ref m key #f))) (and (string? v) (> (string-length v) 0))))

;; Checks the shapes the spec actually promises: type (§required), status
;; (§an enum), stale_after (§5.5, an ISO 8601 date), tags/sources (§list
;; shapes), and generated/verified (§5.2, each a by/at-bearing map or, for
;; verified, a list of them). Returns a list of human-readable issue
;; strings, '() when clean.
(define (okf-validate-concept c)
  (let ((fm (okf-concept-frontmatter c)))
    (append
      (let ((type (%fm-ref fm "type" #f)))
        (if (and (string? type) (> (string-length type) 0))
            '()
            (list "type is required and must be a non-empty string (spec REQUIRED)")))
      (let ((status (%fm-ref fm "status" #f)))
        (if (or (not status) (member status '("draft" "stable" "deprecated")))
            '()
            (list (string-append "status must be draft/stable/deprecated, got: "
                                  (if (string? status) status "(non-string value)")))))
      (let ((sa (%fm-ref fm "stale_after" #f)))
        (if (or (not sa) (%iso-date-shaped? sa))
            '()
            (list "stale_after must be a YYYY-MM-DD date string")))
      (let ((tags (%fm-ref fm "tags" #f)))
        (if (or (not tags) (%list-of-strings? tags))
            '()
            (list "tags must be a list of strings")))
      (let ((sources (%fm-ref fm "sources" #f)))
        (if (or (not sources) (%list-of-maps? sources))
            '()
            (list "sources must be a list of maps")))
      (let ((generated (%fm-ref fm "generated" #f)))
        (cond
          ((not generated) '())
          ((not (%map-shaped? generated)) (list "generated must be a map"))
          ((not (%map-has-nonempty-string? generated "by")) (list "generated.by must be a non-empty string"))
          (else '())))
      (let ((verified (%fm-ref fm "verified" #f)))
        (cond
          ((or (not verified) (null? verified)) '())
          ((%map-shaped? verified)
           (if (%map-has-nonempty-string? verified "by") '() (list "verified.by must be a non-empty string")))
          ((%list-of-maps? verified)
           (%filter-map (lambda (v) (if (%map-has-nonempty-string? v "by") #f "verified entry missing a non-empty by"))
                        verified))
          (else (list "verified must be a map or a list of maps")))))))

;; Bundle-wide sweep, same (id . issues) shape convention as
;; okf-bundle-broken-links: only concepts with at least one issue appear.
(define (okf-validate-bundle bundle)
  (%filter-map
    (lambda (c)
      (let ((issues (okf-validate-concept c)))
        (and (pair? issues) (cons (okf-concept-id c) issues))))
    (okf-bundle-concepts bundle)))

;;; =========================================================================
;;; Writing
;;; =========================================================================

(define (%plist-ref plist key default)
  (cond ((null? plist) default)
        ((eq? (car plist) key) (cadr plist))
        (else (%plist-ref (cddr plist) key default))))

(define (%fm-entry key val) (and val (cons key val)))

(define (make-okf-concept . rest)
  (let* ((type        (%plist-ref rest '#:type #f))
         (title       (%plist-ref rest '#:title #f))
         (description (%plist-ref rest '#:description #f))
         (resource    (%plist-ref rest '#:resource #f))
         (tags        (%plist-ref rest '#:tags '()))
         (sources     (%plist-ref rest '#:sources '()))
         (generated   (%plist-ref rest '#:generated #f))
         (verified    (%plist-ref rest '#:verified '()))
         (status      (%plist-ref rest '#:status "stable"))
         (stale-after (%plist-ref rest '#:stale-after #f))
         (body        (%plist-ref rest '#:body "")))
    (unless type (error "make-okf-concept: #:type is required"))
    (let ((fm (filter (lambda (x) x)
                (list
                  (cons "type" type)
                  (%fm-entry "title" title)
                  (%fm-entry "description" description)
                  (%fm-entry "resource" resource)
                  (if (pair? tags) (cons "tags" tags) #f)
                  (if (pair? sources) (cons "sources" sources) #f)
                  (%fm-entry "generated" generated)
                  (if (pair? verified) (cons "verified" verified) #f)
                  (cons "status" status)
                  (%fm-entry "stale_after" stale-after)))))
      (make-okf-concept% #f #f fm body))))

(define (okf-write-concept-port c port)
  (display "---\n" port)
  (yaml-write (okf-concept-frontmatter c) port)
  (display "---\n" port)
  (display (okf-concept-body c) port))

;; Tolerates the directory having sprung into existence between the
;; file-exists? check and create-directory (two agents racing to create
;; the same first-of-its-kind subdirectory) by re-checking before
;; propagating the error — anything else (permission denied, etc.) still
;; raises.
(define (%ensure-parent-dirs! dir)
  (unless (or (string=? dir "") (file-exists? dir))
    (%ensure-parent-dirs! (%dirname dir))
    (guard (e (#t (unless (file-exists? dir) (raise e))))
      (create-directory dir))))

;; A fixed ".tmp" suffix would be a predictable path a concurrent process
;; could pre-place (e.g. as a symlink) before the write lands; folding in
;; the writer's pid keeps it from being guessable in advance without
;; needing a dedicated random-bytes source.
(define (%tmp-sibling path) (string-append path ".tmp." (number->string (pid))))

;; Writes to a pid-suffixed `.tmp` sibling, then renames — so a crash
;; mid-write never leaves a half-written concept file.
(define (okf-write-concept c root id)
  (unless (%safe-bundle-relative? id)
    (error "okf-write-concept: id must be bundle-relative, with no .. segments or embedded NUL" id))
  (let* ((path (string-append root "/" id ".md"))
         (tmp (%tmp-sibling path)))
    (%ensure-parent-dirs! (%dirname path))
    (call-with-output-file tmp (lambda (p) (okf-write-concept-port c p)))
    (rename-file tmp path)))

;;; =========================================================================
;;; Index and log generation
;;; =========================================================================

(define (%dir-path bundle dir name)
  (string-append (okf-bundle-root bundle)
                  (if (string=? dir "") "" (string-append "/" dir))
                  "/" name))

(define (%titleize s)
  (if (= (string-length s) 0)
      s
      (string-append (string (char-upcase (string-ref s 0)))
                      (substring s 1 (string-length s)))))

;; Regenerates index.md for a directory prefix: collects every concept
;; whose id starts with prefix and has no further "/" beyond it (i.e.
;; immediate children only, not nested subdirectories), and writes a
;; "# Section" heading followed by a "* [title](id.md) - description"
;; listing.
(define (okf-generate-index bundle prefix0)
  (let ((prefix (%strip-trailing-slash prefix0)))
    (unless (%safe-bundle-relative? prefix)
      (error "okf-generate-index: prefix must be bundle-relative, with no .. segments or embedded NUL" prefix))
    (let* ((pfx (if (string=? prefix "") "" (string-append prefix "/")))
           (children
             (filter
               (lambda (c)
                 (let ((id (okf-concept-id c)))
                   (and (%string-prefix? pfx id)
                        (not (%string-last-index (substring id (string-length pfx) (string-length id)) #\/)))))
               (okf-bundle-concepts bundle)))
           (section (if (string=? prefix "") "Index" (%titleize prefix)))
           (path (%dir-path bundle prefix "index.md")))
      (%ensure-parent-dirs! (%dirname path))
      (call-with-output-file path
        (lambda (out)
          (display "# " out) (display section out) (newline out) (newline out)
          (for-each
            (lambda (c)
              (let* ((id (okf-concept-id c))
                     (rel (substring id (string-length pfx) (string-length id)))
                     (title (or (okf-concept-title c) rel))
                     (desc (okf-concept-description c)))
                (display "* [" out) (display title out) (display "](" out)
                (display rel out) (display ".md)" out)
                (when desc (display " - " out) (display desc out))
                (newline out)))
            children))))))

;; Appends an entry to log.md at the given directory level, creating it if
;; absent. Like okf-write-concept, the actual file replacement goes
;; through a pid-suffixed tmp+rename so a crash mid-write can't corrupt
;; the log — though the read-existing/append/write-back sequence itself
;; is not lock-protected, so two writers racing on the same log.md can
;; still each read the same "existing" content and one's entry will be
;; lost to the other's rename; there is no OS-level file-locking primitive
;; in (curry posix) to close that window.
(define (okf-log-append bundle dir0 kind text)
  (let ((dir (%strip-trailing-slash dir0)))
    (unless (%safe-bundle-relative? dir)
      (error "okf-log-append: dir must be bundle-relative, with no .. segments or embedded NUL" dir))
    (let* ((path (%dir-path bundle dir "log.md"))
           (tmp (%tmp-sibling path))
           (existing (if (file-exists? path) (%read-whole-file path) ""))
           (entry (string-append "- " (%today) " **" kind "** — " text "\n")))
      (%ensure-parent-dirs! (%dirname path))
      (call-with-output-file tmp
        (lambda (out) (display existing out) (display entry out)))
      (rename-file tmp path))))

))
