;;; examples/naips_seed_area_cache.scm — seed the NAIPS area-code cache
;;; Version: 1.0
;;;
;;; A maintenance script, run by a human (not an MCP tool an agent calls
;;; mid-conversation), that writes a baseline into the same OKF cache
;;; mcp_naips.scm's area_code_lookup/area_code_cache_add tools read and
;;; grow — see that file's "Area-code cache" section for why the mapping
;;; isn't shipped as static data in the repo instead. Only areas actually
;;; confirmed go in here: each area code below was confirmed by issuing a
;;; live area_briefing call and reading back which locations it returned
;;; (see docs/reference/module-naips.md for the operation). No area code is
;;; guessed or transcribed from a source that hasn't been cross-checked
;;; against a real response.
;;;
;;; Usage:
;;;   ./build/curry examples/naips_seed_area_cache.scm
;;;   NAIPS_OKF_CACHE_DIR=/custom/path ./build/curry examples/naips_seed_area_cache.scm
;;;
;;; Safe to re-run: writes go through the same merge-by-area_code path
;;; area_code_cache_add uses, so re-seeding an already-seeded cache just
;;; re-confirms it (locations union with themselves; source/verified
;;; entries append, so a repeat run does add a fresh dated confirmation).

(import (scheme base) (scheme write) (curry okf) (curry regex) (srfi 19))

(define (%cache-root)
  (or (get-environment-variable "NAIPS_OKF_CACHE_DIR")
      (string-append
        (or (get-environment-variable "XDG_CACHE_HOME")
            (string-append (get-environment-variable "HOME") "/.cache"))
        "/curry/naips-areas")))

(define (%area-concept-id area-code) (string-append "areas/" area-code))
(define (%loc-tag? t) (and (>= (string-length t) 4) (string=? (substring t 0 4) "loc:")))
(define (%tag->loc t) (substring t 4 (string-length t)))
(define (%loc->tag l) (string-append "loc:" (string-upcase l)))
(define (%concept-locations c) (map %tag->loc (filter %loc-tag? (okf-concept-tags c))))

;; Same validation mcp_naips.scm's area_code_cache_add applies — kept in
;; sync by hand since this script deliberately doesn't import mcp_naips.scm
;; (that file starts an MCP stdio server and requires NAIPS credentials at
;; load time, neither of which this offline seeder wants).
(define %rx-naips-area-code (regex-compile "^[789][0-9]{3}$"))
(define (%valid-naips-area-code? s) (and (regex-match %rx-naips-area-code s) #t))

(define (%string-list-union a b)
  (let loop ((xs b) (acc a))
    (cond ((null? xs) acc)
          ((member (car xs) acc) (loop (cdr xs) acc))
          (else (loop (cdr xs) (append acc (list (car xs))))))))

(define (%today) (date->string (current-date) "~Y-~m-~d"))

(define (seed-area! root area-code region state locations source-note)
  (unless (%valid-naips-area-code? area-code)
    (error "seed-area!: area_code must be 4 digits starting with 7, 8, or 9" area-code))
  (let* ((id (%area-concept-id area-code))
         (bundle (and (file-exists? root) (okf-load-bundle root)))
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
             #:verified (list (list (cons "by" "naips_seed_area_cache.scm/1.0") (cons "at" (%today))))
             #:body (string-append
                      "# Locations\n\n"
                      (apply string-append (map (lambda (l) (string-append "- " l "\n")) merged-locations))))))
    (okf-write-concept concept root id)
    (display "seeded ") (display id)
    (display " (") (display (length merged-locations)) (display " locations)")
    (newline)))

(define root (%cache-root))
(display "Seeding NAIPS area-code cache at: ") (display root) (newline)

;; Confirmed 2026-08-07 via a live area_briefing(["9200","9210"]) call —
;; every location listed is one the response actually returned under that
;; area, not carried over from documentation alone.

(seed-area! root "9200" "Hunter / North Coast / Sydney Basin, NSW" "NSW"
  '("MTB" "MUI" "YARM" "YBCG" "YBNA" "YBTH" "YCAS" "YCBB" "YCFS" "YCNK"
    "YGDH" "YGFN" "YGLI" "YKMP" "YLIS" "YMDG" "YMND" "YMOR" "YNBR" "YPMQ"
    "YSBK" "YSCO" "YSDU" "YSHW" "YSRI" "YSSY" "YSTW" "YSWS" "YTRE" "YTRY"
    "YWLM")
  "live area_briefing(\"9200\") response, curry-naips MCP session")

(seed-area! root "9210" "Southern Tablelands / South Coast / Riverina, NSW" "NSW"
  '("MSV" "YCOM" "YCWR" "YFBS" "YGLB" "YJBY" "YMAY" "YMER" "YMRY" "YORG"
    "YPKS" "YSCB" "YSCN" "YSHL" "YSNW" "YSWG" "YTEM" "YYNG")
  "live area_briefing(\"9210\") response, curry-naips MCP session")

(newline)
(display "Done. area_code_lookup/area_code_cache_add (in examples/mcp_naips.scm) read this same cache.")
(newline)
