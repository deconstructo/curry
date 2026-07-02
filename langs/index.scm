;;; langs/index.scm — Language pack registry index for Curry Scheme.
;;;
;;; Format: list of (id display-name description)
;;; This file is fetched by (curry lang) to enumerate available packs.

(define lang-index
  '(("akkadian" "Akkadian / Cuneiform"
     "Standard Babylonian Akkadian synonyms plus cuneiform Unicode tokens.
      The built-in language; always available without network access.")
    ("warlpiri" "Warlpiri (Yapa)"
     "Central Australian indigenous language. A template / starter pack
      showing the mapping structure. Community linguists: fork and complete.")))
