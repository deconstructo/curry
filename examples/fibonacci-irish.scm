;;
;; Sraith Fibonacci as Gaeilge
;; The Fibonacci Series in Irish (Gaeilge)
;;
;; Gaeilge — teanga na hÉireann, ceann de na teangacha is sine san Eoraip.
;; Irish — the language of Ireland, one of the oldest living languages in Europe.
;;
;; Focal ar fhocal — word by word:
;;
;;   sainmhínigh  — define (sainmhíniú: give a precise definition)
;;   lig          — let (ligean: allow, hold)
;;   má           — if
;;   nuair        — when
;;   inbhéartaigh — reverse (invert the order)
;;   cónasc       — cons (cónasc: link, join — an ancient word for binding)
;;   ceann        — car (ceann: head, the first of a thing)
;;   eireaball    — cdr (eireaball: tail)
;;   taispeáin    — display (taispeáin: show, make visible)
;;   líne-nua     — newline
;;   péire?       — pair?
;;
;; The words for the mathematical concept:
;;
;;   roimhe    — the one before (literally: before it)
;;   ina dhiaidh — the one after (literally: in its wake)
;;   sraith    — series, sequence, row
;;   timthriall — cycle, circuit (our loop variable)
;;   téarma    — term (a term in a series)
;;   uimhir    — number
;;   ceann     — head / the current one (double duty: car and "one")
;;
;; Scriobhadh an clár seo as Gaeilge mar thaispeántas.
;; This program was written in Irish as a demonstration.
;; Tá fáilte roimh cheartúcháin ó chainteoirí dúchais.
;; Corrections from native speakers are welcome.
;;
;; Rith le / Run with:
;;   ./build/curry examples/fibonacci-irish.scm
;;

(import (curry lang))
(lang:load-file! "langs/irish.scm")
(set-active-language! "irish")

;;
;; Ón bpointe seo, scríobhtar an clár as Gaeilge.
;; From this point, the program is written in Irish.
;;

;;; ríomh-sraithe — "reckoning of the series"
;;; Ríomhann sé sraith Fibonacci go dtí |deireadh| téarmaí.
;;; Computes the Fibonacci series up to |deireadh| terms.
(sainmhínigh (ríomh-sraithe deireadh)       ;; define
  (lig timthriall                            ;; let — named loop (timthriall = cycle/circuit)
      ((i          0)
       (roimhe     0)                        ;; roimhe    = the one before
       (ina-dhiaidh 1)                       ;; ina-dhiaidh = the one after (in its wake)
       (sraith     '()))                     ;; sraith    = the series (accumulated)
    (má (= i deireadh)                       ;; if i = deireadh
        (inbhéartaigh sraith)                ;; reverse — fill in correct order
        (timthriall (+ i 1)                  ;; lean ar aghaidh — advance the counter
                    ina-dhiaidh              ;; tagann an dara ceann chun tosaigh
                    (+ roimhe ina-dhiaidh)   ;; an suim — the sum is the next term
                    (cónasc roimhe sraith))))) ;; cónasc — link current term to series

;;; scríobh-sraithe — "writing-out of the series"
;;; Clóscríobhann gach téarma lena uimhir ordanáis.
;;; Prints each term with its ordinal number.
(lig timthriall
    ((sraith  (ríomh-sraithe 10000))
     (uimhir  1))                            ;; uimhir = number (the term index)
  (nuair (péire? sraith)                     ;; when (pair? sraith) — fad is ann
    (taispeáin uimhir)                       ;; taispeáin — show the index
    (taispeáin ". ")
    (taispeáin (ceann sraith))               ;; ceann — the head / current value
    (líne-nua)                               ;; líne nua — go to next line
    (timthriall (eireaball sraith)           ;; eireaball — the tail / remaining terms
                (+ uimhir 1))))              ;; an chéad uimhir eile — next index
