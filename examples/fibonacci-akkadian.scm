;;
;; 𒀭 ṭuppi ṣipti ša šittim pānûtim
;; The Tablet of the Series of the Two Predecessors
;;
;; 𒊕:   0     — rēšum:   first term is zero
;; 𒆜:   1     — arkûm:   second term is one
;; 𒋻𒁹:  𒊕 𒆜  — matāḫum: each later term is the sum of the two before it
;;

;;; napḫar-ṣiptu ("sum-series") — compute the series to mīnûtu terms
(𒁹 (napḫar-ṣiptu mīnûtu)          ;; define
  (𒅁 alāku                         ;; let  (alāku = "to go"; named loop)
      ((i       0)
       (pānû    0)                   ;; pānû  = "the one before"
       (arkû    1)                   ;; arkû  = "the one after"
       (ṣipātu '()))                 ;; ṣipātu = "the series" (accumulated, reversed)
    (𒋗𒈠 (𒈠𒋻 i mīnûtu)            ;; if  (= i mīnûtu)
        (𒋻𒀀 ṣipātu)                ;;   reverse  — return series in correct order
        (alāku (𒋻𒁹 i 1)            ;;   + i 1    — advance the counter
               arkû                  ;;   next pānû is current arkû
               (𒋻𒁹 pānû arkû)      ;;   + pānû arkû  — next arkû is their sum
               (𒇲 pānû ṣipātu))))) ;; cons pānû ṣipātu — prepend term to series

;;; šapāru ṣipāti ("write out the series")
(𒅁 alāku                            ;; let  (named loop)
    ((ṣipātu (napḫar-ṣiptu 100))
     (mīnu   1))                      ;; mīnu = "term number"
  (𒌑 (𒇲𒇲 ṣipātu)                   ;; when  (pair? ṣipātu)
    (𒅆 mīnu)                         ;;   display  term index
    (𒅆 ". ")                         ;;   display  separator
    (𒅆 (𒊕 ṣipātu))                  ;;   display  (car ṣipātu) — the value
    (𒁹𒁹𒁹)                           ;;   newline
    (alāku (𒆜 ṣipātu)               ;;   (cdr ṣipātu) — rest of series
           (𒋻𒁹 mīnu 1))))           ;;   (+ mīnu 1)  — next index
