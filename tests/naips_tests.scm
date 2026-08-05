;;; (curry naips) tests — request building and response parsing.
;;;
;;; No network access: NAIPS requires a real account, so these tests only
;;; exercise the pure functions — SOAP request construction (escaping) and
;;; SOAP response parsing, for all four briefing operations (loc/area/met/
;;; notam) — against synthetic fixtures built directly from the service's
;;; own published XSD shape (see module-naips.md), not against the live
;;; endpoint. Response parsing is exercised once, via loc-brief's response
;;; shape, since all four operations share the same BriefingResponse parser
;;; (naips-parse-briefing-response) — the per-operation tests below focus on
;;; each operation's own request shape and input validation instead.

(import (curry naips) (curry aviation-weather))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; =========================================================================
;;; Fixture base64 payloads (each is the base64 of one plain-ASCII product
;;; body; computed offline with `base64`, not with this module, so the test
;;; doesn't validate the decoder against itself).
;;; =========================================================================

(define %b64-taf
  "VEFGIFlTU1kgMTQxMTA4WiAxNDEyLzE1MTggMjYwMDhLVCA5OTk5IEZFVzAyMCBGTTE0MTUwMCAyOTAwN0tUIDk5OTkgTlNXIEZFVzAyMCBSTUsgVCAxNiAxNSAxNCAxNSBRIDEwMTIgMTAxMSAxMDA5IDEwMTAgVEFGMw==")

(define %b64-metar
  "TUVUQVIgWVNTWSAxNDExMDBaIDI3MDA3S1QgOTk5OSBGRVcwMTggU0NUMDYwIDE3LzE1IFExMDEz")

(define %b64-atis
  "MTMyNlogWVNTWSBBUlIgQVRJUyBPCjEzMjZaIDE2MDA4S1QgOTk5OSBGRVcwMjAgMTcvMTUgUTEwMTMgQVBDSCBJTFMgUldZIDE2TA==")

(define %b64-notam
  "QTEyMzQvMjMgTk9UQU1OIFEpIFlNTU0vUU1STEMvSVYvTkJPL0EvMDAwLzk5OS8zMzUyUzE1MTEyRTAwNSBBKSBZU1NZIEIpIDIzMDEwMTAwMDAgQykgMjMwMTMxMjM1OQ==")

;;; =========================================================================
;;; A synthetic SOAP envelope in the exact shape of a real loc-brief-rsp:
;;; status attribute, top-level <content> text blob, then one <product> per
;;; TAF/METAR/ATIS/NOTAM — matching the LocationBriefingRsp/BriefingResponse
;;; complexType (see docs/reference/module-naips.md).
;;; =========================================================================

(define %success-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<ns2:loc-brief-rsp xmlns:ns2=\"http://www.airservicesaustralia.com/naips/xsd\" status=\"SUCCESS\">"
    "<content>LOCATION BRIEFING TEXT GOES HERE</content>"
    "<product type=\"TEXT\"><content>" %b64-taf "</content></product>"
    "<product type=\"TEXT\"><content>" %b64-metar "</content></product>"
    "<product type=\"TEXT\"><content>" %b64-atis "</content></product>"
    "<product type=\"TEXT\"><content>" %b64-notam "</content></product>"
    "</ns2:loc-brief-rsp>"
    "</S:Body>"
    "</S:Envelope>"))

(define %error-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<ns2:loc-brief-rsp xmlns:ns2=\"http://www.airservicesaustralia.com/naips/xsd\" status=\"INVALID_ACCOUNT\">"
    "<info>Invalid account id or password.</info>"
    "</ns2:loc-brief-rsp>"
    "</S:Body>"
    "</S:Envelope>"))

;; Same as %success-response but with the base64 payload line-wrapped, as
;; some SOAP serializers do for base64Binary content.
(define %wrapped-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<ns2:loc-brief-rsp xmlns:ns2=\"http://www.airservicesaustralia.com/naips/xsd\" status=\"SUCCESS\">"
    "<content>x</content>"
    "<product type=\"TEXT\"><content>" (substring %b64-metar 0 40) "\n" (substring %b64-metar 40 (string-length %b64-metar)) "</content></product>"
    "</ns2:loc-brief-rsp>"
    "</S:Body>"
    "</S:Envelope>"))

(define %fault-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<S:Fault>"
    "<faultcode>S:Server</faultcode>"
    "<faultstring>Internal server error</faultstring>"
    "</S:Fault>"
    "</S:Body>"
    "</S:Envelope>"))

;;; =========================================================================
;;; Response parsing — exercised directly via naips-parse-briefing-response,
;;; which takes a raw XML string, so no network round trip is needed.
;;; =========================================================================

(define b (naips-parse-briefing-response %success-response))

