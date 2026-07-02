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
      showing the mapping structure. Community linguists: fork and complete.")
    ("irish" "Gaeilge (Modern Irish)"
     "Modern Irish (Gaeilge) — one of Europe's oldest living languages,
      with a literary tradition of 1500 years. Covers all built-ins, the
      CAS, surreal/quantum/multivector towers, and module procedures.
      Suitable for Gaeltacht communities and Gaelscoileanna.")
    ("greek" "Ελληνική Κλασική (Classical Greek)"
     "Classical Ancient Greek — the language of Euclid, Aristotle, and
      Archimedes. Uses authentic polytonic Unicode. Reveals the Greek roots
      of mathematical and computing vocabulary: ἀριθμός, λόγος, θεωρία,
      ἄτομον. Full coverage of all built-ins and modules.")
    ("latin" "Latina Classica (Classical Latin)"
     "Classical Latin — shows that much of computing IS Latin: integer,
      vector, modulus, error, quotiens, sinus, quantum, momentum, limes,
      radix, gradus. Full coverage of all built-ins and modules.
      A living demonstration of etymology.")))
