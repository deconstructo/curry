;;
;; 𒀭 ṭuppi ṣipti ša šittim pānûtim
;; The Tablet of the Series of the Two Predecessors
;;
;; Standard Babylonian Akkadian — written in cuneiform and transliteration.
;; Akkadian is the default language in Curry Scheme: no import needed.
;; To make it explicit, or to switch from another language:
;;
;;   (set-active-language! "akkadian")
;;
;; All cuneiform tokens (𒁹, 𒅁, 𒋗𒈠, …) and transliterated synonyms
;; (šakānum, epēšum, šumma, …) are translated to their Scheme equivalents
;; at eval time by the language pack runtime. English Scheme also works
;; alongside them — the languages are not exclusive.
;;
;; 𒊕:  0    rēšum  — the first, the beginning
;; 𒆜:  1    arkûm  — the second, the one that follows
;; 𒋻𒁹: 𒊕 𒆜  matāḫum — each term is the sum of the two before it
;;
;; The Babylonians computed Fibonacci-like series for astronomical
;; prediction (the "zig-zag functions" of cuneiform astronomy tablets).
;;

;;; napḫar-ṣiptu ("sum-series") — compute the series to mīnûtu terms
(𒁹 (napḫar-ṣiptu mīnûtu)           ;; šakānum / define
  (𒅁 alāku                          ;; šakānum alāku = let with named loop
      ((i       0)
       (pānû    0)                    ;; pānû  = "the one before"
       (arkû    1)                    ;; arkû  = "the one after"
       (ṣipātu '()))                  ;; ṣipātu = "the series" (accumulated)
    (𒋗𒈠 (𒈠𒋻 i mīnûtu)             ;; šumma (= i mīnûtu) — if we've reached mīnûtu
        (𒋻𒀀 ṣipātu)                 ;;   reverse — return the series right-way-round
        (alāku (𒋻𒁹 i 1)             ;;   (+ i 1)       — advance the counter
               arkû                   ;;   next pānû is the current arkû
               (𒋻𒁹 pānû arkû)       ;;   (+ pānû arkû) — sum gives next arkû
               (𒇲 pānû ṣipātu)))))  ;;   cons — prepend current term to series

;;; šapāru ṣipāti ("write out the series")
(𒅁 alāku
    ((ṣipātu (napḫar-ṣiptu 10000))
     (mīnu   1))                      ;; mīnu = "term number"
  (𒌑 (𒇲𒇲 ṣipātu)                   ;; enūma (pair? ṣipātu) — while terms remain
    (𒅆 mīnu)                         ;;   display  — the term index
    (𒅆 ". ")
    (𒅆 (𒊕 ṣipātu))                  ;;   display  (car) — the value
    (𒁹𒁹𒁹)                           ;;   newline
    (alāku (𒆜 ṣipātu)                ;;   (cdr) — the remaining terms
           (𒋻𒁹 mīnu 1))))            ;;   (+ mīnu 1) — next index