(check "briefing status" (naips-briefing-status b) "SUCCESS")
(check "briefing info absent on success" (naips-briefing-info b) #f)
(check "briefing top-level content" (naips-briefing-content b) "LOCATION BRIEFING TEXT GOES HERE")
(check "briefing product count" (length (naips-briefing-products b)) 4)

(define products (naips-briefing-products b))
(define p-taf (list-ref products 0))
(define p-metar (list-ref products 1))
(define p-atis (list-ref products 2))
(define p-notam (list-ref products 3))

(check "taf product type" (naips-product-type p-taf) "TEXT")
(check "taf product report-kind" (naips-product-report-kind p-taf) 'taf)
(check "taf product parsed station" (taf-station (naips-product-parsed p-taf)) "YSSY")
(check "taf product parsed group count" (length (taf-groups (naips-product-parsed p-taf))) 2)

(check "metar product report-kind" (naips-product-report-kind p-metar) 'metar)
(check "metar product parsed station" (metar-station (naips-product-parsed p-metar)) "YSSY")
(check "metar product parsed altimeter" (metar-altimeter (naips-product-parsed p-metar)) 1013)

(check "atis product report-kind" (naips-product-report-kind p-atis) 'atis)
(check "atis product parsed station" (atis-station (naips-product-parsed p-atis)) "YSSY")
(check "atis product parsed type" (atis-type (naips-product-parsed p-atis)) 'arr)
(check "atis product parsed info letter" (atis-information-letter (naips-product-parsed p-atis)) "O")

(check "notam product classified as other" (naips-product-report-kind p-notam) 'other)
(check "notam product not parsed" (naips-product-parsed p-notam) #f)
(check "notam product text decoded" (naips-product-text p-notam)
       "A1234/23 NOTAMN Q) YMMM/QMRLC/IV/NBO/A/000/999/3352S15112E005 A) YSSY B) 2301010000 C) 2301312359")

;;; Error status

(define eb (naips-parse-briefing-response %error-response))
(check "error status" (naips-briefing-status eb) "INVALID_ACCOUNT")
(check "error info" (naips-briefing-info eb) "Invalid account id or password.")
(check "error has no products" (naips-briefing-products eb) '())
(check "error has no top-level content" (naips-briefing-content eb) #f)

;;; Line-wrapped base64 still decodes correctly

(define wb (naips-parse-briefing-response %wrapped-response))
(check "wrapped base64 product count" (length (naips-briefing-products wb)) 1)
(check "wrapped base64 decodes correctly"
       (metar-station (naips-product-parsed (car (naips-briefing-products wb))))
       "YSSY")

;;; SOAP fault surfaces as a Scheme error, not a parsed briefing

(check "soap fault raises"
       (guard (e (#t 'raised))
         (naips-parse-briefing-response %fault-response)
         'not-raised)
       'raised)

;;; =========================================================================
;;; Request building: credentials/locations containing XML-special
;;; characters must not break or inject into the request markup.
;;; =========================================================================

(define (%found? hay needle) (if (string-contains hay needle) 'found 'not-found))

(define req (naips-build-loc-brief-request "us&er" "p\"w<d>" '("YSSY") '(("met" . #t))))

(check "requestor ampersand is escaped" (%found? req "us&amp;er") 'found)
(check "password ampersand is escaped" (%found? req "&amp;") 'found)
(check "password quote is escaped" (%found? req "&quot;") 'found)
(check "password less-than is escaped" (%found? req "&lt;") 'found)
(check "no raw double-quote from password leaks into markup"
       (%found? req "p\"w<d>") 'not-found)
(check "no raw '<' from password leaks into markup"
       (%found? req "w<d") 'not-found)
(check "location element present" (%found? req "<loc:loc>YSSY</loc:loc>") 'found)

(check "too many locations rejected"
       (guard (e (#t 'raised))
         (naips-loc-briefing "user1234" "pass1234" (make-list 13 "YSSY"))
         'not-raised)
       'raised)

(check "zero locations rejected"
       (guard (e (#t 'raised))
         (naips-loc-briefing "user1234" "pass1234" '())
         'not-raised)
       'raised)

;;; =========================================================================
;;; area-brief request building
;;; =========================================================================

(define area-req (naips-build-area-brief-request "user1234" "pass1234" '("7100") '(("met" . #t))))
(check "area-brief request element present" (%found? area-req "<loc:area>7100</loc:area>") 'found)
(check "area-brief request wrapper present" (%found? area-req "<loc:area-brief-rqs") 'found)

(check "area code must start with 7/8/9"
       (guard (e (#t 'raised))
         (naips-area-briefing "user1234" "pass1234" '("6100"))
         'not-raised)
       'raised)

(check "area code must be 4 digits"
       (guard (e (#t 'raised))
         (naips-area-briefing "user1234" "pass1234" '("710"))
         'not-raised)
       'raised)

(check "too many areas rejected"
       (guard (e (#t 'raised))
         (naips-area-briefing "user1234" "pass1234" (make-list 6 "7100"))
         'not-raised)
       'raised)

;;; =========================================================================
;;; met-brief request building
;;; =========================================================================

(define met-req (naips-build-met-brief-request "user1234" "pass1234" '("YSSY") '("METAR" "TAF")))
(check "met-brief request loc element present" (%found? met-req "<loc:loc>YSSY</loc:loc>") 'found)
(check "met-brief request types wrapper present" (%found? met-req "<loc:types>") 'found)
(check "met-brief request message-type present" (%found? met-req "<loc:message-type>METAR</loc:message-type>") 'found)

(check "too many met-brief locations rejected"
       (guard (e (#t 'raised))
         (naips-met-briefing "user1234" "pass1234" (make-list 5 "YSSY") '("METAR"))
         'not-raised)
       'raised)

(check "empty met-brief message-types rejected"
       (guard (e (#t 'raised))
         (naips-met-briefing "user1234" "pass1234" '("YSSY") '())
         'not-raised)
       'raised)

(check "unknown met-brief message-type rejected"
       (guard (e (#t 'raised))
         (naips-met-briefing "user1234" "pass1234" '("YSSY") '("NOT-A-TYPE"))
         'not-raised)
       'raised)

;;; =========================================================================
;;; notam-brief request building
;;; =========================================================================

(define notam-req (naips-build-notam-brief-request "user1234" "pass1234" "YSSY"))
(check "notam-brief request summary element present" (%found? notam-req "<loc:summary>YSSY</loc:summary>") 'found)

(check "notam-brief entity-id too short rejected"
       (guard (e (#t 'raised))
         (naips-notam-briefing "user1234" "pass1234" "Y")
         'not-raised)
       'raised)

(check "notam-brief entity-id too long rejected"
       (guard (e (#t 'raised))
         (naips-notam-briefing "user1234" "pass1234" "TOOLONGG")
         'not-raised)
       'raised)

;;; An embedded NUL byte must not let an otherwise-invalid area code/entity-id
;;; slip past validation. (curry regex) compiles down to POSIX regexec, a
;;; NUL-terminated-C-string API, so a regex-only check would see only the
;;; bytes up to the NUL and could wrongly accept a value that's actually
;;; longer/malformed once its full, real length is considered — validation
;;; here is done with string-length/string-ref instead (see %valid-area-code?/
;;; %valid-entity-id? in naips.scm) specifically to avoid that.

(check "area code with embedded NUL rejected (not silently truncated)"
       (guard (e (#t 'raised))
         (naips-area-briefing "user1234" "pass1234"
           (list (string-append "7100" (string (integer->char 0)) "garbage")))
         'not-raised)
       'raised)

(check "entity-id with embedded NUL rejected (not silently truncated)"
       (guard (e (#t 'raised))
         (naips-notam-briefing "user1234" "pass1234"
           (string-append "YS" (string (integer->char 0)) "garbage"))
         'not-raised)
       'raised)

;;; =========================================================================
;;; %build-flags-xml: unknown/duplicate flag names must raise, not silently
;;; drop a product category or emit a malformed repeated XML attribute.
;;; =========================================================================

(check "unknown flag name rejected"
       (guard (e (#t 'raised))
         (naips-build-loc-brief-request "user1234" "pass1234" '("YSSY") '(("mett" . #t)))
         'not-raised)
       'raised)

(check "duplicate flag name rejected"
       (guard (e (#t 'raised))
         (naips-build-loc-brief-request "user1234" "pass1234" '("YSSY")
           '(("met" . #t) ("met" . #f)))
         'not-raised)
       'raised)

(check "flag not valid for this operation rejected (sigmet is loc-brief only)"
       (guard (e (#t 'raised))
         (naips-build-area-brief-request "user1234" "pass1234" '("7100") '(("sigmet" . #t)))
         'not-raised)
       'raised)

;;; =========================================================================
;;; general-met-dir / get-general-met
;;; =========================================================================

(define dir-req (naips-build-general-met-dir-request "user1234" "pass1234"))
(check "general-met-dir request wrapper present" (%found? dir-req "<loc:general-met-dir-rqs") 'found)
(check "general-met-dir request has no body elements"
       (%found? dir-req "<loc:general-met-dir-rqs source=\"curry\" requestor=\"user1234\" password=\"pass1234\"></loc:general-met-dir-rqs>")
       'found)

(define brief-req (naips-build-general-met-brief-request "user1234" "pass1234" "National GAF Summary" "GAF_TEXT"))
(check "general-met-brief request wrapper present" (%found? brief-req "<loc:general-met-brief-rqs") 'found)
(check "general-met-brief request name element present" (%found? brief-req "<loc:name>National GAF Summary</loc:name>") 'found)
(check "general-met-brief request type element present" (%found? brief-req "<loc:type>GAF_TEXT</loc:type>") 'found)

(define brief-esc-req (naips-build-general-met-brief-request "user1234" "pass1234" "A & B <name>" "T\"y<pe>"))
(check "general-met-brief name ampersand/less-than escaped" (%found? brief-esc-req "A &amp; B &lt;name&gt;") 'found)
(check "general-met-brief type quote/less-than escaped" (%found? brief-esc-req "T&quot;y&lt;pe&gt;") 'found)
(check "no raw '<' from general-met-brief name/type leaks into markup"
       (%found? brief-esc-req "B <name") 'not-found)

;; general-met-dir-rsp: a flat list of <msg><name/><type/></msg>, no
;; top-level <content> and no <product> elements (it's a directory listing,
;; not a briefing) — a distinct response shape from the four *-brief
;; operations, so it gets its own parser (naips-parse-general-met-dir-response)
;; rather than reusing naips-parse-briefing-response.
(define %dir-success-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<ns2:general-met-dir-rsp xmlns:ns2=\"http://www.airservicesaustralia.com/naips/xsd\" status=\"SUCCESS\">"
    "<msg><name>National GAF Summary</name><type>GAF_TEXT</type></msg>"
    "<msg><name>Volcanic Ash Advisory</name><type>VAA</type></msg>"
    "</ns2:general-met-dir-rsp>"
    "</S:Body>"
    "</S:Envelope>"))

(define dir-msgs (naips-parse-general-met-dir-response %dir-success-response))
(check "general-met-dir message count" (length dir-msgs) 2)
(check "general-met-dir first message name" (naips-general-met-message-name (list-ref dir-msgs 0)) "National GAF Summary")
(check "general-met-dir first message type" (naips-general-met-message-type (list-ref dir-msgs 0)) "GAF_TEXT")
(check "general-met-dir second message name" (naips-general-met-message-name (list-ref dir-msgs 1)) "Volcanic Ash Advisory")
(check "general-met-dir->alist"
       (naips-general-met-message->alist (list-ref dir-msgs 1))
       (list (cons "name" "Volcanic Ash Advisory") (cons "type" "VAA")))

(define %dir-error-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<ns2:general-met-dir-rsp xmlns:ns2=\"http://www.airservicesaustralia.com/naips/xsd\" status=\"INVALID_ACCOUNT\">"
    "<info>Invalid account id or password.</info>"
    "</ns2:general-met-dir-rsp>"
    "</S:Body>"
    "</S:Envelope>"))

(check "general-met-dir non-success status raises"
       (guard (e (#t 'raised))
         (naips-parse-general-met-dir-response %dir-error-response)
         'not-raised)
       'raised)

(check "general-met-dir soap fault raises"
       (guard (e (#t 'raised))
         (naips-parse-general-met-dir-response %fault-response)
         'not-raised)
       'raised)

;; get-general-met-brief-rsp is a bare BriefingResponse, same shape as
;; loc-brief-rsp/area-brief-rsp/met-brief-rsp/notam-brief-rsp, so it reuses
;; naips-parse-briefing-response — no separate parser needed.
(define %general-met-brief-response
  (string-append
    "<?xml version='1.0' encoding='UTF-8'?>"
    "<S:Envelope xmlns:S=\"http://schemas.xmlsoap.org/soap/envelope/\">"
    "<S:Body>"
    "<ns2:general-met-brief-rsp xmlns:ns2=\"http://www.airservicesaustralia.com/naips/xsd\" status=\"SUCCESS\">"
    "<content>GENERAL MET TEXT GOES HERE</content>"
    "</ns2:general-met-brief-rsp>"
    "</S:Body>"
    "</S:Envelope>"))

(define gmb (naips-parse-briefing-response %general-met-brief-response))
(check "general-met-brief response status" (naips-briefing-status gmb) "SUCCESS")
(check "general-met-brief response content" (naips-briefing-content gmb) "GENERAL MET TEXT GOES HERE")

(check "general-met-briefing empty name rejected"
       (guard (e (#t 'raised))
         (naips-general-met-briefing "user1234" "pass1234" "" "GAF_TEXT")
         'not-raised)
       'raised)

(check "general-met-briefing empty type rejected"
       (guard (e (#t 'raised))
         (naips-general-met-briefing "user1234" "pass1234" "National GAF Summary" "")
         'not-raised)
       'raised)

;;; =========================================================================

(newline)
(display "Total: ") (display (+ pass fail)) (display " Pass: ") (display pass)
(display " Fail: ") (display fail) (newline)
(if (> fail 0) (exit 1))
