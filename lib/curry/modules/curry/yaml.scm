;;; (curry yaml) — YAML 1.1/1.2-ish reader and writer, pure Scheme.
;;;
;;; Unlike (curry json), which collapses YAML/JSON null and #f into the same
;;; Scheme value, null is represented here by a distinguished sentinel object
;;; (yaml-null), compared with yaml-null? — the same approach SRFI-180 takes
;;; for JSON, and the right one for YAML specifically since null-vs-false is
;;; a distinction real config files (Kubernetes manifests, CI configs, …)
;;; actually rely on.
;;;
;;; Supported: block and flow mappings/sequences, all scalar styles (plain,
;;; single-/double-quoted, literal |, folded >), comments, --- document
;;; markers and multi-document streams, implicit scalar typing (null/bool/
;;; int/float/string) plus the core explicit tags (!!str/!!int/!!float/
;;; !!bool/!!null) to override it, anchors (&name) and aliases (*name), and
;;; merge keys (<<: *anchor) in block and flow mappings.
;;;
;;; Deliberately not supported (see docs/reference/module-yaml.md): complex
;;; (explicit "? key" / ": value") mapping keys, YAML directives (%YAML/%TAG
;;; are skipped, not processed), custom/application tags (ignored, content
;;; still parsed with implicit typing), and the YAML 1.1 sexagesimal number
;;; form. The writer does not detect or preserve shared/circular structure
;;; via anchors — each occurrence is serialized independently, so a script
;;; that builds a genuinely circular structure will loop forever.

(import (scheme base))

;;; =========================================================================
;;; The null sentinel
;;; =========================================================================

(define yaml-null (list 'yaml-null))
(define (yaml-null? x) (eq? x yaml-null))

;;; =========================================================================
;;; Small local helpers (kept independent of (srfi s1 lists))
;;; =========================================================================

(define (%string-suffix? suf s)
  (let ((ls (string-length s)) (lf (string-length suf)))
    (and (>= ls lf) (string=? (substring s (- ls lf) ls) suf))))

(define (%string-trim-right s)
  (let loop ((i (string-length s)))
    (if (and (> i 0) (char-whitespace? (string-ref s (- i 1))))
        (loop (- i 1))
        (substring s 0 i))))

(define (%string-trim-left s)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (if (and (< i n) (char-whitespace? (string-ref s i)))
          (loop (+ i 1))
          (substring s i n)))))

(define (%string-trim s) (%string-trim-left (%string-trim-right s)))

