;;; examples/naips/mcp_naips.scm — Airservices Australia NAIPS briefing MCP server
;;; Version: 1.0
;;;
;;; Exposes the (curry naips) briefing family as MCP tools, so an LLM client
;;; can pull real Australian aeronautical weather/NOTAM briefings and get
;;; back both the parsed (curry aviation-weather) structure and the raw
;;; report text.
;;;
;;; Tools:
;;;   loc_briefing       — METAR/TAF/ATIS(/NOTAM) for up to 12 locations
;;;                        (ICAO code or plain airport name — see
;;;                        (curry airports); an ambiguous name is rejected
;;;                        with the candidate ICAO codes rather than guessed)
;;;   area_briefing      — same, for a whole briefing area (7xxx/8xxx/9xxx code)
;;;   met_briefing       — briefing restricted to specific MET message types
;;;                        (locations accept names the same way as loc_briefing)
;;;   notam_briefing     — NOTAM summary for one location/area
;;;   general_met_dir    — directory of "general" MET bulletins (not tied to
;;;                        an ICAO location or area code) currently published
;;;   general_met_briefing — fetch one bulletin's content by (name, type)
;;;                        taken from a general_met_dir entry
;;;   area_code_lookup    — look up cached Area Briefing codes (7xxx/8xxx/
;;;                        9xxx) covering a given location or tag
;;;   area_code_cache_add — record a confirmed area-code mapping into the
;;;                        cache for future lookups
;;;
;;; Credentials: read once from the environment at startup (NAIPS_REQUESTOR /
;;; NAIPS_PASSWORD), never taken as a tool argument — so a password never
;;; flows through a tool call, an MCP client's call log, or an LLM's context.
;;;
;;; Usage:
;;;   NAIPS_REQUESTOR=... NAIPS_PASSWORD=... ./build/curry examples/naips/mcp_naips.scm
;;;
;;; Claude Code config (~/.claude.json):
;;;   { "mcpServers": { "curry-naips": {
;;;       "command": "/path/to/build/curry",
;;;       "args":    ["/path/to/examples/naips/mcp_naips.scm"],
;;;       "env":     { "NAIPS_REQUESTOR": "...", "NAIPS_PASSWORD": "..." } } } }

(import (curry mcp) (curry naips) (curry aviation-weather)
        (curry okf) (curry regex) (curry airports) (srfi 19) (srfi 1))

;;; ---- Credentials ----

(define *requestor* (get-environment-variable "NAIPS_REQUESTOR"))
(define *password*  (get-environment-variable "NAIPS_PASSWORD"))

(unless (and *requestor* *password*)
  (error "mcp_naips: set NAIPS_REQUESTOR and NAIPS_PASSWORD in the environment before starting this server"))

;;; ---- Argument helpers ----

(define (arg args name)
  (let ((p (assq name args)))
    (if p (cdr p) (error "missing argument" name))))

(define (arg? args name default)
  (let ((p (assq name args)))
    (if p (cdr p) default)))

;; Lets loc_briefing/met_briefing take a human name ("the oaks") as well as
;; an ICAO code ("YOAS") — an exact ICAO match always wins over a name
;; search, so a location that happens to also be a substring of some other
;; airport's name (unlikely, given ICAO codes are 4 letters) is never
;; ambiguous. (curry airports) is the general ICAO/name directory used
;; here; it knows nothing about NAIPS area codes, so this only applies to
;; the *-briefing tools' individual-location arguments, not area_briefing's
;; 4-digit area codes or notam_briefing's location-or-area entity_id.
(define (%resolve-location loc)
  (let ((exact (airport-lookup loc)))
    (if exact
        (airport-icao exact)
        (let ((matches (airport-search loc)))
          (cond
            ((null? matches)
             (error "no known ICAO code or airport name matches" loc))
            ((null? (cdr matches)) (airport-icao (car matches)))
            (else
             (error "airport name is ambiguous — matched multiple airports; use the ICAO code instead"
                    (map (lambda (a) (list (airport-icao a) (airport-name a))) matches))))))))

;;; ---- Formatting a <naips-briefing> as readable text ----

(define (fmt-product p)
  (string-append
    "[" (naips-product-type p)
    (let ((k (naips-product-report-kind p))) (if k (string-append "/" (symbol->string k)) ""))
    "]\n"
    (or (naips-product-text p) "(non-text product, not shown)")
    "\n"))

