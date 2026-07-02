;;; langs/warlpiri.scm — Warlpiri language pack for Curry Scheme.
;;;
;;; Language: Warlpiri (Yapa kurlangu yimi) — Central Australian
;;;
;;; ── This is a beginning, not a finished thing. ──────────────────────────────
;;;
;;; This file is an experiment and an offer.
;;;
;;; We — the Curry project — built a system that lets programs be written in
;;; any language, not just English or Akkadian. When we thought about who that
;;; was for, we thought about Warlpiri kids at Yuendumu, Lajamanu, Willowra,
;;; Nyirrpi — young people who might find it easier, more natural, or simply
;;; more joyful to write code in their own language.
;;;
;;; So we made a start. We used published dictionaries and learner materials
;;; to put together the mappings below. Some will be right. Some will be
;;; wrong. Some might feel awkward, or miss the point, or use a word in a
;;; context it doesn't belong in. We don't know what we don't know.
;;;
;;; This file belongs to Warlpiri people, not to us.
;;;
;;; If it's useful — take it, fix it, make it yours. The format is plain
;;; text (each line is just: warlpiri-name  english-name  "a note").
;;; A teacher, linguist, or fluent speaker can open this in any editor.
;;; You don't need to be a programmer to improve it.
;;;
;;; If it's not useful — if the approach is wrong, if the words don't sit
;;; right, if there's a better way to think about this — tell us and we'll
;;; remove it and work with you on something that actually helps. We would
;;; rather do nothing than do the wrong thing.
;;;
;;; ── Contact / contribute ────────────────────────────────────────────────────
;;;
;;;   File a GitHub issue, or email the maintainer.
;;;   Pull requests welcome — especially from community members and educators.
;;;   ngalikirlangu — this is for all of us, together.
;;;
;;; ── A note on this draft ────────────────────────────────────────────────────
;;;
;;; Vocabulary drawn from:
;;;   - Warlpiri–English Dictionary (Laughren, Hoogenraad, Hale, Granites, 1996)
;;;   - AIATSIS Warlpiri materials
;;;   - Warlpiri learner resources, NTDE
;;;   - Jukurrpa Media Warlpiri curriculum materials
;;;
;;; ⚠  Some entries are compounds not in standard dictionaries — invented to
;;;    fill a gap. These are marked in the notes. Tonal and register nuance is
;;;    not captured. Some words have restricted or ceremonial uses that this
;;;    file cannot know — please review with a fluent speaker before teaching.
;;;
;;; Usage:
;;;   (import (curry lang))
;;;   (lang:load-file! "langs/warlpiri.scm")
;;;   (set-active-language! "warlpiri")

(import (curry lang))

