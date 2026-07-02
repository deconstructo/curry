;;
;; De Serie Fibonacciana
;; The Fibonacci Series — in Classical Latin
;;
;; Latin is not merely a historical curiosity here. Much of what we call
;; "programming vocabulary" is Latin in thin disguise:
;;
;;   integer  — whole, untouched        (Curry's integer? predicate IS Latin)
;;   series   — row, sequence, chain    (our word for list)
;;   radix    — root                    (sqrt: the root from which a square grows)
;;   recursio — a running back          (what Fibonacci numbers do)
;;   terminus — boundary, end-point     (the stopping condition)
;;   gradus   — step, degree            (each Fibonacci step forward)
;;   prior    — the one before          (natural Latin for the predecessor term)
;;   sequens  — the one following       (natural Latin for the successor; cf. sequel)
;;
;; Run with:
;;   ./build/curry examples/fibonacci-latin.scm
;;

(import (curry lang))
(lang:load-file! "langs/latin.scm")
(set-active-language! "latin")

;;
;; From here the program is written in Latin.
;; English Scheme names remain valid alongside it.
;;

;;; computatio-seriei ("reckoning of the series")
;;; Computes the Fibonacci series up to |terminus| terms.
(definio (computatio-seriei terminus)
  (esto cursus                             ;; named let — cursus = a running
      ((i           0)
       (prior        0)                    ;; prior    = the one before
       (sequens      1)                    ;; sequens  = the one following (cf. sequel)
       (series-acc  '()))                  ;; series-acc = accumulated series (reversed)
    (si (= i terminus)                     ;; if i = terminus
        (inverto series-acc)               ;; reverse — return in correct order
        (cursus (+ i 1)                    ;; advance counter
                sequens                    ;; next prior is current sequens
                (+ prior sequens)          ;; next sequens is their sum
                (iungo prior series-acc))))) ;; cons — prepend current term

;;; scriptura-seriei ("the writing-out of the series")
;;; Prints each term with its ordinal index.
(esto cursus
    ((series-acc  (computatio-seriei 10000))
     (numerus     1))                      ;; numerus = number, term index
  (quando (geminum? series-acc)            ;; when (pair? series-acc) — while terms remain
    (ostendo numerus)                      ;; display the index
    (ostendo ". ")
    (ostendo (caput series-acc))           ;; display (car) — the value
    (nova-linea)                           ;; newline
    (cursus (cauda series-acc)             ;; (cdr) — remaining terms
            (+ numerus 1))))               ;; next index
