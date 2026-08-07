;;; examples/naips/naips_area_cache_tool.scm — offline reader/writer for the
;;; NAIPS area-code cache
;;; Version: 1.0
;;;
;;; A CLI for inspecting and editing the same OKF cache mcp_naips.scm's
;;; area_code_lookup/area_code_cache_add tools read and grow (see that
;;; file's "Area-code cache" section for what the cache is and why it
;;; exists), without starting the MCP server or needing NAIPS_REQUESTOR/
;;; NAIPS_PASSWORD — this only ever touches the on-disk cache, never NAIPS
;;; itself. Useful for checking what's cached, or hand-editing an entry,
;;; from a plain shell.
;;;
;;; Usage:
;;;   ./build/curry examples/naips/naips_area_cache_tool.scm lookup <query>
;;;   ./build/curry examples/naips/naips_area_cache_tool.scm add <area_code> <region> <state-or--> <loc1,loc2,...-or--> <source-note>
;;;
;;;   NAIPS_OKF_CACHE_DIR=/custom/path ./build/curry examples/naips/naips_area_cache_tool.scm lookup NSW
;;;
;;; Examples:
;;;   lookup YBTL           -> matches by ICAO code
;;;   lookup Townsville     -> matches by well-known aerodrome name
;;;   lookup NSW            -> matches by state tag
;;;   add 9999 "Test area" QLD YBTL,YBMK "manual correction, 2026-08-07"
;;;   add 9999 "Test area" - YBTL "no state tag"
;;;
;;; `add` only records what you assert on the command line — same rule as
;;; area_code_cache_add: only add a mapping you've actually confirmed (a
;;; live area_briefing response, or Airservices' own published docs).

(import (scheme base) (scheme write) (curry okf) (curry regex) (curry airports) (srfi 19) (srfi 1))

(define (%cache-root)
  (or (get-environment-variable "NAIPS_OKF_CACHE_DIR")
      (string-append
        (or (get-environment-variable "XDG_CACHE_HOME")
            (string-append (get-environment-variable "HOME") "/.cache"))
        "/curry/naips-areas")))