(define (%string-index s ch start)
  (let ((n (string-length s)))
    (let loop ((i start))
      (cond ((>= i n) #f)
            ((char=? (string-ref s i) ch) i)
            (else (loop (+ i 1)))))))

;;; =========================================================================
;;; Cursor: a mutable (string pos len) triple. Every parse-* function below
;;; mutates the cursor's position as it consumes input and returns only the
;;; parsed value — an imperative style, deliberately, since that's what a
;;; hand-written recursive-descent parser over indentation-sensitive text
;;; actually wants; threading position functionally through 30-odd mutually
;;; recursive functions would obscure the control flow far more than it'd
;;; buy in purity.
;;; =========================================================================

(define (%mk-cursor s) (vector s 0 (string-length s)))
(define (%c-str c) (vector-ref c 0))
(define (%c-pos c) (vector-ref c 1))
(define (%c-len c) (vector-ref c 2))
(define (%c-set! c p) (vector-set! c 1 p))
(define (%c-eof? c) (>= (%c-pos c) (%c-len c)))
(define (%c-ch c) (if (%c-eof? c) #f (string-ref (%c-str c) (%c-pos c))))
(define (%c-ch-at c k)
  (let ((p (+ (%c-pos c) k)))
    (if (or (< p 0) (>= p (%c-len c))) #f (string-ref (%c-str c) p))))
(define (%c-adv! c) (%c-set! c (+ 1 (%c-pos c))))
(define (%c-adv-n! c n) (%c-set! c (+ n (%c-pos c))))

(define (%c-looking-at? c lit)
  (let ((n (string-length lit)))
    (let loop ((i 0))
      (cond ((= i n) #t)
            ((not (eqv? (%c-ch-at c i) (string-ref lit i))) #f)
            (else (loop (+ i 1)))))))

;; Column of the current position: number of characters since the previous
;; newline (or start of string). O(line length); fine for realistic input.
(define (%c-column c)
  (let ((s (%c-str c)))
    (let loop ((i (%c-pos c)) (n 0))
      (if (or (= i 0) (char=? (string-ref s (- i 1)) #\newline))
          n
          (loop (- i 1) (+ n 1))))))

(define (%c-skip-line! c)
  (let loop ()
    (cond ((%c-eof? c) #t)
          ((char=? (%c-ch c) #\newline) (%c-adv! c))
          (else (%c-adv! c) (loop)))))

;; Advance past leading spaces, blank lines, and full-comment lines, leaving
;; the cursor at the first content character of a line (or at EOF). Tabs are
;; not valid YAML indentation and are deliberately left as content so a
;; malformed file fails to parse rather than being silently misread.
(define (%skip-to-content! c)
  (let loop ()
    (let sp ()
      (when (and (not (%c-eof? c)) (char=? (%c-ch c) #\space)) (%c-adv! c) (sp)))
    (cond
      ((%c-eof? c) #f)
      ((char=? (%c-ch c) #\newline) (%c-adv! c) (loop))
      ((char=? (%c-ch c) #\#) (%c-skip-line! c) (loop))
      ((and (char=? (%c-ch c) #\return) (eqv? (%c-ch-at c 1) #\newline))
       (%c-adv-n! c 2) (loop))
      (else #t))))

(define (%at-doc-marker? c)
  (and (= (%c-column c) 0)
       (or (and (%c-looking-at? c "---")
                (memv (%c-ch-at c 3) (list #f #\space #\newline #\tab)))
           (and (%c-looking-at? c "...")
                (memv (%c-ch-at c 3) (list #f #\space #\newline #\tab))))))

;; Skip whitespace freely (spaces, tabs, newlines, comments) — used inside
;; flow collections, where line breaks are just whitespace.
(define (%skip-flow-ws! c)
  (let loop ()
    (cond
      ((%c-eof? c) #f)
      ((memv (%c-ch c) (list #\space #\tab #\newline)) (%c-adv! c) (loop))
      ((char=? (%c-ch c) #\return) (%c-adv! c) (loop))
      ((char=? (%c-ch c) #\#) (%c-skip-line! c) (loop))
      (else #t))))

;;; =========================================================================
;;; Scalar type resolution (implicit typing of plain scalars)
;;; =========================================================================

(define (%digit? ch) (and ch (char-numeric? ch)))

(define (%looks-like-int? s)
  (let ((n (string-length s)))
    (and (> n 0)
         (let ((start (if (memv (string-ref s 0) (list #\+ #\-)) 1 0)))
           (and (< start n)
                (let loop ((i start) (any #f))
                  (cond ((= i n) any)
                        ((char=? (string-ref s i) #\_) (loop (+ i 1) any))
                        ((%digit? (string-ref s i)) (loop (+ i 1) #t))
                        (else #f))))))))

(define (%looks-like-hex-or-octal? s)
  (and (>= (string-length s) 3)
       (char=? (string-ref s 0) #\0)
       (memv (string-ref s 1) (list #\x #\o))))

(define (%looks-like-float? s)
  (let* ((n (string-length s))
         (start (if (and (> n 0) (memv (string-ref s 0) (list #\+ #\-))) 1 0)))
    (and (> n start)
         (let loop ((i start) (seen-digit #f) (seen-dot #f) (seen-exp #f) (ok #t))
           (cond
             ((not ok) #f)
             ((= i n) (and seen-digit (or seen-dot seen-exp)))
             (else
              (let ((ch (string-ref s i)))
                (cond
                  ((char=? ch #\_) (loop (+ i 1) seen-digit seen-dot seen-exp ok))
                  ((%digit? ch) (loop (+ i 1) #t seen-dot seen-exp ok))
                  ((and (char=? ch #\.) (not seen-dot) (not seen-exp))
                   (loop (+ i 1) seen-digit #t seen-exp ok))
                  ((and (memv ch (list #\e #\E)) seen-digit (not seen-exp))
                   (let ((j (+ i 1)))
                     (loop (if (and (< j n) (memv (string-ref s j) (list #\+ #\-))) (+ j 1) j)
                           seen-digit seen-dot #t ok)))
                  (else #f)))))))))

(define (%strip-underscores s)
  (let ((out (open-output-string)))
    (let loop ((i 0))
      (when (< i (string-length s))
        (unless (char=? (string-ref s i) #\_) (write-char (string-ref s i) out))
        (loop (+ i 1))))
    (get-output-string out)))

(define (%resolve-plain-scalar s)
  (cond
    ((or (string=? s "") (string=? s "~") (string-ci=? s "null")) yaml-null)
    ((or (string-ci=? s "true") (string-ci=? s "yes") (string-ci=? s "on")) #t)
    ((or (string-ci=? s "false") (string-ci=? s "no") (string-ci=? s "off")) #f)
    ((or (string-ci=? s ".inf") (string-ci=? s "+.inf")) +inf.0)
    ((string-ci=? s "-.inf") -inf.0)
    ((string-ci=? s ".nan") +nan.0)
    ((%looks-like-hex-or-octal? s)
     (let ((radix (if (char=? (string-ref s 1) #\x) 16 8))
           (digits (%strip-underscores (substring s 2 (string-length s)))))
       (or (string->number digits radix) s)))
    ((%looks-like-int? s) (or (string->number (%strip-underscores s)) s))
    ((%looks-like-float? s) (or (string->number (%strip-underscores s)) s))
    (else s)))

;; Apply an explicit core-schema tag (already parsed by the caller) to force
;; the type of a scalar's raw text, overriding implicit resolution. Any tag
;; other than the core-schema scalar tags is ignored (content still parses
;; with normal implicit typing) — see the module header for why.
(define (%apply-tag tag raw resolved)
  (cond
    ((not tag) resolved)
    ((string=? tag "!!str") raw)
    ((string=? tag "!!int") (or (string->number (%strip-underscores raw)) raw))
    ((string=? tag "!!float") (or (string->number (%strip-underscores raw)) raw))
    ((string=? tag "!!bool") (if (or (string-ci=? raw "true") (string-ci=? raw "yes")) #t #f))
    ((string=? tag "!!null") yaml-null)
    (else resolved)))

;;; =========================================================================
;;; Quoted scalars
;;; =========================================================================

;; Single-quoted: '' is an escaped literal single quote; nothing else is
;; special. Can span multiple lines (line breaks fold to a single space,
;; blank lines fold to a real newline, matching YAML's line-folding rule).
(define (%parse-single-quoted! c)
  (%c-adv! c) ; opening '
  (let ((out (open-output-string)))
    (let loop ((blank-run 0))
      (cond
        ((%c-eof? c) (get-output-string out))
        ((and (char=? (%c-ch c) #\') (eqv? (%c-ch-at c 1) #\'))
         (write-char #\' out) (%c-adv-n! c 2) (loop 0))
        ((char=? (%c-ch c) #\') (%c-adv! c) (get-output-string out))
        ((char=? (%c-ch c) #\newline)
         (%c-adv! c)
         (loop (+ blank-run 1)))
        (else
         (when (> blank-run 0)
           (write-char (if (> blank-run 1) #\newline #\space) out))
         (write-char (%c-ch c) out) (%c-adv! c) (loop 0))))))

(define (%hex-digit-value ch)
  (cond ((and (char>=? ch #\0) (char<=? ch #\9)) (- (char->integer ch) (char->integer #\0)))
        ((and (char>=? ch #\a) (char<=? ch #\f)) (+ 10 (- (char->integer ch) (char->integer #\a))))
        ((and (char>=? ch #\A) (char<=? ch #\F)) (+ 10 (- (char->integer ch) (char->integer #\A))))
        (else #f)))

(define (%read-hex-escape! c n)
  (let loop ((i 0) (acc 0))
    (if (= i n)
        acc
        (let ((v (%hex-digit-value (%c-ch c))))
          (if (not v)
              (error "yaml: bad hex escape")
              (begin (%c-adv! c) (loop (+ i 1) (+ (* acc 16) v))))))))

;; Double-quoted: full backslash-escape set plus line folding like single-
;; quoted (blank line -> newline, single line break -> space).
(define (%parse-double-quoted! c)
  (%c-adv! c) ; opening "
  (let ((out (open-output-string)))
    (let loop ((blank-run 0))
      (cond
        ((%c-eof? c) (get-output-string out))
        ((char=? (%c-ch c) #\") (%c-adv! c) (get-output-string out))
        ((char=? (%c-ch c) #\newline) (%c-adv! c) (loop (+ blank-run 1)))
        ((char=? (%c-ch c) #\\)
         (when (> blank-run 0) (write-char (if (> blank-run 1) #\newline #\space) out))
         (%c-adv! c)
         (let ((e (%c-ch c)))
           (cond
             ((eqv? e #\n) (write-char #\newline out) (%c-adv! c))
             ((eqv? e #\t) (write-char #\tab out) (%c-adv! c))
             ((eqv? e #\r) (write-char #\return out) (%c-adv! c))
             ((eqv? e #\0) (write-char (integer->char 0) out) (%c-adv! c))
             ((eqv? e #\") (write-char #\" out) (%c-adv! c))
             ((eqv? e #\\) (write-char #\\ out) (%c-adv! c))
             ((eqv? e #\/) (write-char #\/ out) (%c-adv! c))
             ((eqv? e #\a) (write-char (integer->char 7) out) (%c-adv! c))
             ((eqv? e #\b) (write-char (integer->char 8) out) (%c-adv! c))
             ((eqv? e #\f) (write-char (integer->char 12) out) (%c-adv! c))
             ((eqv? e #\v) (write-char (integer->char 11) out) (%c-adv! c))
             ((eqv? e #\e) (write-char (integer->char 27) out) (%c-adv! c))
             ((eqv? e #\newline) (%c-adv! c)) ; escaped line break: no fold char at all
             ((eqv? e #\x) (%c-adv! c) (write-char (integer->char (%read-hex-escape! c 2)) out))
             ((eqv? e #\u) (%c-adv! c) (write-char (integer->char (%read-hex-escape! c 4)) out))
             ((eqv? e #\U) (%c-adv! c) (write-char (integer->char (%read-hex-escape! c 8)) out))
             (else (write-char e out) (%c-adv! c))))
         (loop 0))
        (else
         (when (> blank-run 0) (write-char (if (> blank-run 1) #\newline #\space) out))
         (write-char (%c-ch c) out) (%c-adv! c) (loop 0))))))

;;; =========================================================================
;;; Plain scalars
;;; =========================================================================

;; True if the character at c's current position begins ": " / ":<EOL>" —
;; the mapping-value indicator — which ends a plain scalar wherever it
;; appears (at top level or inside flow).
(define (%at-colon-indicator? c)
  (and (eqv? (%c-ch c) #\:)
       (memv (%c-ch-at c 1) (list #f #\space #\tab #\newline))))

;; Read one line's worth of plain-scalar text starting at c's position,
;; stopping at EOL, at " #" (a comment), or at the mapping colon indicator.
;; `flow?` additionally stops at , ] } (also relevant inside flow colon use).
(define (%read-plain-line! c flow?)
  (let ((out (open-output-string)))
    (let loop ()
      (cond
        ((%c-eof? c) (get-output-string out))
        ((char=? (%c-ch c) #\newline) (get-output-string out))
        ((%at-colon-indicator? c) (get-output-string out))
        ((and flow? (memv (%c-ch c) (list #\, #\] #\}))) (get-output-string out))
        ((and (char=? (%c-ch c) #\#)
              (> (%c-pos c) 0)
              (char-whitespace? (string-ref (%c-str c) (- (%c-pos c) 1))))
         (get-output-string out))
        (else (write-char (%c-ch c) out) (%c-adv! c) (loop))))))

;; Block-context plain scalar, including line-folded continuations: after
;; the first line, any following line indented strictly more than
;; parent-indent and not itself a new structural line is folded in.
(define (%parse-plain-block-scalar! c parent-indent)
  (let ((first (%string-trim-right (%read-plain-line! c #f))))
    (let loop ((lines (list first)))
      (let ((save (%c-pos c)))
        (if (or (%c-eof? c) (not (char=? (%c-ch c) #\newline)))
            (begin (%c-set! c save) (%finish-plain-fold lines))
            (begin
              (%c-adv! c) ; consume the newline after the previous line
              (let ((probe-save (%c-pos c)))
                (%skip-to-content! c)
                (cond
                  ((or (%c-eof? c) (%at-doc-marker? c) (<= (%c-column c) parent-indent))
                   (%c-set! c probe-save) (%c-set! c save)
                   (%finish-plain-fold lines))
                  ((or (%at-colon-indicator? c)
                       (and (char=? (%c-ch c) #\-) (memv (%c-ch-at c 1) (list #f #\space #\newline))))
                   (%c-set! c save) (%finish-plain-fold lines))
                  (else
                   (let ((line (%string-trim-right (%read-plain-line! c #f))))
                     (loop (append lines (list line)))))))))))))

(define (%finish-plain-fold lines)
  (let ((out (open-output-string)))
    (let loop ((ls lines) (first #t) (prev-blank #f))
      (unless (null? ls)
        (let ((line (car ls)))
          (cond
            (first (write-string line out))
            ((string=? line "") 'defer) ; blank line: contributes a newline once next non-blank appears
            (prev-blank (write-char #\newline out) (write-string line out))
            (else (write-char #\space out) (write-string line out)))
          (loop (cdr ls) #f (string=? line "")))))
    (%string-trim (get-output-string out))))

;;; =========================================================================
;;; Block scalars: | (literal) and > (folded)
;;; =========================================================================

(define (%parse-block-scalar! c parent-indent)
  (let ((style (%c-ch c)))
    (%c-adv! c)
    (let ((chomp #f) (explicit-indent #f))
      (let hdr ()
        (cond
          ((memv (%c-ch c) (list #\- #\+))
           (set! chomp (%c-ch c)) (%c-adv! c) (hdr))
          ((%digit? (%c-ch c))
           (set! explicit-indent (- (char->integer (%c-ch c)) (char->integer #\0)))
           (%c-adv! c) (hdr))
          (else 'done)))
      (%c-skip-line! c) ; rest of header line (comment allowed), through the newline
      (let* ((content-indent #f)
             (raw-lines '()))
        ;; Determine content indent from the first non-blank line if not given.
        (let loop ()
          (let ((save (%c-pos c)))
            (cond
              ((%c-eof? c) 'done)
              (else
               (let ((line-start (%c-pos c)))
                 ;; scan this raw line's leading spaces without consulting %skip-to-content!
                 ;; (which would eat comments — invalid inside a block scalar body).
                 (let sp ((i (%c-pos c)))
                   (if (and (< i (%c-len c)) (char=? (string-ref (%c-str c) i) #\space))
                       (sp (+ i 1))
                       (%c-set! c i)))
                 (let ((indent-here (- (%c-pos c) line-start)))
                   (cond
                     ((or (%c-eof? c) (char=? (%c-ch c) #\newline))
                      ;; blank line: keep as empty, indent doesn't decide anything
                      (set! raw-lines (append raw-lines (list "")))
                      (unless (%c-eof? c) (%c-adv! c))
                      (loop))
                     ((and (not content-indent) explicit-indent)
                      (set! content-indent (+ parent-indent explicit-indent))
                      (%c-set! c line-start) (loop))
                     ((and (not content-indent) (> indent-here parent-indent))
                      (set! content-indent indent-here)
                      (%c-set! c line-start) (loop))
                     ((and content-indent (>= indent-here content-indent))
                      (%c-set! c (+ line-start content-indent))
                      (let ((text (%read-raw-line! c)))
                        (set! raw-lines (append raw-lines (list text)))
                        (loop)))
                     (else
                      ;; dedent below content: this line belongs to the outer context
                      (%c-set! c line-start)
                      'done))))))))
        (let* ((joined (if (eq? style #\|) (%join-literal raw-lines) (%join-folded raw-lines)))
               (chomped (%apply-chomp joined chomp)))
          chomped)))))

(define (%read-raw-line! c)
  (let ((out (open-output-string)))
    (let loop ()
      (cond
        ((%c-eof? c) (get-output-string out))
        ((char=? (%c-ch c) #\newline) (%c-adv! c) (get-output-string out))
        (else (write-char (%c-ch c) out) (%c-adv! c) (loop))))))

(define (%join-literal lines)
  (let ((out (open-output-string)))
    (let loop ((ls lines) (first #t))
      (unless (null? ls)
        (unless first (write-char #\newline out))
        (write-string (car ls) out)
        (loop (cdr ls) #f)))
    (get-output-string out)))

(define (%join-folded lines)
  (let ((out (open-output-string)))
    (let loop ((ls lines) (first #t) (prev-blank #f))
      (unless (null? ls)
        (let ((line (car ls)))
          (cond
            (first (write-string line out))
            ((string=? line "") (write-char #\newline out))
            (prev-blank (write-string line out))
            (else (write-char #\space out) (write-string line out)))
          (loop (cdr ls) #f (string=? line "")))))
    (get-output-string out)))

(define (%apply-chomp s chomp)
  (cond
    ((eqv? chomp #\-) (%string-trim-right-newlines s))
    ((eqv? chomp #\+) (string-append s "\n"))
    (else (string-append (%string-trim-right-newlines s) "\n"))))

(define (%string-trim-right-newlines s)
  (let loop ((i (string-length s)))
    (if (and (> i 0) (char=? (string-ref s (- i 1)) #\newline))
        (loop (- i 1))
        (substring s 0 i))))

;;; =========================================================================
;;; Anchors, aliases, tags — shared prefix-handling for both block and flow
;;; node dispatch.
;;; =========================================================================

(define (%read-name-token! c)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (%c-ch c)))
        (if (or (not ch) (char-whitespace? ch) (memv ch (list #\, #\[ #\] #\{ #\} #\:)))
            (get-output-string out)
            (begin (write-char ch out) (%c-adv! c) (loop)))))))

(define (%read-tag-token! c)
  ;; consumes leading '!' or '!!' and the following token, returns e.g. "!!str"
  (let ((out (open-output-string)))
    (write-char #\! out) (%c-adv! c)
    (when (eqv? (%c-ch c) #\!) (write-char #\! out) (%c-adv! c))
    (let loop ()
      (let ((ch (%c-ch c)))
        (if (or (not ch) (char-whitespace? ch))
            (get-output-string out)
            (begin (write-char ch out) (%c-adv! c) (loop)))))))

;;; =========================================================================
;;; Block collections
;;; =========================================================================

(define (%starts-seq-item? c)
  (and (eqv? (%c-ch c) #\-) (memv (%c-ch-at c 1) (list #f #\space #\newline #\tab))))

;; Scan the current line only (simple keys can't span lines) for an
;; unquoted ": " or ":"+EOL, tracking quote state so a colon inside a
;; quoted key doesn't count. Non-mutating (does not advance the cursor).
(define (%line-has-mapping-colon? c)
  (let ((s (%c-str c)) (len (%c-len c)))
    (let loop ((i (%c-pos c)) (q #f))
      (cond
        ((>= i len) #f)
        (else
         (let ((ch (string-ref s i)))
           (cond
             ((char=? ch #\newline) #f)
             ((and q (char=? ch q)) (loop (+ i 1) #f))
             (q (loop (+ i 1) q))
             ((memv ch (list #\' #\")) (loop (+ i 1) ch))
             ((and (char=? ch #\#) (> i (%c-pos c)) (char-whitespace? (string-ref s (- i 1)))) #f)
             ((and (char=? ch #\:) (or (= (+ i 1) len) (memv (string-ref s (+ i 1)) (list #\space #\tab #\newline))))
              #t)
             (else (loop (+ i 1) q)))))))))

(define (%parse-node c min-indent anchors) (%parse-node* c min-indent anchors #f))

;; Shared by mapping-value parsing and the &anchor/!tag prefixes below: `c`
;; is positioned right after a ':'/anchor-name/tag-name and any inline
;; spaces. If nothing but a comment or newline follows, the value is
;; entirely on subsequent lines indented more than `indent` — skip to the
;; next content line and recurse there; otherwise the value continues
;; inline on the current line.
(define (%parse-value-inline-or-nested! c indent anchors flow?)
  (cond
    ((or (%c-eof? c) (char=? (%c-ch c) #\newline) (char=? (%c-ch c) #\#))
     (%c-skip-line! c)
     (%skip-to-content! c)
     (if (or (%c-eof? c) (%at-doc-marker? c) (<= (%c-column c) indent))
         yaml-null
         (%parse-node* c (+ indent 1) anchors flow?)))
    (else (%parse-node* c indent anchors flow?))))

;; Core dispatcher. `flow?` selects flow-scalar termination rules when
;; parsing a bare scalar inside a flow collection.
(define (%parse-node* c min-indent anchors flow?)
  (cond
    ((or (%c-eof? c) (%at-doc-marker? c)) yaml-null)
    ((< (%c-column c) min-indent) yaml-null)
    ((eqv? (%c-ch c) #\&)
     (%c-adv! c)
     (let ((name (%read-name-token! c)))
       (%skip-flow-ws-inline! c)
       (let ((value (%parse-value-inline-or-nested! c min-indent anchors flow?)))
         (hash-table-set! anchors name value)
         value)))
    ((eqv? (%c-ch c) #\*)
     (%c-adv! c)
     (let ((name (%read-name-token! c)))
       (if (hash-table-exists? anchors name)
           (hash-table-ref anchors name)
           (error (string-append "yaml: unknown alias *" name)))))
    ((eqv? (%c-ch c) #\!)
     (let ((tag (%read-tag-token! c)))
       (%skip-flow-ws-inline! c)
       (if (or (%c-eof? c) (memv (%c-ch c) (list #\newline #\# #\{ #\[ #\' #\" #\| #\>)))
           ;; Collection, quoted/block scalar, or value on following lines:
           ;; parse normally. A quoted/block scalar's content is already
           ;; exactly the string it should be, so !!str on one is a no-op;
           ;; other tags on a non-plain-scalar value are likewise no-ops
           ;; (there's no raw numeric/bool text to reinterpret).
           (let ((value (%parse-value-inline-or-nested! c min-indent anchors flow?)))
             (if (string? value) (%apply-tag tag value value) value))
           ;; Plain scalar: read the RAW text ourselves, before implicit
           ;; type resolution, so e.g. !!str 123 can force it to stay the
           ;; string "123" instead of the already-resolved number 123.
           (let ((raw (%string-trim-right (%read-plain-line! c flow?))))
             (%apply-tag tag raw (%resolve-plain-scalar raw))))))
    ((eqv? (%c-ch c) #\{) (%parse-flow-mapping! c anchors))
    ((eqv? (%c-ch c) #\[) (%parse-flow-sequence! c anchors))
    ((eqv? (%c-ch c) #\') (%parse-single-quoted! c))
    ((eqv? (%c-ch c) #\") (%parse-double-quoted! c))
    ((and (not flow?) (memv (%c-ch c) (list #\| #\>))) (%parse-block-scalar! c min-indent))
    ((and (not flow?) (%starts-seq-item? c)) (%parse-block-sequence! c (%c-column c) anchors))
    ((and (not flow?) (%line-has-mapping-colon? c)) (%parse-block-mapping! c (%c-column c) anchors))
    (flow? (%resolve-plain-scalar (%string-trim-right (%read-plain-line! c #t))))
    (else (%resolve-plain-scalar (%parse-plain-block-scalar! c min-indent)))))

(define (%skip-flow-ws-inline! c)
  (let loop () (when (memv (%c-ch c) (list #\space #\tab)) (%c-adv! c) (loop))))

(define (%parse-block-sequence! c indent anchors)
  (let loop ((acc '()))
    (%skip-to-content! c)
    (if (or (%c-eof? c) (%at-doc-marker? c) (not (= (%c-column c) indent)) (not (%starts-seq-item? c)))
        (reverse acc)
        (begin
          (%c-adv! c) ; consume '-'
          (cond
            ((or (%c-eof? c) (memv (%c-ch c) (list #\newline #\tab)))
             (%c-skip-line! c)
             (%skip-to-content! c)
             (if (or (%c-eof? c) (%at-doc-marker? c) (<= (%c-column c) indent))
                 (loop (cons yaml-null acc))
                 (loop (cons (%parse-node c (+ indent 1) anchors) acc))))
            (else
             (%skip-flow-ws-inline! c)
             (cond
               ((or (%c-eof? c) (char=? (%c-ch c) #\newline))
                (%skip-to-content! c)
                (if (or (%c-eof? c) (%at-doc-marker? c) (<= (%c-column c) indent))
                    (loop (cons yaml-null acc))
                    (loop (cons (%parse-node c (+ indent 1) anchors) acc))))
               ;; A block scalar's content is indented relative to the
               ;; enclosing sequence's own indent (the '-'), not relative to
               ;; where '|'/'>' itself sits — same relationship as a mapping
               ;; key's own indent, not its value's column. Dispatching this
               ;; through the generic min-indent (which for anything else
               ;; here is the *value's* column, correctly) would make
               ;; %parse-block-scalar! require content indented past the
               ;; indicator itself, which conventional "- |\n  text" style
               ;; never is — silently producing an empty scalar and
               ;; corrupting everything parsed after it.
               ((memv (%c-ch c) (list #\| #\>))
                (loop (cons (%parse-block-scalar! c indent) acc)))
               (else
                (let ((item-col (%c-column c)))
                  (loop (cons (%parse-node c item-col anchors) acc)))))))))))

(define (%apply-merge-keys alist)
  (let ((merged '()) (own '()))
    (for-each
      (lambda (kv)
        (if (equal? (car kv) "<<")
            (let ((src (cdr kv)))
              (cond
                ;; Multi-source form, <<: [*a, *b] — src is a list of
                ;; alists (its first element's own car is itself a pair,
                ;; i.e. a key/value pair, rather than a scalar key):
                ;; flatten each source alist's entries in turn.
                ((and (pair? src) (pair? (car src)) (pair? (caar src)))
                 (for-each
                   (lambda (sub-alist)
                     (for-each (lambda (p) (set! merged (append merged (list p)))) sub-alist))
                   src))
                ;; Single-source form, <<: *a — src is itself one alist.
                ((pair? src)
                 (for-each (lambda (p) (set! merged (append merged (list p)))) src))
                (else 'ignore)))
            (set! own (append own (list kv)))))
      alist)
    (let ((own-keys (map car own)))
      (append own
              (%dedupe-by-car (filter (lambda (p) (not (member (car p) own-keys))) merged))))))

;; Per YAML merge-key semantics, when multiple sources are merged
;; (<<: [*a, *b, ...]) and they share a key, the earlier source wins — keep
;; only the first pair seen for each distinct key, in order.
(define (%dedupe-by-car lst)
  (let loop ((l lst) (seen '()) (acc '()))
    (cond
      ((null? l) (reverse acc))
      ((member (caar l) seen) (loop (cdr l) seen acc))
      (else (loop (cdr l) (cons (caar l) seen) (cons (car l) acc))))))

(define (%parse-block-mapping! c indent anchors)
  (let loop ((acc '()))
    (%skip-to-content! c)
    (if (or (%c-eof? c) (%at-doc-marker? c) (not (= (%c-column c) indent))
            (%starts-seq-item? c) (not (%line-has-mapping-colon? c)))
        (%apply-merge-keys (reverse acc))
        (let ((key (%parse-key! c anchors)))
          (%skip-flow-ws-inline! c)
          (if (not (eqv? (%c-ch c) #\:))
              (error "yaml: expected ':' after mapping key")
              (begin
                (%c-adv! c)
                (%skip-flow-ws-inline! c)
                (let ((value (%parse-value-inline-or-nested! c indent anchors #f)))
                  (loop (cons (cons key value) acc)))))))))

;; A mapping key is always a scalar (or anchor/alias/tag wrapping one) in
;; this module's subset — never a nested collection (see module header:
;; explicit "? key" complex keys aren't supported).
(define (%parse-key! c anchors)
  (cond
    ((eqv? (%c-ch c) #\') (%parse-single-quoted! c))
    ((eqv? (%c-ch c) #\") (%parse-double-quoted! c))
    ((eqv? (%c-ch c) #\&)
     (%c-adv! c)
     (let ((name (%read-name-token! c)))
       (%skip-flow-ws-inline! c)
       (let ((v (%parse-key! c anchors))) (hash-table-set! anchors name v) v)))
    ((eqv? (%c-ch c) #\*)
     (%c-adv! c)
     (let ((name (%read-name-token! c)))
       (if (hash-table-exists? anchors name) (hash-table-ref anchors name)
           (error (string-append "yaml: unknown alias *" name)))))
    (else (%resolve-plain-scalar (%string-trim-right (%read-plain-line! c #f))))))

;;; =========================================================================
;;; Flow collections
;;; =========================================================================

(define (%parse-flow-sequence! c anchors)
  (%c-adv! c) ; '['
  (%skip-flow-ws! c)
  (if (eqv? (%c-ch c) #\])
      (begin (%c-adv! c) '())
      (let loop ((acc '()))
        (let ((v (%parse-node* c 0 anchors #t)))
          (%skip-flow-ws! c)
          (cond
            ((eqv? (%c-ch c) #\,)
             (%c-adv! c) (%skip-flow-ws! c)
             (if (eqv? (%c-ch c) #\]) (begin (%c-adv! c) (reverse (cons v acc)))
                 (loop (cons v acc))))
            ((eqv? (%c-ch c) #\]) (%c-adv! c) (reverse (cons v acc)))
            (else (error "yaml: expected ',' or ']' in flow sequence")))))))

(define (%parse-flow-mapping! c anchors)
  (%c-adv! c) ; '{'
  (%skip-flow-ws! c)
  (if (eqv? (%c-ch c) #\})
      (begin (%c-adv! c) '())
      (let loop ((acc '()))
        (let ((k (%parse-key-flow! c anchors)))
          (%skip-flow-ws! c)
          (let ((v (if (eqv? (%c-ch c) #\:)
                       (begin (%c-adv! c) (%skip-flow-ws! c) (%parse-node* c 0 anchors #t))
                       yaml-null)))
            (%skip-flow-ws! c)
            (cond
              ((eqv? (%c-ch c) #\,)
               (%c-adv! c) (%skip-flow-ws! c)
               (if (eqv? (%c-ch c) #\}) (begin (%c-adv! c) (%apply-merge-keys (reverse (cons (cons k v) acc))))
                   (loop (cons (cons k v) acc))))
              ((eqv? (%c-ch c) #\}) (%c-adv! c) (%apply-merge-keys (reverse (cons (cons k v) acc))))
              (else (error "yaml: expected ',' or '}' in flow mapping"))))))))

(define (%parse-key-flow! c anchors)
  (cond
    ((eqv? (%c-ch c) #\') (%parse-single-quoted! c))
    ((eqv? (%c-ch c) #\") (%parse-double-quoted! c))
    (else (%resolve-plain-scalar (%string-trim-right (%read-plain-line! c #t))))))

;;; =========================================================================
;;; Document stream
;;; =========================================================================

(define (%skip-directives-and-markers! c)
  ;; Skip %YAML/%TAG directive lines and a leading '---' marker (with
  ;; anything after it on the same line left for content parsing).
  (let loop ()
    (%skip-to-content! c)
    (cond
      ((%c-eof? c) #f)
      ((eqv? (%c-ch c) #\%) (%c-skip-line! c) (loop))
      ((and (%c-looking-at? c "---") (memv (%c-ch-at c 3) (list #f #\space #\newline #\tab)))
       (%c-adv-n! c 3) (%skip-flow-ws-inline! c) #t)
      (else #t))))

(define (%at-doc-end-marker? c)
  (and (%c-looking-at? c "...") (memv (%c-ch-at c 3) (list #f #\space #\newline #\tab))))

(define (yaml-parse-all str)
  (let ((c (%mk-cursor str)) (docs '()))
    (let loop ()
      (%skip-to-content! c)
      (if (%c-eof? c)
          (reverse docs)
          (begin
            (%skip-directives-and-markers! c)
            (%skip-to-content! c)
            (if (or (%c-eof? c) (%at-doc-end-marker? c) (%c-looking-at? c "---"))
                (begin
                  (when (%at-doc-end-marker? c) (%c-skip-line! c))
                  (set! docs (cons yaml-null docs)))
                (let ((anchors (make-hash-table)))
                  ;; -1, not 0: the document root has no enclosing indent to
                  ;; exceed (real YAML treats the root's block context as
                  ;; indent level -1), so a root-level plain scalar's
                  ;; continuation lines at column 0 — the normal case, e.g.
                  ;; "first line\nsecond line" as a whole document — still
                  ;; count as "more indented than the root" and fold
                  ;; correctly instead of being truncated after the first
                  ;; line (which then left yaml-parse-all re-parsing the
                  ;; leftover text as a second, bogus document).
                  (set! docs (cons (%parse-node c -1 anchors) docs))))
            (%skip-to-content! c)
            (when (%at-doc-end-marker? c) (%c-skip-line! c))
            (loop))))))

(define (yaml-parse str)
  (let ((docs (yaml-parse-all str)))
    (if (null? docs) yaml-null (car docs))))

;;; =========================================================================
;;; Writer
;;; =========================================================================

;; An alist entry's key must itself be a scalar — checking only that each
;; element is *a* pair isn't enough, since a whole sub-alist like
;; (("k" . 1) ("j" . 2)) is also a pair, so a plain list of mappings (what
;; yaml-parse itself returns for a sequence of mappings, e.g.
;; "- name: a\n- name: b") would otherwise be misidentified as one big
;; mapping instead of a sequence.
(define (%alist? v)
  (and (pair? v)
       (let loop ((l v))
         (cond ((null? l) #t)
               ((not (pair? l)) #f)
               ((not (pair? (car l))) #f)
               ((not (%scalar-value? (caar l))) #f)
               (else (loop (cdr l)))))))

(define (%plain-safe-string? s)
  (and (> (string-length s) 0)
       (not (char-whitespace? (string-ref s 0)))
       (not (char-whitespace? (string-ref s (- (string-length s) 1))))
       (not (memv (string-ref s 0) (list #\- #\? #\: #\, #\[ #\] #\{ #\} #\# #\& #\* #\! #\| #\> #\' #\" #\% #\@ #\`)))
       (not (%string-index s #\newline 0))
       (not (%contains-mapping-colon? s))
       (not (%contains-comment-hash? s))
       (not (yaml-null? (%resolve-plain-scalar s))) ; would round-trip as null unless quoted
       (not (memv (%resolve-plain-scalar s) (list #t #f)))
       (not (number? (%resolve-plain-scalar s)))))

(define (%contains-mapping-colon? s)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (cond ((>= i n) #f)
            ((and (char=? (string-ref s i) #\:)
                  (or (= (+ i 1) n) (char-whitespace? (string-ref s (+ i 1)))))
             #t)
            (else (loop (+ i 1)))))))

(define (%contains-comment-hash? s)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (cond ((>= i n) #f)
            ((and (char=? (string-ref s i) #\#) (> i 0) (char-whitespace? (string-ref s (- i 1)))) #t)
            (else (loop (+ i 1)))))))

(define (%emit-indent n out) (let loop ((i 0)) (when (< i n) (write-char #\space out) (loop (+ i 1)))))

(define (%emit-scalar-string s out)
  (cond
    ((%string-index s #\newline 0)
     ;; multi-line string: literal block scalar for readability
     (write-string "|" out))
    ((%plain-safe-string? s) (write-string s out))
    ((not (%string-index s #\' 0))
     (write-char #\' out) (write-string s out) (write-char #\' out))
    (else
     (write-char #\" out)
     (string-for-each
       (lambda (ch)
         (cond
           ((char=? ch #\") (write-string "\\\"" out))
           ((char=? ch #\\) (write-string "\\\\" out))
           (else (write-char ch out))))
       s)
     (write-char #\" out))))

(define (%count-trailing-newlines s)
  (let loop ((i (string-length s)) (n 0))
    (if (and (> i 0) (char=? (string-ref s (- i 1)) #\newline))
        (loop (- i 1) (+ n 1))
        n)))

;; Pick the chomping indicator that makes this round-trip exactly: none
;; (clip, adds exactly one \n back) for a string with exactly one trailing
;; newline, "-" (strip, adds none back) for a string with no trailing
;; newline, "+" (keep, preserves all) for two or more. In every case, one
;; trailing newline is removed from the body before emitting each line with
;; its own "\n" — that emitted newline is what clip/keep's re-added single
;; newline accounts for, so removing more than one here would double it.
(define (%emit-literal-block s indent out)
  (let* ((trailing (%count-trailing-newlines s))
         (chomp-char (cond ((= trailing 0) "-") ((= trailing 1) "") (else "+")))
         (body (if (> trailing 0) (substring s 0 (- (string-length s) 1)) s)))
    (write-string "|" out) (write-string chomp-char out) (write-char #\newline out)
    (let ((lines (%split-lines body)))
      (for-each
        (lambda (line) (%emit-indent (+ indent 2) out) (write-string line out) (write-char #\newline out))
        lines))))

(define (%split-lines s)
  (let ((n (string-length s)) (out '()) (start 0))
    (let loop ((i 0))
      (cond
        ((= i n) (reverse (cons (substring s start i) out)))
        ((char=? (string-ref s i) #\newline)
         (set! out (cons (substring s start i) out))
         (set! start (+ i 1))
         (loop (+ i 1)))
        (else (loop (+ i 1)))))))

(define (%emit-scalar v indent out)
  (cond
    ((yaml-null? v) (write-string "null" out))
    ((eq? v #t) (write-string "true" out))
    ((eq? v #f) (write-string "false" out))
    ((and (number? v) (inexact? v) (= v +inf.0)) (write-string ".inf" out))
    ((and (number? v) (inexact? v) (= v -inf.0)) (write-string "-.inf" out))
    ((and (number? v) (inexact? v) (not (= v v))) (write-string ".nan" out))
    ((number? v) (write-string (number->string v) out))
    ((symbol? v) (%emit-scalar-string (symbol->string v) out))
    ((string? v)
     (if (%string-index v #\newline 0)
         (%emit-literal-block v indent out)
         (%emit-scalar-string v out)))
    (else (error "yaml-stringify: unsupported scalar type"))))

(define (%scalar-value? v)
  (or (yaml-null? v) (boolean? v) (number? v) (string? v) (symbol? v)))

;; An empty collection ("[]"/"{}") must be emitted inline like a scalar,
;; never on its own unindented following line — "key:\n[]" is not valid
;; YAML for "key" mapping to an empty sequence (the "[]" wouldn't be
;; recognized as belonging to "key" on reparse, since it has no indent of
;; its own to associate it with the parent mapping).
(define (%inline-value? v)
  (or (%scalar-value? v)
      (and (list? v) (null? v))
      (and (vector? v) (= (vector-length v) 0))
      (and (hash-table? v) (= (hash-table-size v) 0))))

(define (%emit-node v indent out)
  (cond
    ((%scalar-value? v) (%emit-scalar v indent out))
    ((hash-table? v) (%emit-mapping (hash-table->alist v) indent out))
    ((%alist? v) (%emit-mapping v indent out))
    ((vector? v) (%emit-sequence (vector->list v) indent out))
    ((list? v) (%emit-sequence v indent out))
    (else (error "yaml-stringify: unsupported value type"))))

;; Every entry — including the first — starts on its own fresh line at
;; `indent` spaces. The caller (top-level yaml-stringify, or the parent
;; mapping/sequence emitter right before this call) always leaves the
;; output positioned at the start of a line, so this is never skippable.
(define (%emit-mapping alist indent out)
  (if (null? alist)
      (write-string "{}" out)
      (let loop ((kvs alist))
        (unless (null? kvs)
          (%emit-indent indent out)
          (let ((k (caar kvs)) (v (cdar kvs)))
            (%emit-scalar k indent out)
            (write-char #\: out)
            (if (%inline-value? v)
                (begin (write-char #\space out) (%emit-node v indent out) (write-char #\newline out))
                (begin (write-char #\newline out) (%emit-node v (+ indent 2) out))))
          (loop (cdr kvs))))))

(define (%emit-sequence items indent out)
  (if (null? items)
      (write-string "[]" out)
      (let loop ((xs items))
        (unless (null? xs)
          (%emit-indent indent out)
          (write-string "- " out)
          (let ((v (car xs)))
            (if (%inline-value? v)
                (begin (%emit-node v (+ indent 2) out) (write-char #\newline out))
                ;; A nested mapping/sequence value always starts its own
                ;; emit-* call assuming the cursor is at the start of a
                ;; fresh line (see %emit-mapping's doc comment) — "- " left
                ;; the cursor mid-line, so a newline is needed here first,
                ;; matching %emit-mapping's parallel branch below.
                (begin (write-char #\newline out) (%emit-node v (+ indent 2) out))))
          (loop (cdr xs))))))

(define (yaml-stringify value)
  (let ((out (open-output-string)))
    (if (%scalar-value? value)
        (begin (%emit-scalar value 0 out) (write-char #\newline out))
        (%emit-node value 0 out))
    (get-output-string out)))

;;; =========================================================================
;;; File convenience wrappers
;;; =========================================================================

(define (%port->string p)
  (let ((out (open-output-string)))
    (let loop ()
      (let ((ch (read-char p)))
        (unless (eof-object? ch) (write-char ch out) (loop))))
    (get-output-string out)))

(define (yaml-load-file path)
  (call-with-input-file path (lambda (p) (yaml-parse (%port->string p)))))

(define (yaml-load-file-all path)
  (call-with-input-file path (lambda (p) (yaml-parse-all (%port->string p)))))

(define (yaml-dump-file value path)
  (call-with-output-file path (lambda (p) (write-string (yaml-stringify value) p))))
