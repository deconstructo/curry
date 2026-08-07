;;; examples/naips/naips_seed_area_cache.scm — seed the NAIPS area-code cache
;;; Version: 2.0
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
;;;   ./build/curry examples/naips/naips_seed_area_cache.scm
;;;   NAIPS_OKF_CACHE_DIR=/custom/path ./build/curry examples/naips/naips_seed_area_cache.scm
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
             #:verified (list (list (cons "by" "naips_seed_area_cache.scm/2.0") (cons "at" (%today))))
             #:body (string-append
                      "# Locations\n\n"
                      (apply string-append (map (lambda (l) (string-append "- " l "\n")) merged-locations))))))
    (okf-write-concept concept root id)
    (display "seeded ") (display id)
    (display " (") (display (length merged-locations)) (display " locations)")
    (newline)))

(define root (%cache-root))
(display "Seeding NAIPS area-code cache at: ") (display root) (newline)

;; All areas below were confirmed 2026-08-07 via live area_briefing calls
;; issued one area (or a handful) at a time through the curry-naips MCP
;; server, reading back which locations each response actually listed
;; under that area's "AREA NN (NN)" header. Sub-division codes (e.g. 9201
;; vs 9202 vs 9209, all splits of Area 20) were queried individually and
;; cross-checked against each other to avoid conflating merged-batch
;; responses — see the source-note on each for how it was isolated.

;; --- NSW (Area 20/21/22/24) ---

(seed-area! root "9200" "Hunter / North Coast / Sydney Basin, NSW" "NSW"
  '("MTB" "MUI" "YARM" "YBCG" "YBNA" "YBTH" "YCAS" "YCBB" "YCFS" "YCNK"
    "YGDH" "YGFN" "YGLI" "YKMP" "YLIS" "YMDG" "YMND" "YMOR" "YNBR" "YPMQ"
    "YSBK" "YSCO" "YSDU" "YSHW" "YSRI" "YSSY" "YSTW" "YSWS" "YTRE" "YTRY"
    "YWLM")
  "live area_briefing(\"9200\") response, curry-naips MCP session")

(seed-area! root "9201" "Hunter / North Coast / Sydney Basin, NSW — coastal sub-division of Area 20" "NSW"
  '("YBCG" "YBNA" "YCAS" "YCFS" "YCNK" "YGFN" "YKMP" "YLIS" "YMND" "YPMQ"
    "YSBK" "YSHW" "YSRI" "YSSY" "YSWS" "YTRE" "YTRY" "YWLM")
  "live area_briefing(\"9201\") response, curry-naips MCP session, 2026-08-07 — coastal sub-division, distinct from 9202 (inland)")

(seed-area! root "9202" "Hunter / North Coast / Sydney Basin, NSW — inland/tablelands sub-division of Area 20" "NSW"
  '("MTB" "MUI" "YARM" "YBTH" "YCAS" "YCBB" "YCNK" "YGDH" "YGLI" "YMDG"
    "YMND" "YMOR" "YNBR" "YSCO" "YSDU" "YSRI" "YSTW" "YSWS")
  "live area_briefing(\"9202\") response, curry-naips MCP session, 2026-08-07 — inland sub-division, distinct from 9201 (coastal)")

(seed-area! root "9209" "Sydney Basin core — Sydney/Bankstown/Holsworthy/Richmond/Camden area, sub-division of Area 20" "NSW"
  '("YSBK" "YSHW" "YSRI" "YSSY" "YSWS" "YTRY")
  "live area_briefing(\"9209\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9210" "Southern Tablelands / South Coast / Riverina, NSW" "NSW"
  '("MSV" "YCOM" "YCWR" "YFBS" "YGLB" "YJBY" "YMAY" "YMER" "YMRY" "YORG"
    "YPKS" "YSCB" "YSCN" "YSHL" "YSNW" "YSWG" "YTEM" "YYNG")
  "live area_briefing(\"9210\") response, curry-naips MCP session")

(seed-area! root "9211" "Southern Tablelands / South Coast NSW — coastal sub-division of Area 21" "NSW"
  '("MSV" "YJBY" "YMER" "YMRY" "YSBK" "YSCN" "YSHL" "YSHW" "YSNW" "YSSY" "YSWS")
  "live area_briefing(\"9211\") response, curry-naips MCP session, 2026-08-07 — distinct from 9212 (tablelands/inland)")

(seed-area! root "9212" "Southern Tablelands NSW / Canberra — inland sub-division of Area 21" "NSW"
  '("MSV" "MTB" "YBTH" "YCOM" "YGLB" "YSCB" "YSWS")
  "live area_briefing(\"9212\") response, curry-naips MCP session, 2026-08-07 — distinct from 9211 (coastal)")