(define (%naips-cache-bundle root)
  (if (file-exists? root) (okf-load-bundle root) #f))

(define (%area-concept-id area-code) (string-append "areas/" area-code))
(define %rx-naips-area-code (regex-compile "^[789][0-9]{3}$"))
(define (%valid-naips-area-code? s) (and (regex-match %rx-naips-area-code s) #t))

(define (%loc-tag? t) (and (>= (string-length t) 4) (string=? (substring t 0 4) "loc:")))
(define (%tag->loc t) (substring t 4 (string-length t)))
(define (%loc->tag l) (string-append "loc:" (string-upcase l)))
(define (%concept-locations c) (map %tag->loc (filter %loc-tag? (okf-concept-tags c))))

(define (%string-list-union a b)
  (let loop ((xs b) (acc a))
    (cond ((null? xs) acc)
          ((member (car xs) acc) (loop (cdr xs) acc))
          (else (loop (cdr xs) (append acc (list (car xs))))))))

(define (%interpose sep lst)
  (cond ((null? lst) '())
        ((null? (cdr lst)) lst)
        (else (cons (car lst) (cons sep (%interpose sep (cdr lst)))))))

(define (%today) (date->string (current-date) "~Y-~m-~d"))

;; Does `query` (a free-text airport-name fragment, e.g. "Townsville") name
;; any location covered by concept `c`? Uses (curry airports)'s full
;; ~28,000-airport directory — same logic as mcp_naips.scm's copy, kept in
;; sync by being the same three lines rather than a hand-maintained table.
(define (%name-matches-concept? query c)
  (let ((locs (%concept-locations c)))
    (any (lambda (a) (member (airport-icao a) locs))
         (airport-search query))))

;;; ---- lookup ----

(define (cmd-lookup query)
  (let* ((root (%cache-root))
         (bundle (%naips-cache-bundle root))
         (q (string-upcase query)))
    (if (not bundle)
        (begin (display "(cache is empty — nothing seeded/cached yet at ") (display root) (display ")") (newline))
        (let* ((concepts (okf-concepts-by-type bundle "NAIPS Briefing Area"))
               (matches (filter
                          (lambda (c)
                            (or (member q (map string-upcase (%concept-locations c)))
                                (member q (map string-upcase (okf-concept-tags c)))
                                (%name-matches-concept? q c)))
                          concepts)))
          (if (null? matches)
              (begin (display "No cached area code covers \"") (display query) (display "\".") (newline))
              (for-each
                (lambda (c)
                  (display (okf-concept-id c)) (display "  ") (display (or (okf-concept-title c) "")) (newline)
                  (display "  ") (display (or (okf-concept-description c) "")) (newline)
                  (display "  locations: ") (display (apply string-append (%interpose ", " (%concept-locations c)))) (newline)
                  (display "  trust: ") (display (okf-trust-tier c)) (newline)
                  (newline))
                matches))))))

;;; ---- add ----

;; No string-split builtin in curry — split on commas by hand.
(define (%split-on-comma s)
  (let ((len (string-length s)))
    (let loop ((i 0) (start 0) (acc '()))
      (cond ((= i len) (reverse (cons (substring s start i) acc)))
            ((char=? (string-ref s i) #\,)
             (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
            (else (loop (+ i 1) start acc))))))

;; A trailing or doubled comma ("YBTL," / "YBTL,,YBMK") would otherwise
;; silently produce an empty-string location, which then gets written into
;; the cache as a bare "loc:" tag with no ICAO — reject it here instead.
(define (%parse-locations s)
  (if (string=? s "-")
      '()
      (let ((locs (%split-on-comma s)))
        (when (any (lambda (l) (string=? l "")) locs)
          (error "add: locations must not contain empty entries (check for a trailing or doubled comma)" s))
        locs)))

(define (cmd-add area-code region state-arg locations-arg source-note)
  (unless (%valid-naips-area-code? area-code)
    (error "add: area_code must be 4 digits starting with 7, 8, or 9" area-code))
  (let* ((state (if (string=? state-arg "-") #f state-arg))
         (locations (%parse-locations locations-arg))
         (root (%cache-root))
         (id (%area-concept-id area-code))
         (bundle (%naips-cache-bundle root))
         (existing (and bundle (okf-bundle-ref bundle id)))
         (merged-locations (%string-list-union (if existing (%concept-locations existing) '())
                                                (map string-upcase locations)))
         (prior-sources (if existing (okf-concept-sources existing) '()))
         (concept
           (make-okf-concept
             #:type "NAIPS Briefing Area"
             #:title (string-append "Area " area-code (if state (string-append " (" state ")") ""))
             #:description region
             #:tags (append (list "naips-area-code")
                             (if state (list state) '())
                             (map %loc->tag merged-locations))
             #:sources (append prior-sources
                                (list (list (cons "note" source-note) (cons "checked" (%today)))))
             #:verified (list (list (cons "by" "naips_area_cache_tool.scm/1.0") (cons "at" (%today))))
             #:body (string-append
                      "# Locations\n\n"
                      (apply string-append (map (lambda (l) (string-append "- " l "\n")) merged-locations))))))
    (okf-write-concept concept root id)
    (display "cached ") (display id)
    (display " (") (display (length merged-locations)) (display " locations) at ") (display root)
    (newline)))

;;; ---- entry point ----

(define (usage)
  (display "usage:") (newline)
  (display "  naips_area_cache_tool.scm lookup <query>") (newline)
  (display "  naips_area_cache_tool.scm add <area_code> <region> <state-or--> <loc1,loc2,...-or--> <source-note>") (newline))

(let ((argv (cdr (command-line))))
  (cond
    ((and (pair? argv) (string=? (car argv) "lookup") (pair? (cdr argv)))
     (cmd-lookup (cadr argv)))
    ((and (pair? argv) (string=? (car argv) "add") (= (length (cdr argv)) 5))
     (apply cmd-add (list-tail argv 1)))
    (else (usage))))