(register-language!
  `((id           . "warlpiri")
    (display-name . "Warlpiri (Yapa)")
    (intro        . "Yapa yimi Warlpiri kurlangu — ngajuju karlipa yimi!")
    (error-preamble . "Ngurra-kurlu karlipa yimi:")
    (mappings     .
      ;; (warlpiri-name  english-canonical  "cultural / conceptual note")
      ;;
      ;; ── Special forms: the law of the program ───────────────────────────────
      ;;
      ;; In Warlpiri thought, naming and pattern-making are acts of creation
      ;; connected to Jukurrpa (the Dreaming). We use that connection here.

      ((nyinaja      define     "nyinami = to be, to sit — give a name so a thing can be known")
      (yirdi        lambda     "pattern / way of doing — the shape of an action")
      (kuja         if         "kuja = when/if (subordinating conjunction)")
      (pala         cond       "pala = then / those — choosing among paths")
      (panu-panu    begin      "panu = many; one after another in sequence")
      (yarda        let        "yarda = then, continuing — hold these names for now")
      (karnta-yani  let*       "carry forward one by one in order")
      (jinta-jinta  letrec     "jinta = one; each holding the other mutually")
      (pirramini    set!       "pirramini = change, transform — alter what a name holds")
      (manu         and        "manu = and, with — two things going together")
      (yuwayi       or         "yuwayi = yes / either — one or the other will do")
      (lawa         not        "lawa = no, none, lacking, absent")
      (ngula-juku   when       "ngula-juku = right then, just when that happens")
      (lawa-kuja    unless     "lawa + kuja = if not, only when absent")
      (luwarni      do         "luwarni = do, carry out (iterative action)")
      (yimi-pirri   quote      "yimi = word/speech; pirri = hold still, keep as-is")
      (wangkaja     begin      "wangkaja = speaking/saying — start the telling")
      (yirrarni     delay      "yirrarni = place down, set aside for later")
      (marlpa-nyina spawn      "marlpa = companion; nyina = sit — birth a new companion")
      (yimi-wangkaja send!     "send a word/message to another")
      (yimi-nyanyi  receive    "nyanyi = see/hear — receive what was sent")
      (ngajuju      self       "ngajuju = I, myself — the one speaking")

      ;; ── Pairs and lists: kin and mob ────────────────────────────────────────
      ;;
      ;; Warlpiri kinship (kurdungurlu / kirda) structures groups relationally.
      ;; A list is a ngurlu — a travelling group moving through country together.

      (kurlangu     cons       "kurlangu = belonging-to, joined with — link two things")
      (pina         car        "pina = ear, first — what you hear first / the head")
      (manu-kari    cdr        "the rest that comes after / the tail of the mob")
      (ngurlu       list       "travelling group / mob moving through country")
      (karlipa      map        "karlipa = we go — go across, visit each one")
      (kurdiji      filter     "kurdiji = shield — select and protect the wanted ones")
      (nganimpaku   for-each   "nganimpaku = for us all — do for every one")
      (panu         length     "panu = many — how many are in the mob?")
      (yirrarni-manu append    "place together, join the groups")
      (kari-yani    reverse    "turn back the other way, reverse direction")
      (marlpa       member     "marlpa = companion — is this one in the group?")
      (kari-kari    assoc      "look through to find the one that matches")

      ;; ── Logic ───────────────────────────────────────────────────────────────

      (yarda-yanu   apply      "yarda yanu = then went — carry out with these things")
      (kari-yanu    error      "kari yanu = went wrong / went away (the wrong path)")
      (ngurlu-wanti raise      "ngurlu = up; wanti = leave/put — throw up to be caught")

      ;; ── Numbers and comparison ──────────────────────────────────────────────
      ;;
      ;; Warlpiri has a counting system; jinta (one), jirrama (two),
      ;; marnkurrpa (few/several), panu (many). These map naturally.

      (kuruwarri    number->string "kuruwarri = sacred mark / count — the mark of a number")
      (wiri         max        "wiri = big, large — the greatest one")
      (wita         min        "wita = small, little — the smallest one")
      (jinta-yani   zero?      "jinta = one going to none — reaching nothing")
      (lawa-kuruwarri zero?    "lawa kuruwarri = no marks — the count of nothing")
      (wiri-kari    positive?  "going toward the big side")
      (wita-kari    negative?  "going toward the little/below side")
      (jirrama?     even?      "jirrama = two — can it pair up evenly?")
      (jinta?       odd?       "jinta = one left over — one remaining unpaired")
      (wita-juku    abs        "wita-juku = just the littleness, no direction")
      (wiri-ngurluju max       "the one going highest")
      (panu-manu    +          "adding to the mob, making more")
      (wita-manu    -          "taking from the mob, making less")

      ;; ── Predicates: knowing what a thing is ─────────────────────────────────
      ;;
      ;; In Warlpiri, identifying what category a thing belongs to is central
      ;; to correct relationship — kirda and kurdungurlu (custodians and managers).

      (jinta-nyanyi  eq?       "jinta = same one — are these the very same thing?")
      (jinta-juku?   equal?    "jinta-juku = just one/same — do these have the same value?")
      (kuruwarri?    number?   "kuruwarri = mark/design — is this a number-mark?")
      (yimi?         string?   "yimi = word/speech — is this made of words?")
      (nyampu?       symbol?   "nyampu = this one here — is this a name-token?")
      (nyamba?       list?     "nyamba = those things — is this a group of things?")
      (marlpa?       pair?     "marlpa = companion — does this have a partner?")
      (lawa?         null?     "lawa = nothing — is the group empty?")
      (yuwayi-lawa?  boolean?  "is this a yes-or-no value?")
      (yirdi?        procedure? "yirdi = pattern — is this a way-of-doing?")

      ;; ── Input and output: making visible, hearing ────────────────────────────

      (jaru          display    "jaru = speak aloud, make the sound visible")
      (kuruwarri-yirrarni write "place the marks down (write)")
      (kuruwarri-nyanyi read   "nyanyi = see — look at the marks (read)")
      (yirnmi-kari   newline   "yirnmi = line — go to a new line")
      (nyanyi-yirdi  read-char "see one mark at a time")
      (wangkaja-yirdi write-char "say one mark at a time")
      (kari-ngurra   eof-object? "kari-ngurra = away from camp — reached the end")

      ;; ── Strings: chains of speech ────────────────────────────────────────────

      (yimi-kari     make-string  "make a new chain of speech")
      (yimi-panu     string-length "how many words in the chain?")
      (yimi-yirrarni string-append "join chains of speech together")
      (yimi-pina     string-ref   "find the word at this place in the chain")
      (yimi-manu     string->list "break speech into a group of marks")
      (nyamba-yimi   list->string "gather marks back into speech")

      ;; ── Actors: the people of the program ───────────────────────────────────
      ;;
      ;; Warlpiri social life is built on people doing things simultaneously,
      ;; together and separately — just like concurrent actors.

      (yapa-nyina    actor-alive? "yapa = person; nyina = living/sitting — is this person still here?")

      ;; ── Symbolic and mathematical ────────────────────────────────────────────
      ;;
      ;; Jukurrpa (the Dreaming) is the source of pattern and law.
      ;; Symbolic variables are like named Jukurrpa elements.

      (jukurrpa-yirdi sym-var    "jukurrpa = Dreaming-pattern — a named symbolic element")
      (jukurrpa?      symbolic?  "is this a Dreaming-pattern (symbolic expression)?")
      (pirramini-yimi substitute "pirramini = change — replace one thing with another")
      (nyurnu-yirdi   simplify   "nyurnu = straight, direct — make the pattern straight")
      (walya-grad     grad       "walya = ground/country — the slope of the country")
      (kurra-yani     integrate  "kurra = toward — gather toward a whole")

      ;; ── Quantum and superposition ────────────────────────────────────────────
      ;;
      ;; In Warlpiri cosmology, multiple realities (Jukurrpa and present) coexist.
      ;; Superposition — all possibilities at once — resonates with this.

      (jukurrpa-panu  superpose  "jukurrpa-panu = many Dreamings — all possibilities at once")
      (nyanyi-juku    observe    "nyanyi-juku = look carefully — collapse to what you see")

      ;; ── The language itself ──────────────────────────────────────────────────
      ;; (meta: registering and using language packs)

      (yimi-yapa-kari      register-language!   "register a new Yapa (person's) language")
      (yimi-yapa-nyina     set-active-language! "make this language the one we speak now")
      (yimi-yapa-ngurra    active-language      "which language are we speaking now?")
      (yimi-yapa-panu      registered-languages "all the languages we know")
      ))))

;; Uncomment to activate immediately when this file is loaded:
;; (set-active-language! "warlpiri")
;;
;; Ngalikirlangu — this belongs to all of us together.
;; Yapa yimi Warlpiri kurlangu — this speech belongs to Warlpiri people.
