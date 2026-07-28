;;; (curry aviation-weather) tests — METAR / TAF / ATIS parsing

(import (curry aviation-weather))

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
;;; METAR
;;; =========================================================================

(define metar-text "SPECI CYYZ 051326Z 15008KT 1/2SM R15L/4000V5500FT/D SN VV005 M03/M04 A2993")
(define m (metar-parse metar-text))

(check "metar report-type" (metar-report-type m) 'speci)
(check "metar station" (metar-station m) "CYYZ")
(check "metar time" (metar-time m) '(("day" . 5) ("hour" . 13) ("minute" . 26)))
(check "metar wind direction" (wind-direction (metar-wind m)) 150)
(check "metar wind speed" (wind-speed (metar-wind m)) 8)
(check "metar wind unit" (wind-unit (metar-wind m)) 'kt)
(check "metar wind gust is absent" (wind-gust (metar-wind m)) #f)
(check "metar visibility (exact fraction)" (metar-visibility m) 1/2)
(check "metar rvr count" (length (metar-rvr m)) 1)
(check "metar rvr runway" (rvr-runway (car (metar-rvr m))) "15L")
(check "metar rvr min-distance" (rvr-min-distance (car (metar-rvr m))) 4000)
(check "metar rvr max-distance" (rvr-max-distance (car (metar-rvr m))) 5500)
(check "metar rvr trend" (rvr-trend (car (metar-rvr m))) 'd)
(check "metar weather count" (length (metar-weather m)) 1)
(check "metar weather raw" (weather-phenomenon-raw (car (metar-weather m))) "SN")
(check "metar weather intensity (moderate = #f)" (weather-phenomenon-intensity (car (metar-weather m))) #f)
(check "metar cloud count" (length (metar-cloud m)) 1)
(check "metar cloud type (vertical visibility)" (cloud-layer-type (car (metar-cloud m))) 'vv)
(check "metar cloud height" (cloud-layer-height (car (metar-cloud m))) 5)
(check "metar temperature (negative)" (metar-temperature m) -3)
(check "metar dewpoint (negative)" (metar-dewpoint m) -4)
(check "metar altimeter (exact, inHg)" (metar-altimeter m) 2993/100)
(check "metar remarks empty when no RMK present" (metar-remarks m) "")

;;; METAR variations

(define m2 (metar-parse "METAR KJFK 121851Z AUTO 27015G25KT 240V300 10SM FEW020 SCT250 22/12 A3005 RMK AO2 SLP168"))
(check "metar keyword defaults to 'metar" (metar-report-type m2) 'metar)
(check "metar AUTO flag" (metar-auto? m2) #t)
(check "metar wind gust present" (wind-gust (metar-wind m2)) 25)
(check "metar variable wind from" (wind-variable-from (metar-wind m2)) 240)
(check "metar variable wind to" (wind-variable-to (metar-wind m2)) 300)
(check "metar visibility whole SM" (metar-visibility m2) 10)
(check "metar multiple cloud layers" (map cloud-layer-type (metar-cloud m2)) '(few sct))
(check "metar positive temperature" (metar-temperature m2) 22)
(check "metar RMK text captured" (metar-remarks m2) "AO2 SLP168")

(define m3 (metar-parse "METAR EGLL 121850Z 24012KT 9999 -RA SCT015 BKN025 12/10 Q1013"))
(check "metar international visibility (metres)" (metar-visibility m3) 9999)
(check "metar international altimeter (QNH, hPa)" (metar-altimeter m3) 1013)
(check "metar weather intensity light" (weather-phenomenon-intensity (car (metar-weather m3))) '-)

(define m4 (metar-parse "METAR KDEN 121851Z 18010KT 1 1/2SM -SN BR OVC008 M02/M04 A2998"))
(check "metar combined whole+fraction visibility" (metar-visibility m4) 3/2)
(check "metar multiple weather phenomena" (map weather-phenomenon-raw (metar-weather m4)) '("-SN" "BR"))

(define m5 (metar-parse "METAR KORD 121851Z 00000KT 10SM CLR 15/08 A3012"))
(check "metar calm wind direction is still parsed as a number" (wind-direction (metar-wind m5)) 0)
(check "metar CLR cloud has no height" (cloud-layer-height (car (metar-cloud m5))) #f)

(define m6 (metar-parse "METAR KLAX 121851Z VRB03KT 10SM SKC 20/15 A2995"))
(check "metar variable wind direction (VRB)" (wind-direction (metar-wind m6)) 'VRB)

(define m7 (metar-parse "METAR PANC 121851Z 27020G35KT 1SM +TSRA BKN008 OVC015CB 10/08 A2950 RMK TS OHD"))
(check "metar thunderstorm weather code decomposes" (weather-phenomenon-codes (car (metar-weather m7))) '("TS" "RA"))
(check "metar cloud CB modifier" (cloud-layer-modifier (cadr (metar-cloud m7))) 'cb)

;;; CAVOK / NSW — regressions found by independent review: these are
;;; extremely common tokens (CAVOK especially, in international traffic)
;;; that previously weren't recognized at all, which (like the P/M-
;;; visibility bug) silently dropped every field after them.

(define m8 (metar-parse "METAR EGLL 281120Z 24008KT CAVOK 18/12 Q1013"))
(check "metar CAVOK flag is set" (metar-cavok? m8) #t)
(check "metar fields after CAVOK are not lost: temperature" (metar-temperature m8) 18)
(check "metar fields after CAVOK are not lost: dewpoint" (metar-dewpoint m8) 12)
(check "metar fields after CAVOK are not lost: altimeter" (metar-altimeter m8) 1013)
(check "metar without CAVOK reports the flag as false" (metar-cavok? m) #f)

(define m9 (metar-parse "METAR KXXX 121851Z 18010KT 10SM NSW SCT250 25/18 A2993 RMK WS ALL RWY"))
(check "metar NSW flag is set" (metar-nsw? m9) #t)
(check "metar fields after NSW are not lost: cloud" (map cloud-layer-type (metar-cloud m9)) '(sct))
(check "metar fields after NSW are not lost: temperature" (metar-temperature m9) 25)
(check "metar fields after NSW are not lost: altimeter" (metar-altimeter m9) 2993/100)
(check "metar remarks after NSW still captured" (metar-remarks m9) "WS ALL RWY")

(define taf-cavok (taf-parse "TAF EGLL 281120Z 2812/2918 24008KT CAVOK BECMG 2900/2902 18006KT CAVOK"))
(check "taf CAVOK in the base group does not swallow the BECMG group"
       (map taf-group-type (taf-groups taf-cavok))
       '(base becmg))
(check "taf CAVOK flag set on both groups"
       (map taf-group-cavok? (taf-groups taf-cavok))
       '(#t #t))

;;; ->alist

(check "metar-report->alist has the expected keys"
       (map car (metar-report->alist m))
       '("report-type" "station" "time" "auto" "wind" "visibility" "rvr" "weather" "cloud" "temperature" "dewpoint" "altimeter" "cavok" "nsw" "remarks"))

;;; =========================================================================
;;; TAF
;;; =========================================================================

(define taf-text "TAF AMD CYYZ 051239Z 0512/0618 16010KT 1/2SM SN VV004
TEMPO 0512/0514 2SM -SN OVC010
FM051400 16010KT 1SM -SN BR OVC005")
(define t (taf-parse taf-text))

(check "taf station" (taf-station t) "CYYZ")
(check "taf amended flag" (taf-amended? t) #t)
(check "taf issue-time" (taf-issue-time t) '(("day" . 5) ("hour" . 12) ("minute" . 39)))
(check "taf valid-from" (taf-valid-from t) '(("day" . 5) ("hour" . 12)))
(check "taf valid-to" (taf-valid-to t) '(("day" . 6) ("hour" . 18)))
(check "taf group count" (length (taf-groups t)) 3)
(check "taf group types in order" (map taf-group-type (taf-groups t)) '(base tempo from))

(define base-g (car (taf-groups t)))
(check "taf base group wind" (wind-speed (taf-group-wind base-g)) 10)
(check "taf base group visibility" (taf-group-visibility base-g) 1/2)
(check "taf base group cloud" (cloud-layer-type (car (taf-group-cloud base-g))) 'vv)

(define tempo-g (cadr (taf-groups t)))
(check "taf tempo group has its own period" (taf-group-valid-from tempo-g) '(("day" . 5) ("hour" . 12)))
(check "taf tempo group visibility overrides base" (taf-group-visibility tempo-g) 2)
(check "taf tempo group weather" (weather-phenomenon-raw (car (taf-group-weather tempo-g))) "-SN")

(define from-g (caddr (taf-groups t)))
(check "taf FM group has an instant, not a range" (taf-group-valid-to from-g) #f)
(check "taf FM group has hour/minute" (taf-group-valid-from from-g) '(("day" . 5) ("hour" . 14) ("minute" . 0)))
(check "taf FM group two weather phenomena" (map weather-phenomenon-raw (taf-group-weather from-g)) '("-SN" "BR"))

;;; TAF with PROB and BECMG

(define taf-text2 "TAF KDEN 121720Z 1218/1324 15010KT 6SM -SN OVC020
BECMG 1220/1222 09015KT P6SM SCT040
PROB30 1300/1304 1SM +SN OVC010")
(define t2 (taf-parse taf-text2))
(check "taf with BECMG and PROB30: group count" (length (taf-groups t2)) 3)
(check "taf group types with becmg/prob30" (map taf-group-type (taf-groups t2)) '(base becmg prob30))
(check "taf prob30 group visibility" (taf-group-visibility (caddr (taf-groups t2))) 1)

;;; TAF remarks

(define taf-text3 "TAF KJFK 121720Z 1218/1324 27010KT P6SM SKC RMK FCST BASED ON AUTO OBS")
(define t3 (taf-parse taf-text3))
(check "taf remarks captured" (taf-remarks t3) "FCST BASED ON AUTO OBS")

;;; =========================================================================
;;; ATIS
;;; =========================================================================

(define atis-text "1326Z CYYZ ARR ATIS O
1326Z 16008KT 1/2SM SN
VV005 M03/M04 A2993 APCH
ILS RWY 15L. LDG RWY 15L")
(define a (atis-parse atis-text))

(check "atis station" (atis-station a) "CYYZ")
(check "atis type" (atis-type a) 'arr)
(check "atis information letter" (atis-information-letter a) "O")
(check "atis time" (atis-time a) '(("hour" . 13) ("minute" . 26)))
(check "atis wind direction" (wind-direction (atis-wind a)) 160)
(check "atis wind speed" (wind-speed (atis-wind a)) 8)
(check "atis visibility" (atis-visibility a) 1/2)
(check "atis weather" (map weather-phenomenon-raw (atis-weather a)) '("SN"))
(check "atis cloud" (cloud-layer-type (car (atis-cloud a))) 'vv)
(check "atis temperature" (atis-temperature a) -3)
(check "atis dewpoint" (atis-dewpoint a) -4)
(check "atis altimeter" (atis-altimeter a) 2993/100)
(check "atis remarks captures approach/runway free text"
       (atis-remarks a)
       "APCH ILS RWY 15L LDG RWY 15L")

(define atis-text2 "1500Z KDEN DEP ATIS D 1500Z 18012G20KT 10SM FEW250 25/10 A3005 DEPG RWY 17L")
(define a2 (atis-parse atis-text2))
(check "atis departure type" (atis-type a2) 'dep)
(check "atis information letter D" (atis-information-letter a2) "D")
(check "atis wind gust" (wind-gust (atis-wind a2)) 20)

;;; ->alist

(check "atis-report->alist has the expected keys"
       (map car (atis-report->alist a))
       '("station" "type" "information-letter" "time" "wind" "visibility" "weather" "cloud" "temperature" "dewpoint" "altimeter" "cavok" "nsw" "remarks"))
(check "taf-report->alist has the expected keys"
       (map car (taf-report->alist t))
       '("station" "amended" "corrected" "issue-time" "valid-from" "valid-to" "groups" "remarks"))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