(seed-area! root "9220" "Broken Hill / Riverina / Central-West NSW (Area 22)" "NSW"
  '("YBHI" "YBKE" "YCBA" "YCBT" "YCDO" "YCNM" "YDLQ" "YFBS" "YGTH" "YHAY"
    "YIVO" "YMAY" "YMIA" "YMOR" "YNAR" "YPKS" "YSDU" "YSWG" "YTEM" "YTIB"
    "YWCA" "YWHC" "YWLG" "YWWL")
  "live area_briefing(\"9220\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9240" "Lord Howe Island / Norfolk Island (Area 24)" #f
  '("YLHI" "YSNF")
  "live area_briefing(\"9240\") response, curry-naips MCP session, 2026-08-07")

;; --- Victoria (Area 30) ---

(seed-area! root "9300" "Victoria (Area 30)" "VIC"
  '("KMG" "YBDG" "YBLT" "YBNS" "YBXU" "YCEM" "YCHL" "YDLQ" "YEDE" "YFLK"
    "YHGI" "YHML" "YHOT" "YHPN" "YHSM" "YLTV" "YMAV" "YMAY" "YMCO" "YMEN"
    "YMES" "YMIA" "YMMB" "YMML" "YMNG" "YMPC" "YNHL" "YPOD" "YSHT" "YSWH"
    "YSWL" "YWBL" "YWGT" "YWLP" "YYRM" "YYWG")
  "live area_briefing(\"9300\") response, curry-naips MCP session, 2026-08-07; note 9310/9320 returned \"unknown\" for this area's sub-divisions")

;; --- Queensland (Area 40/41/43/44/45) ---

(seed-area! root "9400" "Southeast Queensland (Brisbane, Gold Coast, Sunshine Coast, Toowoomba, Rockhampton) (Area 40)" "QLD"
  '("YAMB" "YBAF" "YBBN" "YBCG" "YBOK" "YBRK" "YBSU" "YBUD" "YBWW" "YEML"
    "YGAY" "YGDI" "YGLA" "YHBA" "YHRN" "YKRY" "YMYB" "YTNG" "YTWB")
  "live area_briefing(\"9400\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9410" "Southwest Queensland outback (Charleville, Longreach, Roma) (Area 41)" "QLD"
  '("YBCK" "YBCV" "YBDV" "YEML" "YLLE" "YLRE" "YROM" "YSGE" "YTGM" "YWDH")
  "live area_briefing(\"9410\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9430" "Northwest Queensland outback (Mount Isa, Cloncurry, Winton) (Area 43)" "QLD"
  '("YBMA" "YCCY" "YCMT" "YCNY" "YGTN" "YHUG" "YJLC" "YRMD" "YTEE" "YTMO" "YWTN")
  "live area_briefing(\"9430\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9440" "Central Queensland coast (Mackay, Townsville, Hamilton Island) (Area 44)" "QLD"
  '("YBHM" "YBMK" "YBPN" "YBTL" "YMRB" "YSMH" "YWIS")
  "live area_briefing(\"9440\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9450" "Far north Queensland / Cape York (Cairns, Weipa, Cooktown) (Area 45)" "QLD"
  '("YBCS" "YBKT" "YBSG" "YBWP" "YCFL" "YCKN" "YCOE" "YHID" "YIFL" "YKOW"
    "YLHR" "YMBA" "YMTI" "YNTN" "YPVI")
  "live area_briefing(\"9450\") response, curry-naips MCP session, 2026-08-07")

;; --- South Australia (Area 50/51/52/53) ---

(seed-area! root "9500" "Adelaide / Eyre Peninsula / Kangaroo Island / lower SA (Area 50)" "SA"
  '("YCEE" "YCMM" "YKDI" "YKSC" "YMBD" "YMIN" "YMTG" "YNRC" "YPAD" "YPAG"
    "YPED" "YPLC" "YPPF" "YRBE" "YREN" "YWHA")
  "live area_briefing(\"9500\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9510" "Flinders Ranges / Olympic Dam / Woomera, SA (Area 51)" "SA"
  '("YLEC" "YMRE" "YOLD" "YPWR" "YYUN")
  "live area_briefing(\"9510\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9520" "Far north SA / Simpson Desert edge (Birdsville, Coober Pedy, Moomba) (Area 52)" "SA"
  '("YBDV" "YCBP" "YERN" "YOOD" "YOOM")
  "live area_briefing(\"9520\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9530" "West coast SA / Nullarbor edge (Ceduna, Tarcoola, Wudinna) (Area 53)" "SA"
  '("YCDU" "YTAR" "YWUD")
  "live area_briefing(\"9530\") response, curry-naips MCP session, 2026-08-07")

