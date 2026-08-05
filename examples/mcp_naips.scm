;;; examples/mcp_naips.scm — Airservices Australia NAIPS briefing MCP server
;;; Version: 1.0
;;;
;;; Exposes the (curry naips) briefing family as MCP tools, so an LLM client
;;; can pull real Australian aeronautical weather/NOTAM briefings and get
;;; back both the parsed (curry aviation-weather) structure and the raw
;;; report text.
;;;
;;; Tools:
;;;   loc_briefing   — METAR/TAF/ATIS(/NOTAM) for up to 12 locations
;;;   area_briefing  — same, for a whole briefing area (7xxx/8xxx/9xxx code)
;;;   met_briefing   — briefing restricted to specific MET message types
;;;   notam_briefing — NOTAM summary for one location/area
;;;
;;; Credentials: read once from the environment at startup (NAIPS_REQUESTOR /
;;; NAIPS_PASSWORD), never taken as a tool argument — so a password never
;;; flows through a tool call, an MCP client's call log, or an LLM's context.
;;;
;;; Usage:
;;;   NAIPS_REQUESTOR=... NAIPS_PASSWORD=... ./build/curry examples/mcp_naips.scm
;;;
;;; Claude Code config (~/.claude.json):
;;;   { "mcpServers": { "curry-naips": {
;;;       "command": "/path/to/build/curry",
;;;       "args":    ["/path/to/examples/mcp_naips.scm"],
;;;       "env":     { "NAIPS_REQUESTOR": "...", "NAIPS_PASSWORD": "..." } } } }

(import (curry mcp) (curry naips) (curry aviation-weather))

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
      (if (null? (naips-briefing-products b))
          "Status: SUCCESS (no products returned)"
          (apply string-append
                 "Status: SUCCESS\n\n"
                 (map fmt-product (naips-briefing-products b))))))

;;; ---- Tool handlers ----

(define (tool-loc-briefing args)
  (let* ((locations (arg args 'locations))
         (notams (eq? (arg? args 'notams #f) #t))
         (flags (list (cons "met" #t) (cons "ntm" notams))))
    (mcp-text (fmt-briefing (naips-loc-briefing *requestor* *password* locations flags)))))

(define (tool-area-briefing args)
  (let* ((areas (arg args 'areas))
         (notams (eq? (arg? args 'notams #f) #t))
         (flags (list (cons "met" #t) (cons "ntm" notams))))
    (mcp-text (fmt-briefing (naips-area-briefing *requestor* *password* areas flags)))))

(define (tool-met-briefing args)
  (let ((locations (arg args 'locations))
        (message-types (arg args 'message_types)))
    (mcp-text (fmt-briefing (naips-met-briefing *requestor* *password* locations message-types)))))

(define (tool-notam-briefing args)
  (let ((entity-id (arg args 'entity_id)))
    (mcp-text (fmt-briefing (naips-notam-briefing *requestor* *password* entity-id)))))

;;; ---- Tools ----

(mcp-tool "loc_briefing"
  "Location briefing: METAR/TAF/ATIS (and optionally NOTAMs) for up to 12
locations or airspace entities, e.g. [\"YSSY\"]. Each report is returned
both as raw text and, for METAR/TAF/ATIS, already parsed into structured
fields (wind, visibility, cloud, temperature, altimeter, ...)."
  '((locations . ((type . "array") (description . "1-12 location/airspace names, e.g. [\"YSSY\", \"YMML\"]")))
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
message_types must be a subset of: TAF, ADWRNG, METAR, SPECI, WSWRNG, AQNH,
SIGMET, AIRMET, ATIS."
  '((locations     . ((type . "array") (description . "1-4 location/airspace names")))
    (message_types . ((type . "array") (description . "e.g. [\"METAR\", \"TAF\"]"))))
  tool-met-briefing)

(mcp-tool "notam_briefing"
  "NOTAM summary briefing for one location or area (2-5 alphanumeric
characters, e.g. \"YSSY\"). Returned as raw text only — (curry
aviation-weather) does not structurally parse NOTAM text."
  '((entity_id . ((type . "string") (description . "Location or area identifier, e.g. \"YSSY\""))))
  tool-notam-briefing)

;;; ---- Serve ----

(mcp-serve "curry-naips" "1.0")
