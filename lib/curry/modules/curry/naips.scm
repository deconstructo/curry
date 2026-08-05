;;; (curry naips) — Airservices Australia NAIPS briefing-service client.
;;;
;;; Talks to the public "briefing-service" SOAP endpoint
;;; (https://www.airservicesaustralia.com/naips/briefing-service) and turns a
;;; briefing response into the record types already defined by (curry
;;; aviation-weather) — no separate XML library was added to build this.
;;; See docs/reference/module-naips.md for why: every operation covered here
;;; (location/area/MET/NOTAM briefing) shares one response schema (pulled
;;; from the service's own published WSDL/XSD, not guessed) with exactly one
;;; level of nesting that matters — a flat list of <product type="TEXT">
;;; <content>BASE64</content></product> elements, each holding one whole
;;; METAR/TAF/ATIS/NOTAM as base64 (so the payload itself can never contain
;;; '<' or break the surrounding markup) — plus a top-level status attribute
;;; and an optional error string. A handful of small, anchored (curry regex)
;;; patterns is enough to pull that apart; a real, general-purpose XML
;;; parser (DTDs, mixed content, arbitrary nesting, namespace-aware
;;; querying) would be solving a much bigger problem than this one response
;;; shape actually has. Base64 decoding is (curry base64)'s
;;; base64-decode-string, not hand-rolled here.
;;;
;;; Scope: the four *briefing* operations (loc-brief/area-brief/met-brief/
;;; notam-brief), which all extend the WSDL's BriefingRequest/BriefingResponse
;;; types and so share the request/response shape this module already knows
;;; how to build and parse. The WSDL has ~40 other operations (chart
;;; retrieval, SPFIB flight-plan templates, NOTAM proposal submission, RAIM,
;;; wind/temp profiles, first/last light, ...) with their own request/
;;; response shapes and, for the write operations, real account-state
;;; consequences — deliberately not covered here.
;;;
;;; notam-brief's request type offers two mutually exclusive modes: a
;;; "summary" briefing keyed by a plain location/area EntityId (what this
;;; module supports — the same kind of code loc-brief/area-brief take), or
;;; a "history" lookup keyed by a full NOTAM identifier, which itself
;;; requires a compound EntityKey (id + FIR + entity-type) most callers
;;; won't have on hand without first browsing a NOTAM directory (also out of
;;; scope) — not supported here.
;;;
;;; Requires a NAIPS account (requestor id + password) — this module only
;;; speaks the wire protocol, it does not manage or store credentials.

(define-library (curry naips)
  (import (scheme base) (curry regex) (curry http) (curry base64) (curry aviation-weather))
  (export
    naips-loc-briefing naips-area-briefing naips-met-briefing naips-notam-briefing
    naips-build-loc-brief-request naips-build-area-brief-request
    naips-build-met-brief-request naips-build-notam-brief-request
    naips-parse-briefing-response
    naips-briefing? naips-briefing-status naips-briefing-info
    naips-briefing-content naips-briefing-products naips-briefing->alist
    naips-product? naips-product-type naips-product-report-kind
    naips-product-text naips-product-parsed naips-product->alist)
  (begin

;;; =========================================================================
;;; Small helpers: XML escaping, namespace-prefix stripping, base64 decode.
;;; None of these are general-purpose — each is tuned to exactly what this
;;; one family of SOAP request/response shapes needs.
;;; =========================================================================

(define (%xml-escape s)
  (let ((out (open-output-string)))
    (string-for-each
      (lambda (c)
        (cond ((char=? c #\&) (write-string "&amp;" out))
              ((char=? c #\<) (write-string "&lt;" out))
              ((char=? c #\>) (write-string "&gt;" out))
              ((char=? c #\") (write-string "&quot;" out))
              ((char=? c #\') (write-string "&apos;" out))
              (else (write-char c out))))
      s)
    (get-output-string out)))

;; Response elements are namespace-qualified by the server (the NAIPS XSD
;; declares elementFormDefault="qualified"), but which prefix it picks
;; (ns0/ns1/SOAP-ENV/...) isn't part of the contract. Rather than teach every
;; extraction pattern below to tolerate an arbitrary optional prefix, strip
;; every "<prefix:" / "</prefix:" down to "<" / "</" once, up front, and let
;; everything after that assume unprefixed tag names. This is deliberately
;; not real namespace resolution (it doesn't check the prefix is actually
;; bound to the NAIPS namespace) — fine for a single known service's
;; responses, wrong for a general XML document.
(define %rx-ns-prefix (regex-compile "<(/?)[A-Za-z_][A-Za-z0-9_.-]*:"))
(define (%strip-namespace-prefixes xml)
  (regex-replace %rx-ns-prefix xml "<\\1" #t))

(define (%require-length-1-32 s what)
  (when (or (< (string-length s) 1) (> (string-length s) 32))
    (error (string-append what ": must be 1 to 32 characters") s)))

;; Character-class validation below is done by walking `string-length`/
;; `string-ref` (which see a curry String's real, length-prefixed extent)
;; rather than by (curry regex) — (curry regex) compiles down to POSIX
;; regcomp/regexec, which are NUL-terminated-C-string APIs, so an embedded
;; NUL byte in a Scheme string (legal in curry; curry strings are not
;; C strings) would make a regex-based check see only the bytes up to that
;; NUL, silently passing a value regex-match alone would reject. Area codes
;; and entity ids are short enough that a manual walk costs nothing.
(define (%digit-char? c) (and (char>=? c #\0) (char<=? c #\9)))
(define (%alnum-char? c) (or (char-alphabetic? c) (char-numeric? c)))

(define (%valid-area-code? s)
  (and (= (string-length s) 4)
       (memv (string-ref s 0) '(#\7 #\8 #\9))
       (%digit-char? (string-ref s 1))
       (%digit-char? (string-ref s 2))
       (%digit-char? (string-ref s 3))))

(define (%valid-entity-id? s)
  (let ((n (string-length s)))
    (and (>= n 2) (<= n 5)
         (let loop ((i 0))
           (or (= i n)
               (and (%alnum-char? (string-ref s i)) (loop (+ i 1))))))))

;;; =========================================================================
;;; Shared SOAP plumbing
;;; =========================================================================

(define %naips-endpoint "https://www.airservicesaustralia.com/naips/briefing-service")

(define (%soap-envelope body-xml)
  (string-append
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:loc=\"http://www.airservicesaustralia.com/naips/xsd\">"
    "<SOAP-ENV:Header/>"
    "<SOAP-ENV:Body>"
    body-xml
    "</SOAP-ENV:Body>"
    "</SOAP-ENV:Envelope>"))

(define (%request-open tag requestor password)
  (let ((out (open-output-string)))
    (write-string "<loc:" out) (write-string tag out)
    (write-string " source=\"curry\" requestor=\"" out)
    (write-string (%xml-escape requestor) out)
    (write-string "\" password=\"" out)
    (write-string (%xml-escape password) out)
    (write-string "\">" out)
    (get-output-string out)))

(define (%request-close tag)
  (string-append "</loc:" tag ">"))

(define (%el tag value)
  (string-append "<loc:" tag ">" (%xml-escape value) "</loc:" tag ">"))

;; A silently-ignored flag (a typo, or a flag valid for a different
;; operation) would produce a briefing request missing a product category
;; the caller thought they'd asked for, with no diagnostic at all — so an
;; unrecognized name, or the same name given twice, raises rather than
;; being dropped or emitted as a duplicate (and therefore invalid) XML
;; attribute.
(define (%build-flags-xml flags allowed-names)
  (let ((out (open-output-string)) (seen '()))
    (write-string "<loc:flags" out)
    (for-each
      (lambda (kv)
        (unless (member (car kv) allowed-names)
          (error "naips: unknown flag name for this operation" (car kv) "allowed:" allowed-names))
        (when (member (car kv) seen)
          (error "naips: flag name given more than once" (car kv)))
        (set! seen (cons (car kv) seen))
        (write-string " " out)
        (write-string (car kv) out)
        (write-string "=\"" out)
        (write-string (if (cdr kv) "true" "false") out)
        (write-string "\"" out))
      flags)
    (write-string "/>" out)
    (get-output-string out)))

;; POSTs a pre-built SOAP envelope and returns the raw XML response body.
;; Raises a Scheme error on a non-2xx HTTP response; a well-formed but
;; unsuccessful NAIPS response (bad credentials, unknown location, ...) is
;; not an HTTP error and is returned as-is for naips-parse-briefing-response
;; to classify via its own `status` attribute.
(define (%soap-post envelope)
  (let* ((res (http-request "POST" %naips-endpoint
                             '(("Content-Type" . "text/xml; charset=utf-8")
                               ("SOAPAction" . "\"\""))
                             envelope))
         (http-status (car res))
         (xml (cdr res)))
    (if (or (< http-status 200) (>= http-status 300))
        (error "naips: HTTP request failed" http-status xml)
        xml)))

;;; =========================================================================
;;; Response records
;;; =========================================================================

(define-record-type <naips-product>
  (%make-naips-product type report-kind text parsed)
  naips-product?
  (type naips-product-type)               ; "TEXT" / "GIF" / "PDF" / "PNG" / "JPEG"
  (report-kind naips-product-report-kind) ; 'taf / 'metar / 'atis / 'other / #f (non-text)
  (text naips-product-text)               ; decoded text (TEXT products only), or #f
  (parsed naips-product-parsed))          ; taf-report/metar-report/atis-report, or #f

(define (naips-product->alist p)
  (list (cons "type" (naips-product-type p))
        (cons "report-kind" (let ((k (naips-product-report-kind p))) (and k (symbol->string k))))
        (cons "text" (naips-product-text p))
        (cons "parsed"
              (let ((parsed (naips-product-parsed p)))
                (cond ((not parsed) #f)
                      ((taf-report? parsed) (taf-report->alist parsed))
                      ((metar-report? parsed) (metar-report->alist parsed))
                      ((atis-report? parsed) (atis-report->alist parsed))
                      (else #f))))))

(define-record-type <naips-briefing>
  (%make-naips-briefing status info content products)
  naips-briefing?
  (status naips-briefing-status)     ; "SUCCESS" / "ERROR" / "INVALID" / "ACCESS_VIOLATION" / ...
  (info naips-briefing-info)         ; string, or #f (present iff status isn't SUCCESS)
  (content naips-briefing-content)   ; the whole briefing as one text blob, or #f
  (products naips-briefing-products)) ; list of <naips-product>, in briefing order

(define (naips-briefing->alist b)
  (list (cons "status" (naips-briefing-status b))
        (cons "info" (naips-briefing-info b))
        (cons "content" (naips-briefing-content b))
        (cons "products" (map naips-product->alist (naips-briefing-products b)))))

;;; =========================================================================
;;; Response parsing — shared by all four briefing operations, since
;;; LocationBriefingRsp/AreaBriefingRsp/METBriefingRsp/NOTAMBriefingRsp are
;;; all bare extensions of the same BriefingResponse type in the XSD (same
;;; status attribute, same optional info/content/product shape); only the
;;; wrapper element's own name differs, and this parser never needs that
;;; name — it finds the first <content> in document order for the
;;; top-level text blob (schema guarantees it precedes any <product>), and
;;; every <product> for the per-report list.
;;; =========================================================================

;; %rx-status/%rx-product-type require a whitespace boundary before the
;; attribute name (always present in valid XML — an attribute is never the
;; first thing after '<', it's always preceded by the tag name or another
;; attribute's value, separated by whitespace) rather than matching
;; "status="/"type=" as a bare substring, which could otherwise misfire on
;; some other attribute merely ending in those letters (e.g. a hypothetical
;; "httpstatus=..."). Every other element-content pattern in this module is
;; naturally anchored by its literal "<"/">" delimiters; these two attribute
;; patterns are the only ones that needed an explicit boundary added.
(define %rx-status (regex-compile "[ \t\r\n]status=\"([A-Za-z_]+)\""))
(define %rx-info (regex-compile "<info>([^<]*)</info>"))
(define %rx-content (regex-compile "<content>([^<]*)</content>"))
(define %rx-product (regex-compile "<product([^>]*)>[ \t\r\n]*<content>([^<]*)</content>[ \t\r\n]*</product>"))
(define %rx-product-type (regex-compile "[ \t\r\n]type=\"([A-Za-z]+)\""))
(define %rx-fault (regex-compile "<faultstring>([^<]*)</faultstring>"))

;; Repeatedly matches `rx` against `text` left to right, returning one list
;; of captured group strings per non-overlapping match (group 0 excluded).
;; (curry regex) only exposes single-shot matching, so the "find every
;; occurrence" loop lives here rather than in the regex module itself —
;; this module is the only place in curry that currently needs it.
(define (%regex-find-all rx text)
  (let ((n (string-length text)))
    (let loop ((start 0) (acc '()))
      (if (> start n)
          (reverse acc)
          (let* ((rest (substring text start n))
                 (m (regex-match rx rest)))
            (if (not m)
                (reverse acc)
                (let* ((whole (car m))
                       (mstart (car whole)) (mend (cdr whole))
                       (groups (map (lambda (p) (and (pair? p) (substring rest (car p) (cdr p))))
                                    (cdr m))))
                  (loop (+ start (max mend (+ mstart 1))) (cons groups acc)))))))))

;; Classifies decoded TEXT-product content by its leading token, using the
;; same report-type keywords (curry aviation-weather) itself checks for.
;; ATIS reports have no distinguishing leading keyword (they start with a
;; bare HHMMZ time), so it's the fallback once TAF/METAR/SPECI are ruled
;; out, rather than a positive match. Anything else (NOTAM text, SIGMET/
;; AIRMET, ...) is 'other — (curry aviation-weather) has no parser for it,
;; so it's exposed as raw text only.
(define (%classify-product-text text)
  (let ((trimmed (%trim-leading-ws text)))
    (cond
      ((%starts-with? trimmed "TAF") 'taf)
      ((or (%starts-with? trimmed "METAR") (%starts-with? trimmed "SPECI")) 'metar)
      ((%looks-like-atis? trimmed) 'atis)
      (else 'other))))

(define (%trim-leading-ws s)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (if (and (< i n) (memv (string-ref s i) '(#\space #\tab #\newline #\return)))
          (loop (+ i 1))
          (substring s i n)))))

(define (%starts-with? s prefix)
  (let ((pn (string-length prefix)))
    (and (>= (string-length s) pn) (string=? (substring s 0 pn) prefix))))

;; ATIS text (per the observed NAIPS format) opens with an HHMMZ timestamp:
;; four digits then 'Z'. That's distinctive enough among the report kinds
;; this module knows how to classify once TAF/METAR/SPECI are ruled out.
(define %rx-atis-lead (regex-compile "^[0-9]{4}Z"))
(define (%looks-like-atis? s) (and (regex-match %rx-atis-lead s) #t))

(define (%parse-product-text kind text)
  (case kind
    ((taf) (taf-parse text))
    ((metar) (metar-parse text))
    ((atis) (atis-parse text))
    (else #f)))

(define (%parse-product groups)
  (let* ((attrs (car groups))
         (b64 (cadr groups))
         (type-m (regex-match-string %rx-product-type attrs))
         (type (if type-m (cadr type-m) "TEXT")))
    (if (string=? type "TEXT")
        (let* ((text (base64-decode-string b64))
               (kind (%classify-product-text text)))
          (%make-naips-product type kind text (%parse-product-text kind text)))
        (%make-naips-product type #f #f #f))))

(define (%toplevel-content xml)
  (let ((all (%regex-find-all %rx-content xml)))
    (if (null? all) #f (car (car all)))))

;; (naips-parse-briefing-response raw-xml) -> <naips-briefing>
;;
;; Parses any of the four briefing-family SOAP responses (loc-brief-rsp/
;; area-brief-rsp/met-brief-rsp/notam-brief-rsp) into a <naips-briefing>
;; record. Exported so a response captured elsewhere (a proxy, a fixture, a
;; saved log) can be parsed without making a network call — this is also
;; how the test suite exercises response parsing without a live account.
(define (naips-parse-briefing-response raw-xml)
  (let* ((xml (%strip-namespace-prefixes raw-xml))
         (fault-m (regex-match-string %rx-fault xml)))
    (if fault-m
        (error "naips: SOAP fault" (cadr fault-m))
        (let* ((status-m (regex-match-string %rx-status xml))
               (status (if status-m (cadr status-m) "ERROR"))
               (info-m (regex-match-string %rx-info xml))
               (info (and info-m (cadr info-m)))
               (content (%toplevel-content xml))
               (product-groups (%regex-find-all %rx-product xml))
               (products (map %parse-product product-groups)))
          (%make-naips-briefing status info content products)))))

;;; =========================================================================
;;; loc-brief — location/airspace briefing (METAR/TAF/ATIS/NOTAM for up to
;;; 12 locations)
;;; =========================================================================

(define %loc-flag-names '("met" "ntm" "hon" "sigmet" "charts" "reference"))

;; (naips-build-loc-brief-request requestor password locations flags) -> string
(define (naips-build-loc-brief-request requestor password locations flags)
  (%soap-envelope
    (string-append
      (%request-open "loc-brief-rqs" requestor password)
      (apply string-append (map (lambda (l) (%el "loc" l)) locations))
      (%build-flags-xml flags %loc-flag-names)
      (%request-close "loc-brief-rqs"))))

;; (naips-loc-briefing requestor password locations [flags]) -> <naips-briefing>
;;
;; locations: list of 1-12 location/airspace name strings (e.g. '("YSSY")).
;; flags: alist of booleans among %loc-flag-names; defaults to MET products
;;   only (METAR/TAF/ATIS) — pass '(("met" . #t) ("ntm" . #t)) to also
;;   include NOTAMs in `products` (as 'other; (curry aviation-weather)
;;   doesn't parse NOTAM text).
(define (naips-loc-briefing requestor password locations . rest)
  (let ((flags (if (pair? rest) (car rest) '(("met" . #t)))))
    (when (or (null? locations) (> (length locations) 12))
      (error "naips-loc-briefing: locations must be a list of 1 to 12 names" locations))
    (for-each (lambda (l) (%require-length-1-32 l "naips-loc-briefing: location")) locations)
    (naips-parse-briefing-response
      (%soap-post (naips-build-loc-brief-request requestor password locations flags)))))

;;; =========================================================================
;;; area-brief — briefing for a whole FIR-defined area (7xxx/8xxx/9xxx code)
;;; =========================================================================

(define %area-flag-names '("met" "ntm" "hon" "charts" "reference"))

;; (naips-build-area-brief-request requestor password areas flags) -> string
(define (naips-build-area-brief-request requestor password areas flags)
  (%soap-envelope
    (string-append
      (%request-open "area-brief-rqs" requestor password)
      (apply string-append (map (lambda (a) (%el "area" a)) areas))
      (%build-flags-xml flags %area-flag-names)
      (%request-close "area-brief-rqs"))))

;; (naips-area-briefing requestor password areas [flags]) -> <naips-briefing>
;;
;; areas: list of 1-5 area codes, each 4 digits starting with 7/8/9 (the
;;   "7 series"/"9 series" briefing areas NAIPS defines — see the service's
;;   own area directory for the codes valid for your account).
;; flags: alist of booleans among %area-flag-names; defaults to MET only.
(define (naips-area-briefing requestor password areas . rest)
  (let ((flags (if (pair? rest) (car rest) '(("met" . #t)))))
    (when (or (null? areas) (> (length areas) 5))
      (error "naips-area-briefing: areas must be a list of 1 to 5 area codes" areas))
    (for-each
      (lambda (a)
        (unless (%valid-area-code? a)
          (error "naips-area-briefing: area code must be 4 digits starting with 7, 8, or 9" a)))
      areas)
    (naips-parse-briefing-response
      (%soap-post (naips-build-area-brief-request requestor password areas flags)))))

;;; =========================================================================
;;; met-brief — briefing restricted to specific MET message types
;;; =========================================================================

(define %met-message-types '("TAF" "ADWRNG" "METAR" "SPECI" "WSWRNG" "AQNH" "SIGMET" "AIRMET" "ATIS"))

;; (naips-build-met-brief-request requestor password locations message-types) -> string
(define (naips-build-met-brief-request requestor password locations message-types)
  (%soap-envelope
    (string-append
      (%request-open "met-brief-rqs" requestor password)
      (apply string-append (map (lambda (l) (%el "loc" l)) locations))
      "<loc:types>"
      (apply string-append (map (lambda (ty) (%el "message-type" ty)) message-types))
      "</loc:types>"
      (%request-close "met-brief-rqs"))))

;; (naips-met-briefing requestor password locations message-types) -> <naips-briefing>
;;
;; locations: list of 1-4 location/airspace name strings.
;; message-types: list of 1+ strings from %met-message-types, e.g.
;;   '("METAR" "TAF" "ATIS").
(define (naips-met-briefing requestor password locations message-types)
  (when (or (null? locations) (> (length locations) 4))
    (error "naips-met-briefing: locations must be a list of 1 to 4 names" locations))
  (for-each (lambda (l) (%require-length-1-32 l "naips-met-briefing: location")) locations)
  (when (null? message-types)
    (error "naips-met-briefing: message-types must be non-empty" message-types))
  (for-each
    (lambda (ty)
      (unless (member ty %met-message-types)
        (error "naips-met-briefing: unknown MET message type" ty "must be one of" %met-message-types)))
    message-types)
  (naips-parse-briefing-response
    (%soap-post (naips-build-met-brief-request requestor password locations message-types))))

;;; =========================================================================
;;; notam-brief — NOTAM summary for a single location/area (see the module
;;; header comment: "history" mode is not supported)
;;; =========================================================================

;; (naips-build-notam-brief-request requestor password entity-id) -> string
(define (naips-build-notam-brief-request requestor password entity-id)
  (%soap-envelope
    (string-append
      (%request-open "notam-brief-rqs" requestor password)
      (%el "summary" entity-id)
      (%request-close "notam-brief-rqs"))))

;; (naips-notam-briefing requestor password entity-id) -> <naips-briefing>
;;
;; entity-id: a location or area identifier, 2-5 alphanumeric characters
;;   (e.g. "YSSY"). Requests the NOTAM summary briefing for that entity.
(define (naips-notam-briefing requestor password entity-id)
  (unless (%valid-entity-id? entity-id)
    (error "naips-notam-briefing: entity-id must be 2 to 5 alphanumeric characters" entity-id))
  (naips-parse-briefing-response
    (%soap-post (naips-build-notam-brief-request requestor password entity-id))))

  )) ;; end begin, define-library