(define (fmt-briefing b)
  (if (not (string=? (naips-briefing-status b) "SUCCESS"))
      (string-append "Status: " (naips-briefing-status b)
                      (let ((i (naips-briefing-info b))) (if i (string-append " — " i) "")))
      (cond
        ((pair? (naips-briefing-products b))
         (apply string-append
                "Status: SUCCESS\n\n"
                (map fmt-product (naips-briefing-products b))))
        ;; Some operations (e.g. loc-brief with the "met" flag) return the
        ;; whole briefing as one top-level <content> text blob with no
        ;; per-report <product> elements at all, even though the WSDL
        ;; schema allows for that shape — naips-briefing-content still has
        ;; the real report text in that case, so fall back to it rather
        ;; than reporting an empty briefing.
        ((let ((c (naips-briefing-content b))) (and c (> (string-length c) 0) c))
         => (lambda (c) (string-append "Status: SUCCESS\n\n" c)))
        (else "Status: SUCCESS (no products returned)"))))

;;; ---- Tool handlers ----

(define (tool-loc-briefing args)
  (let* ((locations (map %resolve-location (arg args 'locations)))
         (notams (eq? (arg? args 'notams #f) #t))
         (flags (list (cons "met" #t) (cons "ntm" notams))))
    (mcp-text (fmt-briefing (naips-loc-briefing *requestor* *password* locations flags)))))

(define (tool-area-briefing args)
  (let* ((areas (arg args 'areas))
         (notams (eq? (arg? args 'notams #f) #t))
         (flags (list (cons "met" #t) (cons "ntm" notams))))
    (mcp-text (fmt-briefing (naips-area-briefing *requestor* *password* areas flags)))))

(define (tool-met-briefing args)
  (let ((locations (map %resolve-location (arg args 'locations)))
        (message-types (arg args 'message_types)))
    (mcp-text (fmt-briefing (naips-met-briefing *requestor* *password* locations message-types)))))

(define (tool-notam-briefing args)
  (let ((entity-id (arg args 'entity_id)))
    (mcp-text (fmt-briefing (naips-notam-briefing *requestor* *password* entity-id)))))

(define (fmt-general-met-dir messages)
  (if (null? messages)
      "(no general MET bulletins currently published)"
      (apply string-append
             (map (lambda (m)
                    (string-append (naips-general-met-message-name m)
                                    " [" (naips-general-met-message-type m) "]\n"))
                  messages))))

;; naips-general-met-dir raises a plain Scheme error on a non-SUCCESS NAIPS
;; response (naips.scm's general-met-dir parser has no <naips-briefing>
;; record to carry a status string, unlike the four *-briefing operations
;; fmt-briefing already renders gracefully) — caught here so a bad-account
;; or transient NAIPS failure still reaches the MCP client as the same kind
;; of readable "Status: ERROR — ..." text the other tools produce, not a
;; bare JSON-RPC internal-error string.
(define (tool-general-met-dir args)
  (mcp-text
    (guard (e (#t (string-append "Status: ERROR — " (error-object-message e))))
      (fmt-general-met-dir (naips-general-met-dir *requestor* *password*)))))

(define (tool-general-met-briefing args)
  (let ((name (arg args 'name))
        (type (arg args 'type)))
    (mcp-text (fmt-briefing (naips-general-met-briefing *requestor* *password* name type)))))

;;; ---- Area-code cache ----
;;;
;;; NAIPS's Area Briefing codes (4 digits: series digit 7/8/9 + area number +
;;; sub-division digit) aren't enumerable through any operation this server
;;; calls — NAIPS only exposes the mapping via a clickable map / "Area & Sub
;;; Area Directory" widget on the website. Rather than ship a hand-copied
;;; table with this server (which would go stale silently the moment
;;; Airservices renumbers an area, with no way for this code to notice),
;;; mappings this server has actually confirmed — either from a live
;;; area_briefing response or from Airservices' own published documentation
;;; — are cached as an OKF bundle on disk, outside the repo, and grown
;;; incrementally via area_code_cache_add. A cache miss means "not learned
;;; yet", never "does not exist".
;;;
;;; One concept per area code, id "areas/<code>". Locations are stored as
;;; "loc:<CODE>" tags (make-okf-concept has no arbitrary-frontmatter-key
;;; escape hatch, but tags is exactly the free-form string list this needs)
;;; so lookup is a plain tag/membership scan; the body repeats them as a
;;; human-readable list for anyone browsing the bundle directly.

(define (%naips-cache-root)
  (or (get-environment-variable "NAIPS_OKF_CACHE_DIR")
      (string-append
        (or (get-environment-variable "XDG_CACHE_HOME")
            (string-append (get-environment-variable "HOME") "/.cache"))
        "/curry/naips-areas")))

;; #f (rather than an empty bundle) when the cache root doesn't exist yet —
;; okf-load-bundle walks a real directory, so "never written to" and "empty"
;; need to be distinguishable without an error.
(define (%naips-cache-bundle)
  (let ((root (%naips-cache-root)))
    (if (file-exists? root) (okf-load-bundle root) #f)))

(define (%area-concept-id area-code) (string-append "areas/" area-code))

(define %rx-naips-area-code (regex-compile "^[789][0-9]{3}$"))
(define (%valid-naips-area-code? s) (and (regex-match %rx-naips-area-code s) #t))

(define (%loc-tag? t) (and (>= (string-length t) 4) (string=? (substring t 0 4) "loc:")))
(define (%tag->loc t) (substring t 4 (string-length t)))
(define (%loc->tag l) (string-append "loc:" (string-upcase l)))
(define (%concept-locations c) (map %tag->loc (filter %loc-tag? (okf-concept-tags c))))

;; Existing locations read back off a concept are already uppercase (they
;; came from %loc->tag on the way in), but a caller-supplied `locations`
;; list isn't guaranteed to be — comparing "yssy" against "YSSY" in
;; %string-list-union would fail to dedupe, so normalize on the way in
;; rather than relying on every caller to have already done so.
(define (%valid-locations-list? x)
  (and (list? x)
       (let loop ((xs x))
         (or (null? xs)
             (and (string? (car xs)) (> (string-length (car xs)) 0) (loop (cdr xs)))))))

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
;; ~28,000-airport directory rather than a hand-maintained table — any
;; airport name curry knows about works here, not just a hand-picked
;; subset, and there's nothing to keep in sync by hand across files.
(define (%name-matches-concept? query c)
  (let ((locs (%concept-locations c)))
    (any (lambda (a) (member (airport-icao a) locs))
         (airport-search query))))

(define (tool-area-code-lookup args)
  (let* ((query (string-upcase (arg args 'query)))
         (bundle (%naips-cache-bundle)))
    (if (not bundle)
        (mcp-text "(area-code cache is empty — nothing has been confirmed and cached yet; use area_briefing to find a code, then area_code_cache_add to remember it)")
        (let* ((concepts (okf-concepts-by-type bundle "NAIPS Briefing Area"))
               (matches (filter
                          (lambda (c)
                            (or (member query (map string-upcase (%concept-locations c)))
                                (member query (map string-upcase (okf-concept-tags c)))
                                (%name-matches-concept? query c)))
                          concepts)))
          (mcp-text
            (if (null? matches)
                (string-append "No cached area code covers \"" query "\" yet — try area_briefing with a plausible code, then area_code_cache_add once confirmed.")
                (apply string-append
                  (map (lambda (c)
                         (string-append
                           (okf-concept-id c) "  " (or (okf-concept-title c) "") "\n"
                           "  " (or (okf-concept-description c) "") "\n"
                           "  locations: " (apply string-append (%interpose ", " (%concept-locations c))) "\n"
                           "  trust: " (symbol->string (okf-trust-tier c)) "\n\n"))
                       matches))))))))

(define (tool-area-code-cache-add args)
  (let* ((area-code (arg args 'area_code))
         (region (arg args 'region))
         (state (arg? args 'state #f))
         (locations (arg? args 'locations '()))
         (source-note (arg args 'source_note)))
    (unless (%valid-naips-area-code? area-code)
      (error "area_code_cache_add: area_code must be 4 digits starting with 7, 8, or 9" area-code))
    (unless (%valid-locations-list? locations)
      (error "area_code_cache_add: locations must be a list of non-empty strings" locations))
    (let* ((root (%naips-cache-root))
           (id (%area-concept-id area-code))
           (bundle (%naips-cache-bundle))
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
               #:verified (list (list (cons "by" "curry-naips-mcp") (cons "at" (%today))))
               #:body (string-append
                        "# Locations\n\n"
                        (apply string-append (map (lambda (l) (string-append "- " l "\n")) merged-locations))))))
      (okf-write-concept concept root id)
      (mcp-text (string-append "Cached " id " — " (number->string (length merged-locations))
                                " location(s) at " root ".")))))

;;; ---- Tools ----

(mcp-tool "loc_briefing"
  "Location briefing: METAR/TAF/ATIS (and optionally NOTAMs) for up to 12
locations or airspace entities. Each entry may be an ICAO code (e.g.
\"YSSY\") or a plain airport name (e.g. \"the oaks\") — names are resolved
against the global airport directory; an ambiguous name (matches more than
one airport) is rejected with the list of candidates, so use the ICAO code
in that case. Each report is returned both as raw text and, for
METAR/TAF/ATIS, already parsed into structured fields (wind, visibility,
cloud, temperature, altimeter, ...)."
  '((locations . ((type . "array") (description . "1-12 ICAO codes and/or airport names, e.g. [\"YSSY\", \"the oaks\"]")))
    (notams    . ((type . "boolean") (description . "Also include NOTAMs (raw text only)") (default . #f))))
  tool-loc-briefing)

(mcp-tool "area_briefing"
  "Area briefing: METAR/TAF/ATIS (and optionally NOTAMs) for a whole NAIPS
briefing area, identified by a 4-digit code starting with 7, 8, or 9
(e.g. \"7100\"). Up to 5 areas per call."
  '((areas  . ((type . "array") (description . "1-5 area codes, e.g. [\"7100\"]")))
    (notams . ((type . "boolean") (description . "Also include NOTAMs (raw text only)") (default . #f))))
  tool-area-briefing)

(mcp-tool "met_briefing"
  "MET briefing restricted to specific message types, for up to 4 locations.
Each location may be an ICAO code or a plain airport name (resolved the same
way as loc_briefing). message_types must be a subset of: TAF, ADWRNG, METAR,
SPECI, WSWRNG, AQNH, SIGMET, AIRMET, ATIS."
  '((locations     . ((type . "array") (description . "1-4 ICAO codes and/or airport names")))
    (message_types . ((type . "array") (description . "e.g. [\"METAR\", \"TAF\"]"))))
  tool-met-briefing)

(mcp-tool "notam_briefing"
  "NOTAM summary briefing for one location or area (2-5 alphanumeric
characters, e.g. \"YSSY\"). Returned as raw text only — (curry
aviation-weather) does not structurally parse NOTAM text."
  '((entity_id . ((type . "string") (description . "Location or area identifier, e.g. \"YSSY\""))))
  tool-notam-briefing)

(mcp-tool "general_met_dir"
  "Lists every 'general' aviation MET bulletin NAIPS currently publishes —
national/regional text products not tied to a specific ICAO location or
7xxx/8xxx/9xxx area code (e.g. area forecasts, national warnings summaries).
Each entry's name and type are what general_met_briefing needs to fetch its
content. Takes no arguments."
  '()
  tool-general-met-dir)

(mcp-tool "general_met_briefing"
  "Fetches one 'general' MET bulletin's content by name and type, as listed
by general_met_dir (call that first to discover valid name/type pairs)."
  '((name . ((type . "string") (description . "Bulletin name, from a general_met_dir entry")))
    (type . ((type . "string") (description . "Bulletin type, from a general_met_dir entry"))))
  tool-general-met-briefing)

(mcp-tool "area_code_lookup"
  "Looks up cached NAIPS Area Briefing codes (4-digit codes starting 7/8/9,
the kind area_briefing takes) covering a given ICAO location code or tag
(e.g. a state name like \"NSW\"). The cache is local to this server and
grows only via area_code_cache_add — a miss means no code has been
confirmed and cached yet, not that none exists. Fall back to a live
area_briefing guess or the NAIPS website's area map on a miss."
  '((query . ((type . "string") (description . "ICAO location code (e.g. \"YSCO\") or tag (e.g. \"NSW\")"))))
  tool-area-code-lookup)

(mcp-tool "area_code_cache_add"
  "Records a confirmed NAIPS Area Briefing code -> location(s) mapping in
the local cache (an OKF bundle on disk, outside the repo — see
NAIPS_OKF_CACHE_DIR), so future area_code_lookup calls can resolve it
without re-deriving it. Only call this once the mapping is actually
confirmed — e.g. an area_briefing response for area_code included the
expected location, or a specific line in NAIPS's own published
documentation says so. Merges with any existing cached entry for the same
area_code (locations are unioned) rather than overwriting it."
  '((area_code   . ((type . "string") (description . "4-digit code starting 7/8/9, e.g. \"9200\"")))
    (region      . ((type . "string") (description . "Short human description of the area, e.g. \"Hunter / North Coast / Sydney Basin, NSW\"")))
    (state       . ((type . "string") (description . "State/territory tag, e.g. \"NSW\"") (default . #f)))
    (locations   . ((type . "array") (description . "ICAO/location codes confirmed to be covered by this area") (default . ())))
    (source_note . ((type . "string") (description . "How this was confirmed, e.g. \"live area_briefing query 2026-08-07\" or a citation into NAIPS's user guide"))))
  tool-area-code-cache-add)

;;; ---- Serve ----

(mcp-serve "curry-naips" "1.0")
