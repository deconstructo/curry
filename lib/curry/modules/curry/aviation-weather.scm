;;; (curry aviation-weather) — METAR / TAF / ATIS parsing.
;;;
;;; Pure Scheme, using (curry regex) (POSIX extended regex, already built
;;; in) for field-level token classification — no new C, and no library
;;; was added to curry to build this; see docs/reference/module-
;;; aviation-weather.md for why POSIX ERE (no named groups, no lookaround)
;;; is not a limitation here: rather than a handful of large regexes (one
;;; per whole message, the common approach in other languages), a report
;;; is split into whitespace-delimited tokens first, and each token is
;;; classified independently by a small, local, position-agnostic regex —
;;; group *position* only ever matters within one tiny pattern.
;;;
;;; Design: parsing produces plain immutable records (wind, cloud layers,
;;; weather phenomena, and one top-level record per report type), each
;;; with a companion ->alist converter — not a JSON/YAML-serializing
;;; method on the record itself. Hand the alist to (curry json) or
;;; (curry yaml) if you want a string; this module only parses.
;;;
;;; Scope (see docs/reference/module-aviation-weather.md for the full
;;; story): North American (statute-mile visibility, inHg altimeter) and
;;; ICAO international (metre visibility, hPa/QNH altimeter) field formats
;;; are both recognized at the token level. ATIS free text beyond the
;;; standard weather fields (approach in use, active runway, NOTAMs) is
;;; not parsed into structured fields — European-format temperature
;;; notation (e.g. "TM03") is not recognized.

