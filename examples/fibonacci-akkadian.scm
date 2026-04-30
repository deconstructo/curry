;;
;; 𒀭 ṭuppi ṣipti ša šittim pānûtim
;; The Tablet of the Series of the Two Predecessors
;;
;; šumma mīnu rēšu:   0
;; šumma mīnu šanûtu: 1
;; šumma mīnu arkû:   napḫar ša pānîn šinā
;;
;; If the first term:  0
;; If the second term: 1
;; If a later term:    the sum of the two before it
;;

;;; napḫar-ṣiptu — compute the series to mīnûtu terms
(define (napḫar-ṣiptu mīnûtu)
  (let alāku ((i       0)
              (pānû    0)    ; the term before
              (arkû    1)    ; the term after
              (ṣipātu  '()))
    (if (= i mīnûtu)
        (reverse ṣipātu)
        (alāku (+ i 1)
               arkû
               (+ pānû arkû)
               (cons pānû ṣipātu)))))

;;; šapāru ṣipāti — write out the series
(let alāku ((ṣipātu  (napḫar-ṣiptu 100))
            (mīnu    1))
  (when (pair? ṣipātu)
    (display mīnu)
    (display ". ")
    (display (car ṣipātu))
    (newline)
    (alāku (cdr ṣipātu) (+ mīnu 1))))
