;;; langs/warlpiri.scm — Warlpiri language pack template for Curry Scheme.
;;;
;;; Language: Warlpiri (Yapa kurlangu yimi) — Central Australian
;;; Status:   TEMPLATE — starter mappings only; community review needed.
;;;
;;; To complete this pack:
;;;   1. Review the mapping table below with a fluent Warlpiri speaker.
;;;   2. Add procedure names (display, map, list, +, …) that feel natural.
;;;   3. Send a pull request to the curry repo, or distribute as warlpiri.scm.
;;;
;;; Usage:
;;;   (import (curry lang))
;;;   (lang:load-file! "langs/warlpiri.scm")
;;;   ;; or from the registry once the pack is published:
;;;   (lang:install! "warlpiri")

(import (curry lang))

(register-language!
  `((id           . "warlpiri")
    (display-name . "Warlpiri (Yapa)")
    (intro        . "Yapa yimi Warlpiri kurlangu — ngajuju karlipa yimi!")
    (error-preamble . "Ngurra-kurlu karlipa yimi:")
    (mappings     .
      ;; (warlpiri-name  english-canonical  "cultural/conceptual note")
      ;;
      ;; Special forms — the building blocks of programs
      (yirdi       lambda    "pattern / way of doing something")
      (nyinaja     define    "give a name to something")
      (kuja        if        "when / whether")
      (manu        and       "together with")
      (yuwayi      or        "or / either")
      (panu-panu   begin     "one after another")
      (karnta-yani let       "holding onto / keeping near")
      (jinta-jinta letrec    "each holding the other")

      ;; Procedures — things you can do
      (jaru        display   "speak / show / make visible")
      (nyamba      list      "group of things / mob")
      (karlipa     map       "go across / touch each one")
      (pina        car       "the first / the head")
      (manu-kari   cdr       "the rest / the tail")
      (kurlangu    cons      "join together / make a pair")

      ;; Values / predicates
      (yuwayi-ku   not       "make opposite")
      (pirli       zero?     "nothing / empty")
      )))

;; Uncomment to activate immediately when this file is loaded:
;; (set-active-language! "warlpiri")