(define-library (curry aviation-weather)
  (import (curry regex) (scheme base) (scheme write))
  (export
    wind? make-wind wind-direction wind-speed wind-gust wind-unit
    wind-variable-from wind-variable-to wind->alist
    cloud-layer? make-cloud-layer cloud-layer-type cloud-layer-height
    cloud-layer-modifier cloud-layer->alist
    weather-phenomenon? make-weather-phenomenon weather-phenomenon-intensity
    weather-phenomenon-codes weather-phenomenon-raw weather-phenomenon->alist
    rvr? make-rvr rvr-runway rvr-min-distance rvr-max-distance rvr-unit rvr-trend
    rvr->alist
    metar-report? metar-report-type metar-station metar-time metar-auto?
    metar-wind metar-visibility metar-rvr metar-weather metar-cloud
    metar-temperature metar-dewpoint metar-altimeter metar-cavok? metar-nsw?
    metar-remarks metar-parse metar-report->alist
    taf-group? make-taf-group taf-group-type taf-group-valid-from
    taf-group-valid-to taf-group-wind taf-group-visibility taf-group-weather
    taf-group-cloud taf-group-cavok? taf-group-nsw? taf-group->alist
    taf-report? taf-station taf-amended? taf-corrected? taf-issue-time
    taf-valid-from taf-valid-to taf-groups taf-remarks taf-parse taf-report->alist
    atis-report? atis-station atis-type atis-information-letter atis-time
    atis-wind atis-visibility atis-weather atis-cloud atis-temperature
    atis-dewpoint atis-altimeter atis-cavok? atis-nsw? atis-remarks
    atis-parse atis-report->alist)
  (begin

;;; =========================================================================
;;; Small helpers
;;; =========================================================================

(define (%upcase s) (string-upcase s))

(define (%strip-trailing-period s)
  (let ((n (string-length s)))
    (if (and (> n 0) (char=? (string-ref s (- n 1)) #\.))
        (substring s 0 (- n 1))
        s)))

(define %rx-ws (regex-compile "[ \t\r\n]+"))

(define (%tokenize text)
  (filter (lambda (s) (> (string-length s) 0))
          (map %strip-trailing-period (regex-split %rx-ws (%upcase text)))))

(define (%str->int s) (string->number s))

;;; =========================================================================
;;; Field-level token classifiers (each pattern is anchored; a token either
;;; matches a field type wholesale or it doesn't — no partial/positional
;;; matching within a token).
;;; =========================================================================

(define %rx-wind (regex-compile "^(VRB|[0-9]{3})([0-9]{2,3})(G([0-9]{2,3}))?(KT|MPS|KPH)$"))
(define %rx-wind-var (regex-compile "^([0-9]{3})V([0-9]{3})$"))
;; A leading P ("plus"/greater-than, e.g. P6SM = visibility at least 6SM) or
;; M ("minus"/less-than, e.g. M1/4SM) is common in real METAR/TAF text —
;; matched and ignored (the qualifier itself isn't preserved; see the
;; module's scope note) rather than left to fail classification entirely,
;; which would otherwise abort body-parsing on this one token and cascade
;; into every field after it, and every group after it in a TAF, being
;; silently dropped.
(define %rx-vis-sm-whole (regex-compile "^[PM]?([0-9]{1,2})SM$"))
(define %rx-vis-sm-frac (regex-compile "^[PM]?([0-9]{1,2})/([0-9]{1,2})SM$"))
(define %rx-vis-bare-int (regex-compile "^[0-9]{1,2}$"))
(define %rx-vis-m (regex-compile "^([0-9]{4})$"))
(define %rx-rvr (regex-compile "^R([0-9]{2}[LRC]?)/([0-9]{4})(V([0-9]{4}))?(FT)?(/([UDN]))?$"))
(define %rx-wx (regex-compile "^([-+]|VC)?([A-Z]{2}){1,3}$"))
(define %rx-cloud (regex-compile "^(SKC|CLR|FEW|SCT|BKN|OVC|VV)([0-9]{3})?(CB|TCU)?$"))
(define %rx-temp-dp (regex-compile "^(M?[0-9]{2})/(M?[0-9]{2})$"))
(define %rx-alt-a (regex-compile "^A([0-9]{4})$"))
(define %rx-alt-q (regex-compile "^Q([0-9]{4})$"))
(define %rx-time (regex-compile "^([0-9]{2})([0-9]{2})([0-9]{2})Z$"))
;; ATIS timestamps (per the observed format) are HHMMZ, with no day-of-
;; month prefix — distinct from METAR/TAF's DDHHMMZ.
(define %rx-time-hhmm (regex-compile "^([0-9]{2})([0-9]{2})Z$"))
(define %rx-period (regex-compile "^([0-9]{2})([0-9]{2})/([0-9]{2})([0-9]{2})$"))
(define %rx-fm (regex-compile "^FM([0-9]{2})([0-9]{2})([0-9]{2})$"))
(define %rx-station (regex-compile "^[A-Z]{4}$"))

;; Two-letter weather codes recognized inside a ~rx-wx match, purely so a
;; token can be split back into its component codes (e.g. "TSRA" -> ("TS"
;; "RA")) for callers that want that detail; the raw code string is always
;; available too.
(define %wx-descriptors '("MI" "BC" "PR" "DR" "BL" "SH" "TS" "FZ"))
(define %wx-precip '("DZ" "RA" "SN" "SG" "IC" "PL" "GR" "GS" "UP"))
(define %wx-obscuration '("BR" "FG" "FU" "VA" "DU" "SA" "HZ" "PY"))
(define %wx-other '("PO" "SQ" "FC" "SS" "DS"))
(define %wx-known-codes (append %wx-descriptors %wx-precip %wx-obscuration %wx-other))

(define (%split-wx-codes s)
  (let loop ((i 0) (acc '()))
    (if (>= (+ i 2) (+ (string-length s) 1))
        (reverse acc)
        (loop (+ i 2) (cons (substring s i (+ i 2)) acc)))))

(define (%token-is-known-wx? s)
  (let ((codes (%split-wx-codes s)))
    (and (pair? codes)
         (let loop ((cs codes))
           (cond ((null? cs) #t)
                 ((member (car cs) %wx-known-codes) (loop (cdr cs)))
                 (else #f))))))

;;; =========================================================================
;;; Records
;;; =========================================================================

(define-record-type <wind>
  (make-wind direction speed gust unit variable-from variable-to)
  wind?
  (direction wind-direction)           ; exact integer degrees, or 'VRB
  (speed wind-speed)                   ; exact integer
  (gust wind-gust)                     ; exact integer, or #f
  (unit wind-unit)                     ; 'kt / 'mps / 'kph
  (variable-from wind-variable-from)   ; exact integer degrees, or #f
  (variable-to wind-variable-to))      ; exact integer degrees, or #f

(define (wind->alist w)
  (list (cons "direction" (wind-direction w))
        (cons "speed" (wind-speed w))
        (cons "gust" (wind-gust w))
        (cons "unit" (symbol->string (wind-unit w)))
        (cons "variable-from" (wind-variable-from w))
        (cons "variable-to" (wind-variable-to w))))

(define-record-type <cloud-layer>
  (make-cloud-layer type height modifier)
  cloud-layer?
  (type cloud-layer-type)         ; 'skc / 'clr / 'few / 'sct / 'bkn / 'ovc / 'vv
  (height cloud-layer-height)     ; hundreds of feet AGL, or #f (skc/clr)
  (modifier cloud-layer-modifier)) ; 'cb / 'tcu / #f

(define (cloud-layer->alist c)
  (list (cons "type" (symbol->string (cloud-layer-type c)))
        (cons "height" (cloud-layer-height c))
        (cons "modifier" (let ((m (cloud-layer-modifier c))) (if m (symbol->string m) #f)))))

(define-record-type <weather-phenomenon>
  (make-weather-phenomenon intensity codes raw)
  weather-phenomenon?
  (intensity weather-phenomenon-intensity) ; '- / '+ / 'vc / #f (moderate)
  (codes weather-phenomenon-codes)          ; list of 2-letter code strings
  (raw weather-phenomenon-raw))             ; original token, e.g. "-SN", "+TSRA"

(define (weather-phenomenon->alist w)
  (list (cons "intensity" (let ((i (weather-phenomenon-intensity w))) (if i (symbol->string i) #f)))
        (cons "codes" (weather-phenomenon-codes w))
        (cons "raw" (weather-phenomenon-raw w))))

(define-record-type <rvr>
  (make-rvr runway min-distance max-distance unit trend)
  rvr?
  (runway rvr-runway)             ; string, e.g. "15L"
  (min-distance rvr-min-distance) ; exact integer
  (max-distance rvr-max-distance) ; exact integer, or #f
  (unit rvr-unit)                 ; 'ft / 'm
  (trend rvr-trend))              ; 'u / 'd / 'n / #f

(define (rvr->alist r)
  (list (cons "runway" (rvr-runway r))
        (cons "min-distance" (rvr-min-distance r))
        (cons "max-distance" (rvr-max-distance r))
        (cons "unit" (symbol->string (rvr-unit r)))
        (cons "trend" (let ((tr (rvr-trend r))) (if tr (symbol->string tr) #f)))))

;;; =========================================================================
;;; Shared body parser: consumes wind/visibility/RVR/weather/cloud/temp-
;;; dewpoint/altimeter from the front of a token list, in any order,
;;; stopping at the first token that matches none of them. Returns the
;;; parsed fields plus whatever tokens are left (RMK text, or — for TAF —
;;; the next change-group keyword).
;;; =========================================================================

(define (%make-body-accum)
  (vector #f #f '() '() '() #f #f #f #f #f)) ; wind vis rvr* wx* cloud* temp dewpt alt cavok? nsw?

(define (%body-wind a) (vector-ref a 0))
(define (%body-vis a) (vector-ref a 1))
(define (%body-rvr a) (reverse (vector-ref a 2)))
(define (%body-wx a) (reverse (vector-ref a 3)))
(define (%body-cloud a) (reverse (vector-ref a 4)))
(define (%body-temp a) (vector-ref a 5))
(define (%body-dewpt a) (vector-ref a 6))
(define (%body-alt a) (vector-ref a 7))
(define (%body-cavok? a) (vector-ref a 8))
(define (%body-nsw? a) (vector-ref a 9))

(define (%try-wind tokens a)
  (and (pair? tokens)
       (let ((m (regex-match-string %rx-wind (car tokens))))
         (and m (not (%body-wind a))
              (let* ((dir-s (list-ref m 1)) (dir (if (string=? dir-s "VRB") 'VRB (%str->int dir-s)))
                     (speed (%str->int (list-ref m 2)))
                     (gust (let ((g (list-ref m 4))) (and g (%str->int g))))
                     (unit-s (list-ref m 5))
                     (unit (cond ((string=? unit-s "KT") 'kt) ((string=? unit-s "MPS") 'mps) (else 'kph)))
                     (rest (cdr tokens))
                     (varm (and (pair? rest) (regex-match-string %rx-wind-var (car rest)))))
                (vector-set! a 0 (make-wind dir speed gust unit
                                             (and varm (%str->int (list-ref varm 1)))
                                             (and varm (%str->int (list-ref varm 2)))))
                (if varm (cdr rest) rest))))))

(define (%try-visibility tokens a)
  (and (pair? tokens) (not (%body-vis a))
       (let ((tok (car tokens)) (rest (cdr tokens)))
         (cond
           ((regex-match-string %rx-vis-sm-frac tok)
            => (lambda (m)
                 (vector-set! a 1 (/ (%str->int (list-ref m 1)) (%str->int (list-ref m 2))))
                 rest))
           ((and (regex-match-string %rx-vis-bare-int tok) (pair? rest)
                 (regex-match-string %rx-vis-sm-frac (car rest)))
            => (lambda (m)
                 (vector-set! a 1 (+ (%str->int tok) (/ (%str->int (list-ref m 1)) (%str->int (list-ref m 2)))))
                 (cdr rest)))
           ((regex-match-string %rx-vis-sm-whole tok)
            => (lambda (m) (vector-set! a 1 (%str->int (list-ref m 1))) rest))
           ((regex-match-string %rx-vis-m tok)
            => (lambda (m) (vector-set! a 1 (%str->int (list-ref m 1))) rest))
           (else #f)))))

(define (%try-rvr tokens a)
  (and (pair? tokens)
       (let ((m (regex-match-string %rx-rvr (car tokens))))
         (and m
              (begin
                (vector-set! a 2
                  (cons (make-rvr (list-ref m 1) (%str->int (list-ref m 2))
                                   (let ((v (list-ref m 4))) (and v (%str->int v)))
                                   'ft
                                   (let ((tr (list-ref m 7)))
                                     (cond ((not tr) #f) ((string=? tr "U") 'u)
                                           ((string=? tr "D") 'd) (else 'n))))
                        (vector-ref a 2)))
                (cdr tokens))))))

(define (%try-wx tokens a)
  (and (pair? tokens)
       (let* ((tok (car tokens)) (m (regex-match-string %rx-wx tok)))
         (and m (%token-is-known-wx? (if (list-ref m 1) (substring tok (string-length (list-ref m 1)) (string-length tok)) tok))
              (let* ((intensity-s (list-ref m 1))
                     (code-part (if intensity-s (substring tok (string-length intensity-s) (string-length tok)) tok))
                     (intensity (cond ((not intensity-s) #f) ((string=? intensity-s "-") '-)
                                       ((string=? intensity-s "+") '+) (else 'vc))))
                (vector-set! a 3 (cons (make-weather-phenomenon intensity (%split-wx-codes code-part) tok) (vector-ref a 3)))
                (cdr tokens))))))

(define (%try-cloud tokens a)
  (and (pair? tokens)
       (let ((m (regex-match-string %rx-cloud (car tokens))))
         (and m
              (let* ((type-s (list-ref m 1))
                     (type (cond ((string=? type-s "SKC") 'skc) ((string=? type-s "CLR") 'clr)
                                 ((string=? type-s "FEW") 'few) ((string=? type-s "SCT") 'sct)
                                 ((string=? type-s "BKN") 'bkn) ((string=? type-s "OVC") 'ovc)
                                 (else 'vv)))
                     (height (let ((h (list-ref m 2))) (and h (%str->int h))))
                     (mod-s (list-ref m 3))
                     (modifier (cond ((not mod-s) #f) ((string=? mod-s "CB") 'cb) (else 'tcu))))
                (vector-set! a 4 (cons (make-cloud-layer type height modifier) (vector-ref a 4)))
                (cdr tokens))))))

(define (%try-temp-dewpoint tokens a)
  (and (pair? tokens) (not (%body-temp a))
       (let ((m (regex-match-string %rx-temp-dp (car tokens))))
         (and m
              (let ((parse1 (lambda (s) (if (char=? (string-ref s 0) #\M)
                                             (- (%str->int (substring s 1 (string-length s))))
                                             (%str->int s)))))
                (vector-set! a 5 (parse1 (list-ref m 1)))
                (vector-set! a 6 (parse1 (list-ref m 2)))
                (cdr tokens))))))

(define (%try-altimeter tokens a)
  (and (pair? tokens) (not (%body-alt a))
       (let ((tok (car tokens)))
         (cond
           ((regex-match-string %rx-alt-a tok)
            => (lambda (m) (vector-set! a 7 (/ (%str->int (list-ref m 1)) 100)) (cdr tokens)))
           ((regex-match-string %rx-alt-q tok)
            => (lambda (m) (vector-set! a 7 (%str->int (list-ref m 1))) (cdr tokens)))
           (else #f)))))

;; CAVOK ("ceiling and visibility OK": visibility >=10km, no cloud below
;; 5000ft or the highest minimum sector altitude, no CB, no significant
;; weather) and NSW ("no significant weather") are common tokens — CAVOK
;; especially so in international METAR/TAF — that carry real information
;; but aren't a wind/visibility/RVR/weather/cloud/temp-dewpoint/altimeter
;; value themselves. Without a classifier for them, the first one
;; encountered would (like the P/M-visibility and TAF group-boundary bugs
;; fixed earlier) stop body-parsing dead and silently drop every field
;; after it — for a TAF, every subsequent change group too. Recognized
;; and consumed as boolean flags (%body-cavok?/%body-nsw?) rather than
;; synthesizing a fabricated visibility/cloud/weather value for CAVOK,
;; which would misrepresent a value that was never actually reported.
(define (%try-cavok-nsw tokens a)
  (and (pair? tokens)
       (cond
         ((string=? (car tokens) "CAVOK") (vector-set! a 8 #t) (cdr tokens))
         ((string=? (car tokens) "NSW") (vector-set! a 9 #t) (cdr tokens))
         (else #f))))

(define %body-classifiers
  (list %try-temp-dewpoint %try-wind %try-visibility %try-rvr %try-cavok-nsw
        %try-wx %try-cloud %try-altimeter))

;; Returns the remaining, unconsumed tokens (RMK text, next TAF change
;; group, etc.) after mutating `a` with everything recognized.
(define (%parse-body! tokens a)
  (let loop ((tokens tokens))
    (if (null? tokens)
        tokens
        (let try ((cs %body-classifiers))
          (if (null? cs)
              tokens ; no classifier matched this token: stop, hand the rest back
              (let ((result ((car cs) tokens a)))
                (if result (loop result) (try (cdr cs)))))))))

(define (%join-remaining tokens)
  (if (null? tokens) "" (%join-tokens tokens)))

(define (%join-tokens tokens)
  (if (null? tokens)
      ""
      (let ((out (open-output-string)))
        (write-string (car tokens) out)
        (for-each (lambda (t) (write-char #\space out) (write-string t out)) (cdr tokens))
        (get-output-string out))))

;;; =========================================================================
;;; METAR / SPECI
;;; =========================================================================

(define-record-type <metar-report>
  (%make-metar-report report-type station time auto? wind visibility rvr weather cloud temperature dewpoint altimeter cavok? nsw? remarks)
  metar-report?
  (report-type metar-report-type)   ; 'metar / 'speci
  (station metar-station)
  (time metar-time)                 ; alist ((day . _) (hour . _) (minute . _))
  (auto? metar-auto?)
  (wind metar-wind)
  (visibility metar-visibility)
  (rvr metar-rvr)                   ; list of <rvr>
  (weather metar-weather)           ; list of <weather-phenomenon>
  (cloud metar-cloud)               ; list of <cloud-layer>
  (temperature metar-temperature)
  (dewpoint metar-dewpoint)
  (altimeter metar-altimeter)
  (cavok? metar-cavok?)             ; "ceiling and visibility OK" — see (curry aviation-weather)'s CAVOK note
  (nsw? metar-nsw?)                 ; "no significant weather"
  (remarks metar-remarks))

(define (%parse-time-token tok)
  (let ((m (regex-match-string %rx-time tok)))
    (and m (list (cons "day" (%str->int (list-ref m 1)))
                 (cons "hour" (%str->int (list-ref m 2)))
                 (cons "minute" (%str->int (list-ref m 3)))))))

(define (%parse-hhmm-time-token tok)
  (let ((m (regex-match-string %rx-time-hhmm tok)))
    (and m (list (cons "hour" (%str->int (list-ref m 1)))
                 (cons "minute" (%str->int (list-ref m 2)))))))

(define (metar-parse text)
  (let* ((tokens (%tokenize text))
         (report-type (cond ((and (pair? tokens) (string=? (car tokens) "SPECI")) 'speci)
                             ((and (pair? tokens) (string=? (car tokens) "METAR")) 'metar)
                             (else 'metar)))
         (tokens (if (and (pair? tokens) (member (car tokens) '("METAR" "SPECI"))) (cdr tokens) tokens))
         (tokens (if (and (pair? tokens) (string=? (car tokens) "COR")) (cdr tokens) tokens))
         (station (and (pair? tokens) (regex-match-string %rx-station (car tokens)) (car tokens)))
         (tokens (if station (cdr tokens) tokens))
         (time (and (pair? tokens) (%parse-time-token (car tokens))))
         (tokens (if time (cdr tokens) tokens))
         (auto? (and (pair? tokens) (string=? (car tokens) "AUTO")))
         (tokens (if auto? (cdr tokens) tokens))
         (a (%make-body-accum))
         (remaining (%parse-body! tokens a))
         (remaining (if (and (pair? remaining) (string=? (car remaining) "RMK")) (cdr remaining) remaining)))
    (%make-metar-report report-type station time auto?
                         (%body-wind a) (%body-vis a) (%body-rvr a) (%body-wx a) (%body-cloud a)
                         (%body-temp a) (%body-dewpt a) (%body-alt a)
                         (%body-cavok? a) (%body-nsw? a)
                         (%join-remaining remaining))))

(define (metar-report->alist r)
  (list (cons "report-type" (symbol->string (metar-report-type r)))
        (cons "station" (metar-station r))
        (cons "time" (metar-time r))
        (cons "auto" (metar-auto? r))
        (cons "wind" (let ((w (metar-wind r))) (and w (wind->alist w))))
        (cons "visibility" (metar-visibility r))
        (cons "rvr" (map rvr->alist (metar-rvr r)))
        (cons "weather" (map weather-phenomenon->alist (metar-weather r)))
        (cons "cloud" (map cloud-layer->alist (metar-cloud r)))
        (cons "temperature" (metar-temperature r))
        (cons "dewpoint" (metar-dewpoint r))
        (cons "altimeter" (metar-altimeter r))
        (cons "cavok" (metar-cavok? r))
        (cons "nsw" (metar-nsw? r))
        (cons "remarks" (metar-remarks r))))

;;; =========================================================================
;;; TAF
;;; =========================================================================

(define-record-type <taf-group>
  (make-taf-group type valid-from valid-to wind visibility weather cloud cavok? nsw?)
  taf-group?
  (type taf-group-type)             ; 'base / 'tempo / 'becmg / 'from / 'prob30 / 'prob40
  (valid-from taf-group-valid-from) ; alist ((day . _) (hour . _)), or ((day)(hour)(minute)) for FM
  (valid-to taf-group-valid-to)     ; alist, or #f for FM/PROB-without-TEMPO
  (wind taf-group-wind)
  (visibility taf-group-visibility)
  (weather taf-group-weather)
  (cloud taf-group-cloud)
  (cavok? taf-group-cavok?)
  (nsw? taf-group-nsw?))

(define (taf-group->alist g)
  (list (cons "type" (symbol->string (taf-group-type g)))
        (cons "valid-from" (taf-group-valid-from g))
        (cons "valid-to" (taf-group-valid-to g))
        (cons "wind" (let ((w (taf-group-wind g))) (and w (wind->alist w))))
        (cons "visibility" (taf-group-visibility g))
        (cons "weather" (map weather-phenomenon->alist (taf-group-weather g)))
        (cons "cloud" (map cloud-layer->alist (taf-group-cloud g)))
        (cons "cavok" (taf-group-cavok? g))
        (cons "nsw" (taf-group-nsw? g))))

(define-record-type <taf-report>
  (%make-taf-report station amended? corrected? issue-time valid-from valid-to groups remarks)
  taf-report?
  (station taf-station)
  (amended? taf-amended?)
  (corrected? taf-corrected?)
  (issue-time taf-issue-time)
  (valid-from taf-valid-from)
  (valid-to taf-valid-to)
  (groups taf-groups)     ; list of <taf-group>, first is 'base
  (remarks taf-remarks))

(define (%parse-period-token tok)
  (let ((m (regex-match-string %rx-period tok)))
    (and m (list (list (cons "day" (%str->int (list-ref m 1))) (cons "hour" (%str->int (list-ref m 2))))
                 (list (cons "day" (%str->int (list-ref m 3))) (cons "hour" (%str->int (list-ref m 4))))))))

(define (%change-group-keyword? tok)
  (or (string=? tok "TEMPO") (string=? tok "BECMG") (string=? tok "PROB30") (string=? tok "PROB40")
      (regex-match-string %rx-fm tok)))

(define (%parse-taf-group! tokens type)
  ;; tokens starts right after the group's own keyword (and, for
  ;; TEMPO/BECMG/PROB30/PROB40, its ddhh/ddhh period token, already
  ;; consumed by the caller) or right after FMddhhmm (whose single instant
  ;; is both valid-from and valid-to, and is parsed here).
  (let* ((a (%make-body-accum))
         (remaining (%parse-body! tokens a)))
    (values (make-taf-group type #f #f (%body-wind a) (%body-vis a) (%body-wx a) (%body-cloud a)
                            (%body-cavok? a) (%body-nsw? a))
            remaining)))

(define (taf-parse text)
  (let* ((tokens (%tokenize text))
         (tokens (if (and (pair? tokens) (string=? (car tokens) "TAF")) (cdr tokens) tokens))
         (amended? (and (pair? tokens) (string=? (car tokens) "AMD")))
         (tokens (if amended? (cdr tokens) tokens))
         (corrected? (and (pair? tokens) (string=? (car tokens) "COR")))
         (tokens (if corrected? (cdr tokens) tokens))
         (station (and (pair? tokens) (regex-match-string %rx-station (car tokens)) (car tokens)))
         (tokens (if station (cdr tokens) tokens))
         (issue-time (and (pair? tokens) (%parse-time-token (car tokens))))
         (tokens (if issue-time (cdr tokens) tokens))
         (period (and (pair? tokens) (%parse-period-token (car tokens))))
         (tokens (if period (cdr tokens) tokens)))
    (let loop ((tokens tokens) (groups '()))
      (cond
        ((null? tokens)
         (%make-taf-report station amended? corrected? issue-time
                            (and period (car period)) (and period (cadr period))
                            (reverse groups) ""))
        ((string=? (car tokens) "RMK")
         (%make-taf-report station amended? corrected? issue-time
                            (and period (car period)) (and period (cadr period))
                            (reverse groups) (%join-remaining (cdr tokens))))
        ((or (string=? (car tokens) "TEMPO") (string=? (car tokens) "BECMG"))
         (let* ((type (if (string=? (car tokens) "TEMPO") 'tempo 'becmg))
                (rest (cdr tokens))
                (p (and (pair? rest) (%parse-period-token (car rest))))
                (rest (if p (cdr rest) rest)))
           (let-values (((g remaining) (%parse-taf-group! rest type)))
             (loop remaining (cons (if p (make-taf-group type (car p) (cadr p)
                                                          (taf-group-wind g) (taf-group-visibility g)
                                                          (taf-group-weather g) (taf-group-cloud g)
                                                          (taf-group-cavok? g) (taf-group-nsw? g))
                                       g)
                                    groups)))))
        ((or (string=? (car tokens) "PROB30") (string=? (car tokens) "PROB40"))
         (let* ((type (if (string=? (car tokens) "PROB30") 'prob30 'prob40))
                (rest (cdr tokens))
                (rest (if (and (pair? rest) (string=? (car rest) "TEMPO")) (cdr rest) rest))
                (p (and (pair? rest) (%parse-period-token (car rest))))
                (rest (if p (cdr rest) rest)))
           (let-values (((g remaining) (%parse-taf-group! rest type)))
             (loop remaining (cons (if p (make-taf-group type (car p) (cadr p)
                                                          (taf-group-wind g) (taf-group-visibility g)
                                                          (taf-group-weather g) (taf-group-cloud g)
                                                          (taf-group-cavok? g) (taf-group-nsw? g))
                                       g)
                                    groups)))))
        ((regex-match-string %rx-fm (car tokens))
         => (lambda (m)
              (let* ((inst (list (cons "day" (%str->int (list-ref m 1)))
                                  (cons "hour" (%str->int (list-ref m 2)))
                                  (cons "minute" (%str->int (list-ref m 3))))))
                (let-values (((g remaining) (%parse-taf-group! (cdr tokens) 'from)))
                  (loop remaining (cons (make-taf-group 'from inst #f (taf-group-wind g)
                                                          (taf-group-visibility g) (taf-group-weather g)
                                                          (taf-group-cloud g)
                                                          (taf-group-cavok? g) (taf-group-nsw? g))
                                         groups))))))
        ((null? groups)
         ;; Base forecast group (no leading keyword) — only ever valid as
         ;; the very first group; every later iteration that reaches here
         ;; means the current token is neither a recognized change-group
         ;; keyword nor RMK, and must not be treated as a second implicit
         ;; base group (which would silently absorb, and typically break
         ;; on, everything after it — including any real PROB30/TEMPO/etc.
         ;; groups still to come).
         (let-values (((g remaining) (%parse-taf-group! tokens 'base)))
           (if (eq? remaining tokens)
               ;; nothing consumed at all: stop rather than loop forever.
               (%make-taf-report station amended? corrected? issue-time
                                  (and period (car period)) (and period (cadr period))
                                  (reverse (cons g groups)) (%join-remaining remaining))
               (loop remaining (cons g groups)))))
        (else
         ;; An unrecognized token after at least one group has already
         ;; been parsed: stop here rather than mis-parsing it as another
         ;; base group. The unconsumed text is preserved as remarks so
         ;; nothing is silently dropped.
         (%make-taf-report station amended? corrected? issue-time
                            (and period (car period)) (and period (cadr period))
                            (reverse groups) (%join-remaining tokens)))))))

(define (taf-report->alist r)
  (list (cons "station" (taf-station r))
        (cons "amended" (taf-amended? r))
        (cons "corrected" (taf-corrected? r))
        (cons "issue-time" (taf-issue-time r))
        (cons "valid-from" (taf-valid-from r))
        (cons "valid-to" (taf-valid-to r))
        (cons "groups" (map taf-group->alist (taf-groups r)))
        (cons "remarks" (taf-remarks r))))

;;; =========================================================================
;;; ATIS
;;; =========================================================================

(define-record-type <atis-report>
  (%make-atis-report station type information-letter time wind visibility weather cloud temperature dewpoint altimeter cavok? nsw? remarks)
  atis-report?
  (station atis-station)
  (type atis-type)                             ; 'arr / 'dep / #f
  (information-letter atis-information-letter)  ; single-char string, e.g. "O"
  (time atis-time)
  (wind atis-wind)
  (visibility atis-visibility)
  (weather atis-weather)
  (cloud atis-cloud)
  (temperature atis-temperature)
  (dewpoint atis-dewpoint)
  (altimeter atis-altimeter)
  (cavok? atis-cavok?)
  (nsw? atis-nsw?)
  (remarks atis-remarks)) ; raw free text: approach in use, active runway, NOTAMs — not structurally parsed

(define (atis-parse text)
  (let* ((tokens (%tokenize text))
         (time1 (and (pair? tokens) (%parse-hhmm-time-token (car tokens))))
         (tokens (if time1 (cdr tokens) tokens))
         (station (and (pair? tokens) (regex-match-string %rx-station (car tokens)) (car tokens)))
         (tokens (if station (cdr tokens) tokens))
         (type (and (pair? tokens) (cond ((string=? (car tokens) "ARR") 'arr)
                                          ((string=? (car tokens) "DEP") 'dep)
                                          (else #f))))
         (tokens (if type (cdr tokens) tokens))
         (tokens (if (and (pair? tokens) (string=? (car tokens) "ATIS")) (cdr tokens) tokens))
         (info-letter (and (pair? tokens) (= (string-length (car tokens)) 1) (car tokens)))
         (tokens (if info-letter (cdr tokens) tokens))
         ;; A second time token (common form: "1326Z <station> ARR ATIS O\n1326Z 16008KT ...")
         (time2 (and (pair? tokens) (%parse-hhmm-time-token (car tokens))))
         (tokens (if time2 (cdr tokens) tokens))
         (a (%make-body-accum))
         (remaining (%parse-body! tokens a)))
    (%make-atis-report station type info-letter (or time2 time1)
                        (%body-wind a) (%body-vis a) (%body-wx a) (%body-cloud a)
                        (%body-temp a) (%body-dewpt a) (%body-alt a)
                        (%body-cavok? a) (%body-nsw? a)
                        (%join-remaining remaining))))

(define (atis-report->alist r)
  (list (cons "station" (atis-station r))
        (cons "type" (let ((ty (atis-type r))) (and ty (symbol->string ty))))
        (cons "information-letter" (atis-information-letter r))
        (cons "time" (atis-time r))
        (cons "wind" (let ((w (atis-wind r))) (and w (wind->alist w))))
        (cons "visibility" (atis-visibility r))
        (cons "weather" (map weather-phenomenon->alist (atis-weather r)))
        (cons "cloud" (map cloud-layer->alist (atis-cloud r)))
        (cons "temperature" (atis-temperature r))
        (cons "dewpoint" (atis-dewpoint r))
        (cons "altimeter" (atis-altimeter r))
        (cons "cavok" (atis-cavok? r))
        (cons "nsw" (atis-nsw? r))
        (cons "remarks" (atis-remarks r))))

  )) ;; end begin, define-library