;; --- Western Australia (Area 60/61/62/63/64/65/66/68/69) ---

(seed-area! root "9600" "Perth metro / WA wheatbelt-midwest (Area 60)" "WA"
  '("YCUN" "YGAD" "YGEL" "YGGE" "YGIG" "YMOG" "YMRW" "YPEA" "YPJT" "YPPH" "YRTI")
  "live area_briefing(\"9600\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9610" "WA Goldfields — Kalgoorlie / Leonora (Area 61)" "WA"
  '("YLEO" "YLST" "YLTN" "YPKG" "YSCR")
  "live area_briefing(\"9610\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9620" "Nullarbor — Forrest, WA (Area 62)" "WA"
  '("YFRT")
  "live area_briefing(\"9620\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9630" "WA south coast — Albany / Esperance / Bunbury (Area 63)" "WA"
  '("YABA" "YBLN" "YBUN" "YESP" "YEST" "YNSM")
  "live area_briefing(\"9630\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9640" "Central WA desert — Giles / Warburton / Tropicana (Area 64)" "WA"
  '("YGLS" "YTRA" "YWBR" "YWMO")
  "live area_briefing(\"9640\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9650" "WA Gascoyne coast — Carnarvon / Shark Bay (Area 65)" "WA"
  '("YCAR" "YSHK")
  "live area_briefing(\"9650\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9660" "WA Murchison — Meekatharra / Wiluna (Area 66)" "WA"
  '("YMEK" "YMNE" "YWLU")
  "live area_briefing(\"9660\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9680" "Pilbara WA — Karratha / Port Hedland / Newman (Area 68)" "WA"
  '("YANG" "YBGD" "YBRY" "YBWX" "YCHK" "YCWA" "YEWA" "YFDF" "YIBO" "YKDD"
    "YMDZ" "YNWN" "YOLW" "YPBO" "YPKA" "YPLM" "YPPD" "YROE" "YSOL" "YTEF" "YTHV")
  "live area_briefing(\"9680\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9690" "Kimberley WA — Broome / Derby / Kununurra (Area 69)" "WA"
  '("YBRM" "YCIN" "YDBY" "YFTZ" "YHLC" "YKAL" "YKLC" "YLBD" "YPKU" "YTST" "YTTI" "YWYM")
  "live area_briefing(\"9690\") response, curry-naips MCP session, 2026-08-07")

;; --- Tasmania (Area 70) ---

(seed-area! root "9700" "Tasmania (Area 70)" "TAS"
  '("YDPO" "YFLI" "YKII" "YMHB" "YMLT" "YMSY" "YSMI" "YSRN" "YSTH" "YTSI" "YWYY")
  "live area_briefing(\"9700\") response, curry-naips MCP session, 2026-08-07")

;; --- Northern Territory (Area 80/84/85) ---

(seed-area! root "9800" "Top End NT — Darwin / Arnhem Land / Gove (Area 80)" "NT"
  '("FAW" "YBRL" "YBTI" "YELD" "YGTE" "YJAB" "YMGB" "YMGD" "YMHU" "YNGU"
    "YPDN" "YPGV" "YPKT" "YPTN" "YVRD")
  "live area_briefing(\"9800\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9840" "Central-north NT — Tennant Creek / Tanami (Area 84)" "NT"
  '("YHOO" "YTGT" "YTNK" "YYND")
  "live area_briefing(\"9840\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9850" "Central Australia — Alice Springs / Ayers Rock / Kintore (Area 85)" "NT"
  '("YAYE" "YBAS" "YKNT")
  "live area_briefing(\"9850\") response, curry-naips MCP session, 2026-08-07")

;; --- Offshore / external territories (no state, no Area grouping for 9980/9990) ---

(seed-area! root "9860" "Timor Sea offshore (Bayu-Undan, Troughton Island) (Area 86)" #f
  '("YBYU" "YTTI")
  "live area_briefing(\"9860\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9980" "Cocos (Keeling) Islands / Christmas Island (external territories, no Area grouping)" #f
  '("YPCC" "YPXM")
  "live area_briefing(\"9980\") response, curry-naips MCP session, 2026-08-07")

(seed-area! root "9990" "Willis Island, Coral Sea (external territory, no Area grouping)" #f
  '("YWLD")
  "live area_briefing(\"9990\") response, curry-naips MCP session, 2026-08-07")

(newline)
(display "Done. area_code_lookup/area_code_cache_add (in examples/naips/mcp_naips.scm) read this same cache.")
(newline)
