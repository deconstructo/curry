/*
 * akkadian_names.h — Three-language name table for Curry Scheme.
 *
 * Every built-in name is available in:
 *   1. English (standard R7RS)        define, lambda, car, +, ...
 *   2. Transliterated Akkadian        šakānum, epēšum, rēšum, matāḫum, ...
 *   3. Cuneiform Akkadian             𒁹, 𒇽, 𒊕, 𒋻, ...
 *
 * Usage:
 *   #define AKK(english, translit, cunei) ...your expansion...
 *   #include "akkadian_names.h"
 *   #undef AKK
 *
 * AKK_SF marks special forms (need eval.c translation).
 * AKK_PR marks procedures (registered as env aliases in builtins.c).
 * Both macros fall through to AKK if only AKK is defined.
 *
 * Cuneiform signs (UTF-8):
 *   𒀭 U+12009 AN  — divine/sky  (error prefix, not reused here)
 *   𒀸 U+12038 AŠ2 — one/first
 *   𒀀 U+12000 A   — water/for
 *   𒁀 U+12040 BA  — to give/half
 *   𒁹 U+12079 DIŠ — single stroke, to mark
 *   𒂍 U+1208D E2  — house (for define-library)
 *   𒂗 U+12097 EN  — lord/to enter
 *   𒃲 U+120F2 GAL — great/big
 *   𒄀 U+12100 GI  — to return/establish
 *   𒄿 U+1213F I   — to go
 *   𒅆 U+12146 IGI — eye/to look
 *   𒅁 U+12141 IB  — to hold
 *   𒆜 U+1219C KUN — tail
 *   𒆠 U+121A0 KI  — earth/ground
 *   𒇲 U+121F2 LAL — to bind/subtract
 *   𒇽 U+121FD LU2 — person/agent
 *   𒈠 U+12220 MA  — boat/give
 *   𒈧 U+12227 MAŠ — half/to divide
 *   𒈷 U+12237 ME  — essence/to be
 *   𒉡 U+12261 NU  — negation
 *   𒊕 U+12295 SAG — head
 *   𒊻 U+122BB ŠE  — grain/to enter
 *   𒋻 U+1256B TAR — to cut/decide
 *   𒌋 U+1260B U   — 10/and
 *   𒌑 U+12611 UD  — sun/day/when
 *   𒌝 U+1261D UM  — tablet/to write
 *   𒍪 U+12369 ZA  — stone/self
 */

#ifndef AKK_SF
#  define AKK_SF AKK
#endif
#ifndef AKK_PR
#  define AKK_PR AKK
#endif

/* ---- Special forms ---- */

AKK_SF("define",          "šakānum",           "𒁹")
AKK_SF("lambda",          "epēšum",            "𒇽")
AKK_SF("if",              "šumma",             "𒋗𒈠")   /* phonetic ŠU.MA */
AKK_SF("begin",           "ištartu",           "𒀸")
AKK_SF("set!",            "šanûm",             "𒁀𒀀")   /* BA.A = to change */
AKK_SF("let",             "leqûm",             "𒅁")
AKK_SF("let*",            "leqûm-watrum",      "𒅁𒌋")   /* IB.U = sequential */
AKK_SF("letrec",          "leqûm-tadārum",     "𒅁𒄀")   /* IB.GI = mutual */
AKK_SF("letrec*",         "leqûm-tadārum-w",   "𒅁𒄀𒌋")
AKK_SF("quote",           "kīma",              "𒆠𒈠")   /* KI.MA = as-it-is */
AKK_SF("quasiquote",      "kīma-libbi",        "𒆠𒈠𒅁")
AKK_SF("unquote",         "pašārum",           "𒉡𒆠")   /* NU.KI = un-place */
AKK_SF("unquote-splicing","pašārum-šapārum",   "𒉡𒆠𒊕")
AKK_SF("and",             "u",                 "𒌋")     /* U = the conjunction! */
AKK_SF("or",              "lū",                "𒇻")     /* LU */
AKK_SF("cond",            "šumma-ribûm",       "𒋗")     /* ŠU = hand/choice */
AKK_SF("case",            "ana",               "𒀀𒈾")   /* A.NA = for/to */
AKK_SF("when",            "inūma",             "𒌑")     /* UD = time/when! */
AKK_SF("unless",          "lā-inūma",          "𒉡𒌑")   /* NU.UD = not-when */
AKK_SF("do",              "alākum",            "𒄿")     /* I = to go */
AKK_SF("define-syntax",   "šakānum-ṭupšarrim", "𒁹𒌝")   /* DIŠ.UM */
AKK_SF("syntax-rules",    "ṭupšarrūtum",       "𒌝𒌋")   /* UM.U */
AKK_SF("define-values",   "šakānum-nikkassī",  "𒁹𒈷")   /* DIŠ.ME */
AKK_SF("define-record-type","šakānum-ṣimtim",  "𒁹𒋻")   /* DIŠ.TAR */
AKK_SF("values",          "nikkassū",          "𒈷")     /* ME = essence/values */
AKK_SF("call/cc",         "riksum",            "𒇲𒁹")   /* LAL.DIŠ = binding */
AKK_SF("call-with-current-continuation", "riksum-dannum", "𒇲𒁹𒃲")
AKK_SF("import",          "erēbum",            "𒂗")     /* EN = lord/enter */
AKK_SF("export",          "waṣûm",             "𒂗𒉡")   /* EN.NU = exit */
AKK_SF("define-library",  "bīt-ṭuppi",         "𒂍𒌝")   /* E2.UM = house of tablets! */
AKK_SF("guard",           "naṣārum",           "𒆠𒂗")   /* KI.EN = ground-lord */
AKK_SF("parameterize",    "šīmtum",            "𒁹𒆠")
AKK_SF("include",         "qebûm",             "𒁀𒌝")
AKK_SF("delay",           "naṭālum-arkûm",     "𒌑𒀸")   /* UD.AŠ2 = time-first */
AKK_SF("spawn",           "wālādum",           "𒅁𒀀")   /* IB.A = to beget */
AKK_SF("send!",           "šapārum",           "𒌝")     /* UM = tablet = letter! */
AKK_SF("receive",         "maḫārum",           "𒈠𒄭")   /* MA.ḪI = to receive */

/* ---- Procedures: pairs and lists ---- */

AKK_PR("cons",            "rakāsum",           "𒇲")     /* LAL = to bind */
AKK_PR("car",             "rēšum",             "𒊕")     /* SAG = head! */
AKK_PR("cdr",             "zibbatum",          "𒆜")     /* KUN = tail! */
AKK_PR("set-car!",        "šakān-rēšim",       "𒁹𒊕")   /* place the head (šakānum, reused) */
AKK_PR("set-cdr!",        "šakān-zibbatim",    "𒁹𒆜")   /* place the tail */
AKK_PR("list",            "nindabûm",          "𒄿𒌝")   /* I.UM = proceeding tablets */
AKK_PR("length",          "mīnum",             "𒈠𒈾")   /* MA.NA = mana (weight/measure) */
AKK_PR("append",          "redûm",             "𒈠𒂗")   /* MA.EN = follow-on */
AKK_PR("reverse",         "turrum",            "𒋻𒀀")   /* TAR.A = to turn */
AKK_PR("list-ref",        "nindabûm-maḫārum",  "𒌝𒊕")
AKK_PR("list-tail",       "nindabûm-zibbat",   "𒌝𒆜")
AKK_PR("map",             "šutakūlum-nindabî", "𒈷𒅆")   /* ME.IGI = see-essence = map */
AKK_PR("for-each",        "ana-kālāma",        "𒀀𒈾𒆠") /* A.NA.KI = for-all */
AKK_PR("filter",          "ṣêrum",             "𒋻")     /* TAR = to cut/select */
AKK_PR("fold-left",       "lapātum-šumēlam",   "𒇲𒆠")
AKK_PR("fold-right",      "lapātum-imittam",   "𒇲𒌋")
AKK_PR("assoc",           "ṭuppum-maḫārum",    "𒌝𒈠𒁹")  /* UM.MA.DIŠ */
AKK_PR("assq",            "ṭuppum-maḫārum-eq", "𒌝𒂗𒁹")  /* UM.EN.DIŠ */
AKK_PR("member",          "libbum-maḫārum",    "𒌝𒊕𒊕")
AKK_PR("null?",           "šūnum?",            "𒉡𒁹")   /* NU.DIŠ = not-one = empty */
AKK_PR("pair?",           "qitnūm?",           "𒇲𒇲")   /* LAL.LAL = bound pair */
AKK_PR("list?",           "nindabûm?",         "𒌝𒌝")

/* ---- Procedures: arithmetic ---- */

/* These actual terms appear in Old Babylonian mathematical tablets */
AKK_PR("+",               "matāḫum",           "𒋻𒁹")   /* TAR.DIŠ — addition */
AKK_PR("-",               "ḫarāṣum",           "𒇲𒌑")   /* LAL.UD — to reduce/cut */
AKK_PR("*",               "šutakūlum",         "𒈧𒁹")   /* MAŠ.DIŠ — multiplication (Babylonian term!) */
AKK_PR("/",               "zâzum",             "𒈧")     /* MAŠ — to halve/divide (Babylonian term!) */
AKK_PR("=",               "mitḫārum",          "𒈠𒋻")   /* MA.TAR — equal (Babylonian math term!) */
AKK_PR("<",               "ṣeḫērum",           "𒉡𒃲")   /* NU.GAL = not-great = lesser */
AKK_PR(">",               "rabûm",             "𒃲")     /* GAL = great! */
AKK_PR("<=",              "ṣeḫērum-mitḫārum",  "𒉡𒃲𒁹") /* not-great-or-equal */
AKK_PR(">=",              "rabûm-mitḫārum",    "𒃲𒁹")
AKK_PR("max",             "ašarēdum",          "𒃲𒃲")   /* GAL.GAL = greatest */
AKK_PR("min",             "ṣiḫrum",            "𒉡𒉡")   /* NU.NU = smallest */
AKK_PR("abs",             "kīttum",            "𒆠𒀸")   /* KI.AŠ2 = truth-one */
AKK_PR("zero?",           "ṣifrum?",           "𒉡𒉡𒁹") /* zero = ṣifrum, whence "cipher" */
AKK_PR("positive?",       "damqum?",           "𒃲𒌑")   /* GAL.UD = great-day = positive */
AKK_PR("negative?",       "lemnûm?",           "𒉡𒄿")   /* NU.I = not-going = negative */
AKK_PR("odd?",            "zûzum?",            "𒀸𒁹")
AKK_PR("even?",           "šinûm?",            "𒀸𒀸")
AKK_PR("floor",           "šaplûm",            "𒆠")     /* KI = ground/earth = floor! */
AKK_PR("ceiling",         "elûm",              "𒀭𒀸")   /* AN.AŠ2 = sky-one = ceiling */
AKK_PR("round",           "labārum",           "𒄀𒁹")
AKK_PR("truncate",        "ḫarāṣum-warkûm",    "𒋻𒇲")   /* TAR.LAL = cut-bind */
AKK_PR("expt",            "napḫarum",          "𒈷𒈷")   /* ME.ME = power */
AKK_PR("sqrt",            "ibum",              "𒅁𒁹")   /* IB.DIŠ — ibum = "the side" = square root in Babylonian math! */
AKK_PR("quotient",        "qātum",             "𒁀𒁹")
AKK_PR("remainder",       "šērum",             "𒊕𒌝")
AKK_PR("modulo",          "kippatum",          "𒄀𒌋")
AKK_PR("gcd",             "kabrum",            "𒃲𒁹𒁹")
AKK_PR("lcm",             "qallum",            "𒉡𒃲𒌑")  /* NU.GAL.UD = least common */
/* mitḫartum: "the equal-sided figure" — the canonical OB term for x², the area of
 * a square of side x as computed on mathematical tablets. */
AKK_PR("square",             "mitḫartum",          "𒈠𒋻𒁹")   /* MA.TAR.DIŠ = equal-cut-one = x² */
AKK_PR("exact-integer?",     "kinattu-nikkassum?", "𒆠𒋻?")    /* KI.TAR? = grounded-decided? = fixed whole & exact? */
AKK_PR("truncate-quotient",  "qātum-ḫarāṣim",      "𒁀𒋻𒁹")   /* BA.TAR.DIŠ = give-cut-one */
AKK_PR("truncate-remainder", "šērum-ḫarāṣim",      "𒊕𒋻")     /* SAG.TAR = head-cut */
AKK_PR("truncate/",          "ḫarāṣum-kala",       "𒋻𒈷𒈷")   /* TAR.ME.ME = cut-both = both results */
AKK_PR("exact-integer-sqrt", "ibum-kinattu",        "𒅁𒆠𒁹")   /* IB.KI.DIŠ = exact root (ibum = "the side" = OB square root) */
AKK_PR("exact",           "kinattu",           "𒆠𒋻")
AKK_PR("inexact",         "lā-kinattu",        "𒉡𒆠𒋻")
AKK_PR("number->string",  "nikkassum-ana-ṭuppi","𒈷𒌝")
AKK_PR("string->number",  "ṭuppum-ana-nikkassim","𒌝𒈷")
/* zikru (designation, reused) of the current numeric display mode
 * (decimal/sexagesimal/cuneiform notation). */
AKK_PR("current-number-notation", "zikru-nikkassi-inanna", "𒌋𒈷𒉡𒉡")

/* Transcendental */
AKK_PR("sin",             "šapaltu-ṣīrum",     "𒁹𒀸𒁹")
AKK_PR("cos",             "ašarēdum-ṣīrum",    "𒁹𒁹𒀸")
AKK_PR("exp",             "napḫarum-ṣīrum",    "𒈷𒁹𒀸")
AKK_PR("log",             "naṭālum-ṣīrum",     "𒅆𒁹")
AKK_PR("atan",            "šapaltu-ippeš",     "𒁹𒀸𒀸")

/* ---- Procedures: number theory ---- */
/* Old Babylonian scribes kept reciprocal tables (igûm/igibûm) and worked
 * extensively with fractions, shares (zittu), and surplus/deficit
 * accounting — genuine anchors for most of this section. A few entries
 * (Bernoulli/Euler/Stirling/Bell/Carmichael/Catalan numbers, the Jacobi/
 * Legendre/Kronecker symbols) are 18th-20th century constructs with no
 * Old Babylonian analog; those get an honestly-labeled evocative word,
 * the same treatment already given to quantum superposition (kalāma) and
 * surreal numbers (dāriš) elsewhere in this file. */

/* ēdum: "one, alone, sole" — a prime has no factors but itself and one. */
AKK_PR("prime?",          "ēdum?",             "𒁹𒉡?")   /* DIŠ.NU? = one-not[-divisible]? */
/* zittum: "share, portion" — the attested term for a division of property
 * or inheritance on OB legal tablets; a factor is the number's share. */
AKK_PR("factor",          "zittum",            "𒁀𒋻𒌑")   /* BA.TAR.UD = give-cut-day = a share */
AKK_PR("prime-factors",   "zittu-ēdûtim",      "𒁀𒋻𒉡")   /* BA.TAR.NU = the sole shares */
AKK_PR("divisors",        "zittū",             "𒁀𒋻𒌋")   /* BA.TAR.U = shares (plural) */
AKK_PR("divisor-count",   "mīnu-zittī",        "𒈠𒁀𒋻")   /* MA.BA.TAR = count of shares */
AKK_PR("num-divisors",    "mīnu-zittim",       "𒈠𒁀𒋻𒁹")  /* alias of divisor-count, distinct glyph required */
/* kamārum: "to gather, to heap up" — an OB accounting verb for summing. */
AKK_PR("divisor-sum",     "kamār-zittī",       "𒃲𒁀𒋻")   /* GAL.BA.TAR = great-gathering of shares */
AKK_PR("sum-divisors",    "kamāru-zittim",     "𒃲𒁀𒋻𒌋")  /* alias, distinct glyph required */
/* šalmum: "whole, complete, sound" — a perfect number equals the sum of
 * its proper divisors, "whole" in the same sense the word already covers. */
AKK_PR("perfect?",        "šalmum?",           "𒈠𒃲?")    /* MA.GAL? = greatly-whole? */
/* watrum: "surplus, excess" — an abundant number exceeds its divisor-sum. */
AKK_PR("abundant?",       "watrum?",           "𒃲𒌋?")    /* GAL.U? = great-and-more? */
/* muṭṭû: "deficient, lacking" — the opposite of watrum. */
AKK_PR("deficient?",      "muṭṭûm?",           "𒉡𒃲?")    /* NU.GAL? = not-great? = lacking */
AKK_PR("perfect-power?",  "šalmu-napḫarim?",   "𒈠𒈷?")    /* MA.ME? = wholly-powered? */
AKK_PR("squarefree?",     "lā-mitḫartim?",     "𒉡𒈠𒋻?")  /* NU.MA.TAR? = not-squared? */
/* damqum: "good, fine" — a smooth number has only small ("fine") prime
 * factors, unlike a "rough" number with a large prime factor. */
AKK_PR("smooth?",         "damqu-zittim?",     "𒌋𒁀𒋻?")   /* U.BA.TAR? = good-shared? */
/* ibru: "friend, companion, partner" — the totient counts the numbers
 * coprime to n, i.e. n's "partners" below it. */
AKK_PR("totient",         "mīnu-ibrī",         "𒈠𒅁𒁹")   /* MA.IB.DIŠ = count of partners */
/* têrtum: "omen, oracular sign" — the Möbius function's famously
 * unpredictable ±1/0 pattern is as good a fit for "omen" as any. */
AKK_PR("mobius",          "têrtum",            "𒂗𒉡𒁹")   /* EN.NU.DIŠ = the lordly single sign */
/* kabrum already denotes gcd; extended-gcd additionally returns the
 * Bézout coefficients — the gcd "with extras" (watrum, surplus). */
AKK_PR("extended-gcd",    "kabru-watrum",      "𒃲𒁹𒁹𒌋") /* GAL.DIŠ.DIŠ.U = gcd-and-more */
/* igûm: the actual OB term for a tabulated reciprocal (as in the
 * reciprocal tables scribes memorized); mod-inverse is exactly that,
 * generalized to modular arithmetic. */
AKK_PR("mod-inverse",     "igûm",              "𒌋𒃲𒌋")   /* U.GAL.U = the great reciprocal */
AKK_PR("mod-expt",        "kippat-napḫarim",   "𒄀𒌋𒈷")   /* GI.U.ME = modulo-power */
/* puḫrum: "assembly, gathering" — CRT gathers several modular constraints
 * into one; kippatu (modulo) already covers the individual constraint. */
AKK_PR("chinese-remainder","kippātu-puḫrum",   "𒄀𒌋𒅁𒌋") /* GI.U.IB.U = assembly of moduli */
/* têrtum family: successive generalizations of the quadratic-residue
 * "omen"-symbol (Legendre -> Jacobi -> Kronecker). */
AKK_PR("legendre-symbol", "têrtu-maḫrītum",    "𒂗𒉡𒁹𒁹") /* first/foremost omen */
AKK_PR("jacobi-symbol",   "têrtu-watartum",    "𒂗𒉡𒁹𒌋") /* extended omen */
AKK_PR("kronecker-symbol","têrtu-gamartum",    "𒂗𒉡𒁹𒃲") /* gamārum = to complete; the complete omen */
/* ḫepûm already denotes "rational" (to break/divide); continued fractions
 * and their convergents are repeated, successive breakings. */
AKK_PR("continued-fraction","ḫepû-šanûtum",    "𒇲𒁹𒌋𒌋")  /* repeated fraction */
AKK_PR("convergents",     "ḫepû-qerbūtum",     "𒇲𒁹𒂗𒉡") /* qerēbum = to approach; the approaching fractions */
AKK_PR("best-rational-approx","ḫepû-qerbum",   "𒇲𒁹𒂗")   /* the nearest fraction */
/* arkûm: "following, later" — the recurrence relation each term follows
 * from those before it. */
AKK_PR("fibonacci",       "arkiātum",          "𒀸𒋻𒁹")   /* AŠ2.TAR.DIŠ = the following-ones */
AKK_PR("lucas",           "arkiātu-eššum",     "𒀸𒋻𒌋")   /* eššu = new; the other following-sequence */
/* purussûm: "decision, verdict" — Catalan numbers count the ways to
 * parenthesize/decide an association order. */
AKK_PR("catalan",         "mīnu-purussî",      "𒈠𒁀𒋻𒃲") /* count of decisions */
/* sarrum: "false, lying" — Carmichael numbers pass Fermat's primality
 * test while being composite: false primes. */
AKK_PR("carmichael",      "ēdu-sarrum",        "𒁹𒉡𒃲")   /* DIŠ.NU.GAL = the lying sole-one */
/* puḫrum (assembly) again, split into the two classical kinds. */
AKK_PR("stirling1",       "puḫur-maḫrûm",      "𒅁𒌋𒃲")   /* first assembly */
AKK_PR("stirling2",       "puḫur-arkûm",       "𒅁𒌋𒉡")   /* later/second assembly */
/* ḫisbu: "allotment, share-count" — a genuine administrative term for
 * counted-out portions, i.e. the number of ways to choose a share. */
AKK_PR("binomial",        "ḫisbum",            "𒅁𒉡𒃲")   /* the allotment */
AKK_PR("multinomial",     "ḫisbu-kalāma",      "𒅁𒉡𒃲𒌋") /* kalāma = everything; the allotment of everything */
AKK_PR("partition-count", "puḫur-mala",        "𒅁𒌋𒉡𒌋") /* mala = "as much as"; assembly of however-many */
AKK_PR("next-prime",      "ēdu-arkûm",         "𒁹𒉡𒉡")   /* the next sole-one */
AKK_PR("prev-prime",      "ēdu-maḫrûm",        "𒁹𒉡𒃲𒉡") /* the former sole-one */
/* Bernoulli/Euler numbers: genuinely modern, no OB analog. Reusing
 * minûtu (an abstract "reckoning/count") flags them honestly as named
 * numeric sequences rather than claiming false ancient pedigree. */
AKK_PR("bernoulli",       "minûtu-maḫrītum",   "𒈠𒈾𒁹")   /* the first reckoning-sequence */
AKK_PR("euler-number",    "minûtu-šanītum",    "𒈠𒈾𒌋")   /* the second reckoning-sequence */
AKK_PR("bell",            "puḫur-kalāma",      "𒅁𒌋𒌋𒉡") /* assembly-of-everything (set partitions) */
AKK_PR("big-omega",       "mīnu-zitti-kalāma", "𒈠𒁀𒋻𒉡") /* count of ALL prime shares, with multiplicity */
AKK_PR("omega",           "mīnu-zitti-ēdûtim", "𒈠𒁀𒋻𒌋𒉡") /* count of DISTINCT prime shares */

/* ---- Procedures: type predicates ---- */

AKK_PR("number?",         "nikkassum?",        "𒈷?")    /* ME? = is essence? */
AKK_PR("string?",         "ṭupšarrum?",        "𒌝?")    /* UM? = is tablet? */
AKK_PR("symbol?",         "šumum?",            "𒁹𒌝?")
AKK_PR("boolean?",        "kēnum?",            "𒆠𒀸?")
AKK_PR("procedure?",      "pārisum?",          "𒇽?")    /* LU2? = is person/agent? */
AKK_PR("vector?",         "nindabûm-šupur?",   "𒌝𒀸?")
AKK_PR("char?",           "ṣibtum?",           "𒁹?")
AKK_PR("port?",           "bābum?",            "𒂍?")    /* E2? = is gate? */
AKK_PR("exact?",          "kinattu?",          "𒆠?")
AKK_PR("inexact?",        "lā-kinattu?",       "𒉡𒆠?")
AKK_PR("integer?",        "nikkassum-šalim?",  "𒈷𒀸?")
AKK_PR("rational?",       "ḫepûm?",            "𒈷𒁹?")
AKK_PR("real?",           "ṣīrum?",            "𒈷𒀀?")
AKK_PR("complex?",        "išārum?",           "𒈷𒌝?")
AKK_PR("eq?",             "mitḫārum-eq?",      "𒂗𒂗?")  /* EN.EN? = same lord? */
AKK_PR("eqv?",            "mitḫārum-eqv?",     "𒈠𒈠?")
AKK_PR("equal?",          "mitḫārum-šalim?",   "𒈠𒋻?")

/* ---- Procedures: I/O ---- */

AKK_PR("display",         "naṭālum",           "𒅆")     /* IGI = eye/to look! */
AKK_PR("write",           "šaṭārum",           "𒌝𒁹")   /* UM.DIŠ = write-mark */
AKK_PR("newline",         "pirištu",           "𒁹𒁹𒁹") /* DIŠ.DIŠ.DIŠ = new line mark */
AKK_PR("read",            "šemûm",             "𒅆𒀸")   /* IGI.AŠ2 = to look-read */
AKK_PR("read-line",       "šemûm-ašrum",       "𒅆𒌋")
AKK_PR("read-char",       "šemûm-ṣibtum",      "𒅆𒁀")   /* IGI.BA = see-give = read one */
AKK_PR("write-char",      "šaṭārum-ṣibtum",    "𒌝𒅆")
AKK_PR("write-string",    "šaṭārum-ṭuppam",    "𒌝𒄀")   /* UM.GI = write-establish (was miscopied from read-string) */
AKK_PR("open-input-file", "petûm-ṭuppi-erēbim","𒂍𒂗")
AKK_PR("open-output-file","petûm-ṭuppi-waṣîm", "𒂍𒉡")
AKK_PR("close-port",      "sakārum",           "𒂍𒇲")   /* E2.LAL = bind the house */
AKK_PR("flush-output-port","pašārum-bābim",    "𒂍𒁀")
AKK_PR("eof-object?",     "qātum?",            "𒉡𒌝𒀸?")  /* NU.UM.AŠ2 = no-more-tablet-first? */
AKK_PR("char-ready?",           "ṣibtum-maḫrum?",             "𒅆𒁀?")    /* IGI.BA? = seen-ready? */
AKK_PR("u8-ready?",             "ṣibtum-riqqi-maḫrum?",       "𒅆𒁹?")    /* IGI.DIŠ? = raw-byte ready? */
AKK_PR("read-u8",               "šemûm-ṣibtum-riqqi",         "𒅆𒁀𒁹")   /* IGI.BA.DIŠ = read one raw byte */
AKK_PR("peek-u8",               "naṭālum-ṣibtum-riqqi",       "𒅆𒉡𒁹")   /* IGI.NU.DIŠ = look-not-one = peek without consuming */
AKK_PR("read-string",           "šemûm-ṭuppam",               "𒅆𒌝")     /* IGI.UM = read tablet */
AKK_PR("read-bytevector",       "šemûm-ṭuppi-ṣibtātim",       "𒅆𒌝𒁀")   /* IGI.UM.BA = read byte-tablet */
AKK_PR("read-bytevector!",      "šemûm-ṭuppi-ṣibtātim-ina",   "𒅆𒌝𒁀𒁹") /* IGI.UM.BA.DIŠ = read into existing */
AKK_PR("write-u8",              "šaṭārum-ṣibtum-riqqi",       "𒌝𒁀𒁹")   /* UM.BA.DIŠ = write one raw byte */
AKK_PR("write-bytevector",      "šaṭārum-ṭuppi-ṣibtātim",     "𒌝𒁀𒃲")   /* UM.BA.GAL = write-many-bytes */
AKK_PR("write-simple",          "šaṭārum-ṣīrum",              "𒌝𒄿𒀸")   /* UM.I.AŠ2 = write-going-plain = simple/non-recursive write */
AKK_PR("file-exists?",          "ṭuppum-ibašši?",             "𒂍𒀸?")    /* E2.AŠ2? = does the tablet-house exist? */
AKK_PR("delete-file",           "ḫepûm-ṭuppi",                "𒋻𒂍")     /* TAR.E2 = cut-house = destroy the tablet */
AKK_PR("call-with-input-file",  "šemûm-ina-ṭuppi",            "𒂍𒅆")     /* E2.IGI = file-read */
AKK_PR("call-with-output-file", "šaṭārum-ina-ṭuppi",          "𒌝𒁀𒂍")   /* UM.BA.E2 = write-into-house = file-write (distinct from define-library's E2.UM, which always wins in call position) */
AKK_PR("with-input-from-file",  "ina-ṭuppi-šemûm",            "𒂍𒅆𒁹")   /* E2.IGI.DIŠ = from-file-read-one */
AKK_PR("with-output-to-file",   "ana-ṭuppi-šaṭārum",          "𒂍𒌝𒁹")   /* E2.UM.DIŠ = to-file-write-one */

/* ---- Procedures: strings ---- */

AKK_PR("make-string",     "epēšum-ṭuppam",     "𒇽𒌝")
AKK_PR("string",          "ṭuppum",            "𒌑𒌋")   /* UD.U = the day's record = the written document */
AKK_PR("string-length",   "mīnum-ṭuppim",      "𒈠𒌝")
AKK_PR("string-ref",      "maḫārum-ṭuppim",    "𒌑𒊕")   /* UD.SAG = time-head */
AKK_PR("string-append",   "redûm-ṭuppim",      "𒌝𒄿")   /* UM.I = tablet-going */
AKK_PR("string-copy",     "šutur-ṭuppim",      "𒌝𒁹𒁹")
AKK_PR("substring",       "libbum-ṭuppim",     "𒌝𒅁")
AKK_PR("string->list",    "ṭuppum-ana-nindabî","𒌝𒇽")
AKK_PR("list->string",    "nindabûm-ana-ṭuppi","𒇽𒇽𒌝")  /* LU2.LU2.UM = many-to-tablet */
AKK_PR("string-upcase",   "elûm-ṭuppim",       "𒌝𒃲")
AKK_PR("string-downcase", "šaplûm-ṭuppim",     "𒌝𒆠")
AKK_PR("string=?",        "mitḫārum-ṭuppim?",  "𒌝𒈠?")
AKK_PR("string<?",        "ṣeḫērum-ṭuppim?",   "𒌝𒉡?")
AKK_PR("string->symbol",  "ṭuppum-ana-šumim",  "𒌝𒀸")   /* UM.AŠ2 = tablet-to-one */
AKK_PR("symbol->string",  "šumum-ana-ṭuppi",   "𒌋𒊕")   /* U.SAG = ten-head = name */
AKK_PR("string<=?",    "ṣeḫērum-mitḫārum-ṭuppim?",           "𒌝𒉡𒁹?")  /* UM.NU.DIŠ? = not-great-one-tablet? */
AKK_PR("string>?",     "rabûm-ṭuppim?",                        "𒌝𒃲?")    /* UM.GAL? = great-tablet? */
AKK_PR("string>=?",    "rabûm-mitḫārum-ṭuppim?",              "𒌝𒃲𒁹?")  /* UM.GAL.DIŠ? = great-one-tablet? */
/* mithāriš: "uniformly, indifferently" — treating upper and lower case as the same sign */
AKK_PR("string-ci=?",  "mithāriš-mitḫārum-ṭuppim?",           "𒈠𒌝𒈠?")  /* MA.UM.MA? = same-tablet-equal? */
AKK_PR("string-ci<?",  "mithāriš-ṣeḫērum-ṭuppim?",            "𒈠𒌝𒉡?")  /* MA.UM.NU? */
AKK_PR("string-ci>?",  "mithāriš-rabûm-ṭuppim?",              "𒈠𒌝𒃲?")  /* MA.UM.GAL? */
AKK_PR("string-ci<=?", "mithāriš-ṣeḫērum-mitḫārum-ṭuppim?",  "𒈠𒌝𒉡𒁹?") /* MA.UM.NU.DIŠ? */
AKK_PR("string-ci>=?", "mithāriš-rabûm-mitḫārum-ṭuppim?",     "𒈠𒌝𒃲𒁹?") /* MA.UM.GAL.DIŠ? */
AKK_PR("string-set!",  "šakānum-ṭuppim",                       "𒌝𒋻")     /* UM.TAR = tablet-set */
AKK_PR("string-copy!", "katābum-ṭuppim",                        "𒌝𒌑𒋻")   /* UM.UD.TAR = overwrite tablet */
AKK_PR("string-for-each","ana-kālāma-ṭuppim",                  "𒀀𒌝")     /* A.UM = for-tablet */
AKK_PR("string-fill!",  "malûm-ṭuppim",                        "𒌝𒌋𒁹")   /* UM.U.DIŠ = fill-tablet-one (malûm = to fill) */
/* string-foldcase: ṭuppum-mithāriš — render the tablet uniformly regardless of case */
AKK_PR("string-foldcase","ṭuppum-mithāriš",                    "𒌑𒌝")     /* UD.UM = time-tablet = folded tablet */
/* ṣibtātum: plural of ṣibtum (sign/character) — a sequence of raw byte-signs */
AKK_PR("string->utf8",  "ṭuppum-ana-ṣibtātim",                "𒌝𒁀")     /* UM.BA = tablet-to-bytes */
AKK_PR("utf8->string",  "ṣibtātum-ana-ṭuppi",                 "𒁀𒁹𒌝")   /* BA.DIŠ.UM = bytes-to-one-tablet (distinct from include's B.UM, which always wins in call position) */

/* ---- Procedures: vectors ---- */

AKK_PR("make-vector",     "epēšum-ṣindum",     "𒇽𒀸")
AKK_PR("vector",          "ṣindānum",          "𒀸𒌋")
AKK_PR("vector-length",   "mīnum-ṣindim",      "𒈠𒀸")
AKK_PR("vector-ref",      "maḫārum-ṣindim",    "𒀸𒊕")
AKK_PR("vector-set!",     "šakānum-ṣindim",    "𒀸𒋻")   /* AŠ2.TAR = one-cut = set */
AKK_PR("vector->list",    "ṣindānum-ana-nindabî","𒀸𒇽")
AKK_PR("list->vector",    "nindabûm-ana-ṣindim","𒇽𒇽𒀸")  /* LU2.LU2.AŠ2 = many-to-one */
AKK_PR("vector-fill!",    "malûm-ṣindim",      "𒀸𒌋𒁹")
AKK_PR("vector-copy",     "šutur-ṣindim",      "𒀸𒁹𒁹")
AKK_PR("vector-map",      "šutakūlum-ṣindim",  "𒈧𒀸")
AKK_PR("vector-for-each", "ana-kālāma-ṣindim", "𒀀𒀸")
AKK_PR("vector-append", "redûm-ṣindim",          "𒀸𒄿")     /* AŠ2.I = one-going = vector-continuing */
AKK_PR("vector-copy!",  "šutur-ṣindim-ina",       "𒀸𒄿𒁹")   /* AŠ2.I.DIŠ = one-going-into = copy into */

/* ---- Procedures: bytevectors ---- */

/* ṭuppi-ṣibtātim: "tablet of signs/bytes" — a fixed-length sequence of raw byte-values,
 * analogous to a clay tablet inscribed with a fixed number of cuneiform wedges. */
AKK_PR("make-bytevector",      "epēšum-ṭuppi-ṣibtātim",   "𒇽𒌝𒁀")   /* LU2.UM.BA = make byte-tablet */
AKK_PR("bytevector",           "ṭuppum-ṣibtātim",          "𒌑𒌝𒁀")   /* UD.UM.BA = the byte-tablet (constructor) */
AKK_PR("bytevector?",          "ṭuppum-ṣibtātim?",         "𒌑𒌝𒁀𒉡?")  /* is it a byte-tablet? */
AKK_PR("bytevector-length",    "mīnum-ṭuppi-ṣibtātim",     "𒈠𒌝𒁀")   /* MA.UM.BA = count of the byte-tablet */
AKK_PR("bytevector-u8-ref",    "maḫārum-ṭuppi-ṣibtātim",   "𒌝𒁀𒊕")   /* UM.BA.SAG = byte-tablet head/index */
AKK_PR("bytevector-u8-set!",   "šakānum-ṭuppi-ṣibtātim",   "𒌝𒁀𒋻")   /* UM.BA.TAR = set in byte-tablet */
AKK_PR("bytevector-copy",      "šutur-ṭuppi-ṣibtātim",     "𒌝𒁀𒁹𒁹") /* UM.BA.DIŠ.DIŠ = copy byte-tablet */
AKK_PR("bytevector-copy!",     "šutur-ṭuppi-ṣibtātim-ina", "𒌝𒁀𒄿𒁹") /* UM.BA.I.DIŠ = copy-into byte-tablet */
AKK_PR("bytevector-append",    "redûm-ṭuppi-ṣibtātim",     "𒌝𒁀𒄿")   /* UM.BA.I = byte-tablet-continuing */

/* ---- Procedures: characters ---- */

AKK_PR("char->integer",   "ṣibtum-ana-nikkassim","𒁀𒈷")   /* BA.ME = give essence */
AKK_PR("integer->char",   "nikkassum-ana-ṣibtim","𒈷𒁹")
/* Character comparators — ṣibtum (the sign/character) + comparator */
AKK_PR("char=?",      "mitḫārum-ṣibtim?",                "𒁀𒈠?")      /* BA.MA? = sign-equal? */
AKK_PR("char<?",      "ṣeḫērum-ṣibtim?",                 "𒁀𒉡𒃲?")    /* BA.NU.GAL? = sign-not-great? */
AKK_PR("char<=?",     "ṣeḫērum-ū-mitḫārum-ṣibtim?",      "𒁀𒉡𒃲𒁹?")  /* BA.NU.GAL.DIŠ? */
AKK_PR("char>?",      "rabûm-ṣibtim?",                    "𒁀𒃲?")      /* BA.GAL? = sign-great? */
AKK_PR("char>=?",     "rabûm-ū-mitḫārum-ṣibtim?",        "𒁀𒃲𒁹?")    /* BA.GAL.DIŠ? */
/* mithāriš: "uniformly" — case-insensitive = treating all forms of a sign as the same */
AKK_PR("char-ci=?",   "mithāriš-mitḫārum-ṣibtim?",       "𒁀𒈠𒈠?")    /* BA.MA.MA? = sign-uniform-equal? */
AKK_PR("char-ci<?",   "mithāriš-ṣeḫērum-ṣibtim?",        "𒁀𒈠𒉡𒃲?")  /* BA.MA.NU.GAL? */
AKK_PR("char-ci>?",   "mithāriš-rabûm-ṣibtim?",           "𒁀𒈠𒃲?")    /* BA.MA.GAL? */
AKK_PR("char-ci<=?",  "mithāriš-ṣeḫērum-mitḫārum-ṣibtim?","𒁀𒈠𒉡𒃲𒁹?")/* BA.MA.NU.GAL.DIŠ? */
AKK_PR("char-ci>=?",  "mithāriš-rabûm-mitḫārum-ṣibtim?",  "𒁀𒈠𒃲𒁹?")  /* BA.MA.GAL.DIŠ? */
/* digit-value: nikkassum-ša-ṣibtim — "the count of the sign" */
AKK_PR("digit-value", "nikkassum-ša-ṣibtim",               "𒈷𒁀")      /* ME.BA = essence of the sign */
AKK_PR("char-foldcase","ṣibtum-mithāriš",                  "𒁀𒈠")      /* BA.MA = sign-uniform */

/* ---- Procedures: booleans ---- */

AKK_PR("not",             "lā",                "𒉡")     /* NU = negation! */
AKK_PR("boolean=?",       "mitḫārum-kēnim?",   "𒆠𒆠?")

/* ---- Procedures: math utilities ---- */

AKK_PR("apply",           "paqādum",           "𒇽𒄿")   /* LU2.I = person-goes */
AKK_PR("error",           "ḫiṭītum",           "𒄷𒁹")   /* same root as akkadian.h */
AKK_PR("raise",           "našûm",             "𒃲𒁹𒌋")

/* ---- Procedures: actors ---- */

AKK_PR("spawn",           "wālādum",           "𒅁𒀀")
AKK_PR("send!",           "šapārum",           "𒌝𒂗")   /* UM.EN = send tablet */
AKK_PR("receive",         "maḫārum",           "𒌝𒈠")   /* UM.MA = receive tablet */
AKK_PR("self",            "ramānī",            "𒍪")     /* ZA = self/stone */
AKK_PR("actor-alive?",    "balāṭum?",          "𒅁𒃲?")

/* ---- Symbolic CAS ---- */

/* la-idûm: "the not-known" — the unknown quantity in O.B. algebraic tablets.
 * Scribes posed problems as "a thing I do not know; find it." */
AKK_PR("sym-var",         "la-idûm",             "𒉡𒅆")    /* NU.IGI = not-seen = the unknown */
AKK_PR("sym-var?",        "la-idûm?",            "𒉡𒅆?")
AKK_PR("sym-expr?",       "awât-la-idûm?",       "𒉡𒌝?")   /* NU.UM? = not-tablet? = unsettled */
AKK_PR("symbolic?",       "la-idûm-šalim?",      "𒉡𒅆𒁹?") /* NU.IGI.DIŠ? = unknown-or-expression */
AKK_PR("sym-var-name",    "šum-la-idûm",         "𒉡𒊕")    /* NU.SAG = not-seen-head = its name */

/* ṣimdat-la-idûm: "decree/constraint of the unknown" — tests whether a symbolic
 * variable carries a given assumption (real, positive, quaternion, …).
 * TAR = "to cut, to decide" — the scribe's determination of its nature. */
AKK_PR("sym-assumption?", "ṣimdat-la-idûm?",    "𒋻𒉡𒅆?") /* TAR.NU.IGI? = is this the decree of the unknown? */

/* māḫirum: "the going rate / exchange rate" — attested on O.B. commercial tablets
 * for the price of silver, grain, oil per unit.  As a derivative: the instantaneous
 * rate at which a quantity changes per unit of its variable. */
AKK_PR("sym-diff",        "māḫirum",             "𒄭𒊕")    /* ḪI.SAG = rate-head */
AKK_PR("frac-diff",       "māḫirum-ḫepûm",       "𒄭𒈠")    /* ḪI.MA  = halved-rate */
AKK_PR("wirtinger-d",     "māḫirum-išārum",      "𒄭𒁹")    /* ḪI.DIŠ = rate-one  (holomorphic ∂/∂z) */
AKK_PR("wirtinger-dbar",  "māḫirum-la",          "𒄭𒉡")    /* ḪI.NU  = rate-not  (anti-holomorphic ∂/∂z̄) */
AKK_PR("auto-diff",       "māḫirum-ramāni",      "𒄭𒍪")    /* ḪI.ZA  = rate-self (forward-mode via dual) */

/* eqlum: "field" — the canonical O.B. word for a measured area of land.
 * Mathematical tablets computed field areas as we compute integrals. */
AKK_PR("integrate",       "eqlum",               "𒀭𒆠")    /* AN.KI = sky-earth = the bounded field */
AKK_PR("frac-int",        "eqlum-ḫepûm",         "𒀭𒆠𒈠") /* AN.KI.MA = halved-field */

/* šuklulum: "to bring to completion, to make whole" — simplification
 * renders an expression into its most perfect/reduced form. */
AKK_PR("simplify",        "šuklulum",            "𒁹𒆠𒁹")  /* DIŠ.KI.DIŠ = one-earth-one */

/* nukkurum: "to alter, to make different" — exchange one thing for another. */
AKK_PR("substitute",      "nukkurum",            "𒁀𒋻")    /* BA.TAR = give-cut = exchange */

/* rapāšum: "to broaden, to widen, to spread out" — distribute products over sums. */
AKK_PR("expand",          "rapāšum",             "𒃲𒀀")    /* GAL.A = greatly-spread */

/* elûm-ṣīrum: "the highest ascent" — the degree is the topmost exponent. */
AKK_PR("degree",          "elûm-ṣīrum",          "𒀭𒈷")    /* AN.ME = sky-essence = the highest */

/* kânum: "to be firm, to establish" — collect gathers like terms into one place. */
AKK_PR("collect",         "kânum",               "𒆠𒁹𒁹")  /* KI.DIŠ.DIŠ = earth-gathered */

/* rēšum-nikkassī: "head of accounts" — the leading coefficient is the chief term. */
AKK_PR("leading-coeff",   "rēšum-nikkassī",      "𒊕𒈷")    /* SAG.ME = head-essence */

/* tawirtum: "image, reflection, likeness" — the complex conjugate is the mirror
 * image: same magnitude, reflected sign on the imaginary part. */
AKK_PR("conjugate",       "tawirtum",            "𒅆𒋻")    /* IGI.TAR = eye-cut = reflected */
AKK_PR("conj",            "tawirtum-ṣīrum",      "𒅆𒋻𒁹")  /* IGI.TAR.DIŠ = short conjugate */

/* ṭuppi-la-idûm: "tablet of the unknown" — rendering a CAS expression as text. */
AKK_PR("sym->string",     "ṭuppi-la-idûm",       "𒉡𒅆𒌝")  /* NU.IGI.UM */
AKK_PR("sym->infix",      "ṭuppi-la-idûm-išārum","𒉡𒅆𒌝𒌋") /* NU.IGI.UM.U = infix tablet */
AKK_PR("sym->latex",      "ṭuppi-ṣīrum-la-idûm", "𒉡𒅆𒌝𒁹") /* NU.IGI.UM.DIŠ = formal tablet */

/* ---- Remaining core builtins (src/builtins.c) ---- */

/* Trig: inverse forms via turrum (turn-back, reused); hyperbolic forms
 * via nabalkutu (to cross over, reused from image-flip); reciprocal
 * forms via igûm (the attested OB reciprocal-table term, reused). */
AKK_PR("acos",  "ašarēdum-turrum",  "𒁹𒁹𒀸𒄀")
AKK_PR("asin",  "šapaltu-turrum",   "𒁹𒀸𒁹𒄀")
AKK_PR("cosh",  "ašarēdu-nabalkutim", "𒁹𒁹𒀸𒉡𒁀")
AKK_PR("sinh",  "šapalti-nabalkutim", "𒁹𒀸𒁹𒉡𒁀")
AKK_PR("tanh",  "ippeš-nabalkutim",   "𒁹𒀸𒀸𒉡𒁀")
AKK_PR("acosh", "ašarēdu-nabalkuti-turrum", "𒁹𒁹𒀸𒉡𒁀𒄀")
AKK_PR("asinh", "šapalti-nabalkuti-turrum", "𒁹𒀸𒁹𒉡𒁀𒄀")
AKK_PR("atanh", "ippeš-nabalkuti-turrum",   "𒁹𒀸𒀸𒉡𒁀𒄀")
AKK_PR("tan",   "ippešum",          "𒁹𒀸𒀸𒉡")
AKK_PR("cot",   "igi-ippešim",      "𒌋𒃲𒁹𒀸𒀸")
AKK_PR("csc",   "igi-šapaltim",     "𒌋𒃲𒁹𒀸𒁹")
AKK_PR("sec",   "igi-ašarēdim",     "𒌋𒃲𒁹𒁹𒀸")

/* Bitwise: šutabalkutu (to keep shifting/rolling over, a genuine derived
 * verb form of nabalkutu, reused) for shift; ṣibtu (sign/character,
 * reused) stands in for "bit". */
AKK_PR("arithmetic-shift", "šutabalkut-ṣibtim", "𒉡𒁀𒁹𒁹")
AKK_PR("bitwise-and",      "kilallān-ṣibtim",   "𒌋𒁹𒁹")
AKK_PR("bitwise-or",       "ištēn-ṣibtim",      "𒀸𒁹𒁹𒁹")
AKK_PR("bitwise-xor",      "aḫu-ṣibtim",        "𒅁𒁹𒁹")
AKK_PR("bitwise-not",      "nakār-ṣibtim",      "𒉡𒄿𒁹𒁹")

/* c[ad]+r family: rēšum (head, reused) / zibbatum (tail, reused). */
AKK_PR("caar",  "rēš-rēšim",           "𒊕𒊕")
AKK_PR("cadr",  "rēš-zibbatim",        "𒊕𒆜")
AKK_PR("cdar",  "zibbat-rēšim",        "𒆜𒊕")
AKK_PR("cddr",  "zibbat-zibbatim",     "𒆜𒆜")
AKK_PR("caaar", "rēš-rēš-rēšim",       "𒊕𒊕𒊕𒊕")
AKK_PR("caadr", "rēš-rēš-zibbatim",   "𒊕𒊕𒆜")
AKK_PR("cadar", "rēš-zibbat-rēšim",   "𒊕𒆜𒊕")
AKK_PR("caddr", "rēš-zibbat-zibbatim","𒊕𒆜𒆜")
AKK_PR("cdaar", "zibbat-rēš-rēšim",   "𒆜𒊕𒊕")
AKK_PR("cdadr", "zibbat-rēš-zibbatim","𒆜𒊕𒆜")
AKK_PR("cddar", "zibbat-zibbat-rēšim","𒆜𒆜𒊕")
AKK_PR("cdddr", "zibbat-zibbat-zibbatim", "𒆜𒆜𒆜")
AKK_PR("memq",  "libbu-mitḫārum",     "𒉡𒈠𒁹𒁹𒁹")
AKK_PR("memv",  "libbu-kīnim",        "𒉡𒆠𒁹")
AKK_PR("assv",  "ṭuppum-maḫārum-kīnim", "𒌝𒈠𒆠")
AKK_PR("list*",      "nindabûm-warkûm",  "𒇽𒉡𒉡𒉡")
AKK_PR("list-copy",  "šutur-nindabîm",   "𒇽𒁹𒁹𒉡")
AKK_PR("list-head",  "mīnu-rēš-nindabîm","𒈠𒊕𒇽")
AKK_PR("list->set",  "nindabûm-ana-napḫarim", "𒇽𒀀𒈷𒈷")

/* Numeric misc: šaplu/elû (below/above, reused) split a fraction into
 * denominator/numerator; kinattu (exact, reused) negated round-trips
 * with inexact. */
AKK_PR("denominator",     "šaplu-ḫepîm",       "𒆠𒇲")
AKK_PR("numerator",       "elû-ḫepîm",         "𒀭𒇲")
AKK_PR("exact->inexact",  "kinattu-ana-lā-kinattim", "𒆠𒉡𒆠")
AKK_PR("inexact->exact",  "lā-kinattu-ana-kinattim", "𒉡𒆠𒀀𒆠")
AKK_PR("floor-quotient",  "šaplu-qātim",       "𒆠𒁀𒁹")
AKK_PR("floor-remainder", "šaplu-šērim",       "𒆠𒊕𒌝")
AKK_PR("floor/",          "šaplu-kalāma",      "𒆠𒉡𒉡")
/* išāru (complex, reused) — magnitude/angle/real/imaginary parts. */
AKK_PR("magnitude",       "ibu-išārim",        "𒅁𒁹𒌝𒉡")
AKK_PR("angle",           "idât-išārim",       "𒀸𒁹𒌝𒉡")
AKK_PR("real-part",       "ṣīru-išārim",       "𒀀𒌝𒉡")
AKK_PR("imag-part",       "lā-ṣīru-išārim",    "𒉡𒀀𒌝𒉡")
AKK_PR("make-rectangular","banû-išārim",       "𒉡𒌋𒌝𒉡")
AKK_PR("make-polar",      "banû-išāri-idātim", "𒉡𒌋𒌝𒉡𒀸")
AKK_PR("finite?",         "gamrum?",           "𒃲𒉡𒉡?")
AKK_PR("infinite?",       "dāriš-nikkassim?",  "𒀭𒀭𒈷?")
AKK_PR("nan?",            "lā-nikkassum?",     "𒉡𒈷𒉡?")

/* Quaternion arithmetic: rebûm (fourfold, reused); components ordered
 * by genuine cardinal ordinals (maḫrûm/šanûm/šalšum first-third, reused;
 * the fourth reuses rebûm itself, the "fourfold" word doing double duty
 * as both the type name and its own last component). */
AKK_PR("quaternion+",              "matāḫ-rebîm",       "𒂗𒌋𒁹")
AKK_PR("quaternion*",              "šutakūl-rebîm",     "𒈧𒁹𒌋𒁹")
AKK_PR("quaternion-conjugate",     "tawirtu-rebîm",     "𒅆𒋻𒌋𒁹")
AKK_PR("quaternion-inverse",       "igi-rebîm",         "𒌋𒃲𒌋𒁹")
AKK_PR("quaternion-norm",          "ibu-napḫar-rebîm",  "𒅁𒁹𒈷𒌋𒁹")
AKK_PR("quaternion-normalize",     "ibu-ṣīr-rebîm",     "𒅁𒁹𒀸𒌋𒁹")
AKK_PR("quaternion-rotate-vector", "saḫār-rebîm",       "𒀭𒀸𒄷𒌋𒁹")
AKK_PR("quaternion-w", "rebû-maḫrûm", "𒌋𒁹𒉌")
AKK_PR("quaternion-x", "rebû-šanûm",  "𒌋𒁹𒁀")
AKK_PR("quaternion-y", "rebû-šalšum", "𒌋𒁹𒋻")
AKK_PR("quaternion-z", "rebû-rebîm",  "𒌋𒁹𒌋")

/* Char predicates/case: ṣibtu (sign/character, reused). */
AKK_PR("char-alphabetic?",  "šiṭir-ṣibtim?",  "𒌝𒁹𒁹?")
AKK_PR("char-numeric?",     "nikkassu-ṣibtim?","𒈷𒁹𒁹?")
/* rīqu: "empty, void" — genuine adjective. */
AKK_PR("char-whitespace?",  "rīqu-ṣibtim?",   "𒉡𒁹𒁹?")
AKK_PR("char-upcase",       "elû-ṣibtim",     "𒀭𒁹𒁹")
AKK_PR("char-downcase",     "šaplû-ṣibtim",   "𒆠𒁹𒁹𒁹")
AKK_PR("char-upper-case?",  "elû-ṣibtim?",    "𒀭𒁹𒁹?")
AKK_PR("char-lower-case?",  "šaplû-ṣibtim?",  "𒆠𒁹𒁹?")

/* Ports: bābu (gate, reused) for a port; petûm/sakārum (open/close,
 * reused) for lifecycle. */
AKK_PR("current-input-port",  "bāb-šemîm",      "𒂍𒅆𒌝")
AKK_PR("current-output-port", "bāb-šaṭārim",    "𒂍𒌝𒁹𒁹")
AKK_PR("current-error-port",  "bāb-ḫiṭītim",    "𒂍𒄷𒁹")
AKK_PR("close-input-port",    "sakār-bāb-šemîm","𒂍𒂍𒅆𒌝")
AKK_PR("close-output-port",   "sakār-bāb-šaṭārim","𒂍𒂍𒌝𒁹𒁹")
AKK_PR("input-port?",         "bāb-šemîm?",     "𒂍𒅆𒌝?")
AKK_PR("output-port?",        "bāb-šaṭārim?",   "𒂍𒌝𒁹𒁹?")
AKK_PR("input-port-open?",    "petê-bāb-šemîm?","𒂍𒂍𒅆𒌝?")
AKK_PR("output-port-open?",   "petê-bāb-šaṭārim?","𒂍𒂍𒌝𒁹?")
AKK_PR("call-with-port",      "epēš-ina-bābim", "𒇽𒂍𒉌")
AKK_PR("open-input-string",   "petû-bāb-ṭuppim",     "𒂍𒂍𒌝")
AKK_PR("open-output-string",  "petû-bāb-šaṭāri-ṭuppim","𒂍𒂍𒌝𒁹")
AKK_PR("open-input-bytevector",  "petû-bāb-ṣibtātim",  "𒂍𒂍𒁀")
AKK_PR("open-output-bytevector", "petû-bāb-šaṭāri-ṣibtātim","𒂍𒂍𒌝𒁀")
AKK_PR("get-output-string",      "leqû-bāb-ṭuppim",    "𒅁𒂍𒌝")
AKK_PR("get-output-bytevector",  "leqû-bāb-ṣibtātim",  "𒅁𒂍𒁀𒁀")
AKK_PR("eof-object",   "qātu-gamrum",     "𒉡𒌝𒃲")
AKK_PR("peek-char",    "naṭāl-ṣibtim",    "𒅆𒉡𒁹𒁹")
AKK_PR("with-output-to-string", "ana-ṭuppi-šaṭāru-libbim", "𒂍𒌝𒁹𒉡")
/* riksu (bond/link, reused) — write-shared preserves the shared structure. */
AKK_PR("write-shared", "šaṭāru-riksim",   "𒌝𒁹𒇲𒁹")

/* Hash tables: puḫur šumim, "assembly of names" (both roots reused). */
AKK_PR("make-hash-table",     "epēš-puḫur-šumim", "𒇽𒉌𒌋𒉌")
AKK_PR("hash-table?",         "puḫur-šumim?",     "𒉌𒌋?")
AKK_PR("hash-table-set!",     "šakān-puḫur-šumim","𒁹𒉌𒌋")
AKK_PR("hash-table-ref",      "maḫār-puḫur-šumim","𒌝𒉌𒌋𒉌")
AKK_PR("hash-table-delete!",  "nasāḫ-puḫur-šumim","𒋻𒉌𒌋")
AKK_PR("hash-table-exists?",  "bašû-puḫur-šumim?","𒀸𒉌𒌋?")
AKK_PR("hash-table-keys",     "šumū-puḫur-šumim", "𒌋𒉌𒌋")
AKK_PR("hash-table-values",   "ṭuppū-puḫur-šumim","𒌝𒉌𒌋𒉡𒉌")
AKK_PR("hash-table-size",     "mīnu-puḫur-šumim", "𒈠𒉌𒌋𒉌")
AKK_PR("hash-table->alist",   "puḫur-šumi-ana-ṭuppim", "𒉌𒌋𒀀𒌝")

/* Sets (SRFI 113): napḫaru, "the gathered whole" — genuine, distinct
 * from puḫru (used above for hash tables and redis) to keep this
 * separate generic-set abstraction unambiguous. */
AKK_PR("make-set",         "epēš-napḫarim",     "𒇽𒈷𒈷")
AKK_PR("set->list",        "napḫarum-ana-nindabîm", "𒈷𒈷𒀀𒇽")
AKK_PR("set?",             "napḫarum?",         "𒈷𒈷?")
AKK_PR("set-empty?",       "rīqu-napḫarim?",    "𒉡𒈷𒈷𒌋?")
AKK_PR("set-size",         "mīnu-napḫarim",     "𒈠𒈷𒈷")
AKK_PR("set-count",        "mīnu-napḫari-šanûm","𒈠𒈷𒈷𒁀")
AKK_PR("set-member?",      "libbu-napḫarim?",   "𒉡𒈷𒈷?")
AKK_PR("set-add!",         "šakān-napḫarim",    "𒁹𒈷𒈷")
AKK_PR("set-adjoin",       "šakān-napḫari-šanûm",  "𒁹𒈷𒈷𒁀")
AKK_PR("set-adjoin!",      "šakān-napḫari-šalšum", "𒁹𒈷𒈷𒋻")
AKK_PR("set-delete",       "nasāḫ-napḫarim",       "𒋻𒈷𒈷𒌋")
AKK_PR("set-delete!",      "nasāḫ-napḫari-šanûm",  "𒋻𒈷𒈷𒁀𒉌")
AKK_PR("set-copy",         "šutur-napḫarim",    "𒁹𒁹𒈷𒈷")
AKK_PR("set-union",        "kamār-napḫarim",    "𒃲𒈷𒈷")
AKK_PR("set-intersection", "libbu-kilallān-napḫarim", "𒉡𒌋𒈷𒈷")
AKK_PR("set-difference",   "ḫarāṣ-napḫarim",    "𒇲𒌑𒈷𒈷")
AKK_PR("set-symmetric-difference", "aḫu-napḫarim", "𒅁𒈷𒈷𒉌")
AKK_PR("set-subset?",      "qerbu-napḫarim?",   "𒂗𒈷𒈷?")
AKK_PR("set=?",            "mitḫāru-napḫarim?", "𒈠𒋻𒈷𒈷?")
AKK_PR("set-any?",         "ištēn-napḫarim?",   "𒀸𒈷𒈷?")
AKK_PR("set-every?",       "gabbu-napḫarim?",   "𒃲𒉡𒈷𒈷?")
AKK_PR("set-find",         "šeʾû-napḫarim",     "𒅆𒅁𒈷𒈷")
AKK_PR("set-filter",       "ṣêru-napḫarim",     "𒋻𒈷𒈷𒉌")
AKK_PR("set-filter!",      "ṣêru-napḫari-šanûm","𒋻𒈷𒈷𒁀")
AKK_PR("set-fold",         "puḫur-napḫarim",    "𒉌𒈷𒈷")
AKK_PR("set-for-each",     "ana-kālāma-napḫarim", "𒀀𒌋𒈷𒈷")
AKK_PR("set-map",          "epēš-kalāma-napḫarim","𒇽𒉡𒈷𒈷")

/* Actors: wālidum, agent noun "the begotten one" from wālādum (to beget,
 * reused from spawn). */
AKK_PR("actor?",           "wālidum?",       "𒅁𒀀?")
AKK_PR("actor-id",         "šumu-wālidim",   "𒌋𒅁𒀀")
AKK_PR("actor-set-name!",  "šakān-šumi-wālidim","𒁹𒌋𒅁𒀀")
AKK_PR("actor-stats",      "ṭēm-wālidim",    "𒅆𒅁𒀀")

/* Error objects: ḫiṭītu (fault, reused). */
AKK_PR("error-object?",           "ḫiṭītu-awātim?", "𒄷𒀸𒁹?")
AKK_PR("error-object-irritants",  "zumur-ḫiṭītim",  "𒄷𒂍𒉡")   /* zumru, reused from redis-smembers */
AKK_PR("error-object-code",       "zikru-awāt-ḫiṭītim", "𒌋𒄷𒀸")
AKK_PR("error-object->string",    "ṭuppi-ḫiṭītim",  "𒉡𒅆𒄷")
/* labīru: "old, ancient, former" — genuine, marking this as the legacy form. */
AKK_PR("error-message",           "awāt-ḫiṭīti-labīrtim", "𒄷𒌝𒉡")

/* Control flow. */
AKK_PR("call-with-values", "paqād-nikkassī", "𒇽𒄿𒈷𒈷")
/* lawûm: "to surround, to encircle" — genuine, an exact fit for wind-
 * before/after wrapping a body. */
AKK_PR("dynamic-wind",  "lawûm",          "𒇲𒀀𒌋")
AKK_PR("eval",          "epēš-awātim",    "𒇽𒀸𒁹")
AKK_PR("exit",          "waṣû-gamrum",    "𒉡𒉡𒃲")
AKK_PR("quit",          "waṣû-ḫamṭum",    "𒉡𒉡𒉡")
/* šūpûm: "to reveal, to make manifest" — genuine, forcing a promise
 * reveals its value. qibītu: "utterance, command, promise" — genuine
 * noun, an actual word for "promise". */
AKK_PR("force",         "šūpûm",          "𒉡𒅆𒉡")
AKK_PR("make-promise",  "epēš-qibītim",   "𒇽𒁹𒉡")
AKK_PR("promise?",      "qibītum?",       "𒁹𒉡𒉡?")
AKK_PR("gensym",        "banû-šumi-la-idîm", "𒉡𒌋𒌋𒅆")
/* mātu: "land, country" — genuine, "the interior of the land" for the
 * current environment. */
AKK_PR("interaction-environment", "libbu-mātim", "𒉡𒈠𒌝")
AKK_PR("load",          "šemû-ṭuppim",    "𒅆𒌝𒉌")
/* šīmtu (fate/determined value, reused from redis-ttl) — a parameter
 * object holds a dynamically-scoped "fate". */
AKK_PR("make-parameter", "epēš-šīmtim",   "𒇽𒁹𒌑")
AKK_PR("raise-continuable", "našû-turrum", "𒃲𒁹𒌋𒄀")
/* naṣārum (to guard, reused) — an exception handler guards against faults. */
AKK_PR("with-exception-handler", "naṣār-ḫiṭītim", "𒉡𒂗𒄷")
/* "ul mimma": genuine Akkadian idiom, "nothing at all". */
AKK_PR("void",          "ul-mimma",       "𒉡𒉡𒉡𒉡𒉌")
AKK_PR("trace",         "šutar-redîm",          "𒈠𒂗𒉡")
AKK_PR("traced?",       "redûm?",         "𒈠𒂗?")
AKK_PR("untrace",       "paṭār-redîm",    "𒇲𒈠𒂗")
/* manzāzu (position/station, reused from plot-device) — a marked halt point. */
AKK_PR("breakpoint",    "manzāzum",       "𒈠𒉡𒌋𒉡𒉌")
AKK_PR("system",        "epēš-bītim",     "𒇽𒂍")

/* GC stats (distinct from gc-collect!/gc-on-collection, already covered). */
AKK_PR("gc",                      "ebēbum",             "𒁀𒉡𒉡")
AKK_PR("gc-stats",                "ṭēm-ebēbim",         "𒅆𒉡𒉡𒉡𒉌")
AKK_PR("gc-stats-reset!",         "turru-ṭēm-ebēbim",   "𒄀𒅆𒉡𒉡")
AKK_PR("gc-total-bytes",          "mīnu-ṣibtātim",      "𒈠𒁀𒉡")
AKK_PR("gc-free-bytes",           "mīnu-ṣibtāti-paṭārim","𒈠𒁀𒉡𒇲")
/* nagbu: "the deep; totality; the underground water source" — genuine,
 * a fitting metaphor for a vast reserve. */
AKK_PR("gc-heap-size",            "mīnu-nagbim",        "𒈠𒉡𒃲𒉡")
AKK_PR("gc-mode",                 "zikru-ebēbim",       "𒌋𒉡𒉡𒉡")
AKK_PR("gc-enable-incremental!",  "šitkun-ebēbi-mala",  "𒁹𒉡𒉡𒉡𒉡")
AKK_PR("gc-set-free-space-divisor!", "šakān-puluggi-paṭārim", "𒁹𒇲𒉌𒇲")
AKK_PR("gc-set-max-heap!",        "šakān-rabûti-nagbim","𒁹𒃲𒉡𒃲𒉡")

/* ---- MPFR (BUILD_MPFR=ON) ---- */
/* nasqu: "chosen, select, precise" — genuine adjective, an apt root for
 * arbitrary-precision arithmetic. */
AKK_PR("mpfr",              "nasqum",             "𒉡𒀸𒌋")
AKK_PR("mpfr?",             "nasqum?",            "𒉡𒀸𒌋?")
AKK_PR("mpfr-precision",    "mīnu-nasqim",        "𒈠𒉡𒀸𒌋")
AKK_PR("mpfr-set-precision","šakān-mīni-nasqim",  "𒁹𒈠𒉡𒀸𒌋")
AKK_PR("mpfr-pi",           "nasqu-kippatim",     "𒉡𒀸𒌋𒄀")
AKK_PR("mpfr-e",            "nasqu-napḫarim",     "𒉡𒀸𒌋𒈷")
AKK_PR("mpfr-phi",          "nasqu-mitḫartim",    "𒉡𒀸𒌋𒈠𒋻")
AKK_PR("mpfr-euler",        "nasqu-kamārim",      "𒉡𒀸𒌋𒃲")
AKK_PR("mpfr-catalan",      "nasqu-purussîm",     "𒉡𒀸𒌋𒁀𒋻")
AKK_PR("mpfr-apery",        "nasqu-kamāri-šalšim","𒉡𒀸𒌋𒃲𒋻")
AKK_PR("mpfr-sqrt",         "nasqu-ibum",         "𒉡𒀸𒌋𒅁")
AKK_PR("mpfr-exp",          "nasqu-napḫar-ṣīrim", "𒉡𒀸𒌋𒈷𒁹")
AKK_PR("mpfr-log",          "nasqu-naṭāl-ṣīrim",  "𒉡𒀸𒌋𒅆𒁹")
AKK_PR("mpfr-gamma",        "nasqu-rabûtim",      "𒉡𒀸𒌋𒃲𒉡")
AKK_PR("mpfr-lgamma",       "nasqu-naṭāl-rabûtim","𒉡𒀸𒌋𒅆𒃲")
AKK_PR("mpfr-zeta",         "nasqu-kamāri-mala",  "𒉡𒀸𒌋𒃲𒉡𒉡")
AKK_PR("mpfr-erf",          "nasqu-pūru-gamrim",  "𒉡𒀸𒌋𒁀𒌋")
AKK_PR("mpfr-erfc",         "nasqu-pūru-gamru-šanûm", "𒉡𒀸𒌋𒁀𒌋𒁀")
AKK_PR("mpfr-j0",           "nasqu-zīqi-ṣifrim",  "𒉡𒀸𒌋𒌋𒉌")
AKK_PR("mpfr-j1",           "nasqu-zīqi-ištēn",   "𒉡𒀸𒌋𒌋𒉌𒀸")
AKK_PR("mpfr-log2",         "nasqu-naṭāl-šinaim", "𒉡𒀸𒌋𒅆𒁹𒁹")
AKK_PR("mpfr-log2-of",      "nasqu-naṭāl-šanûm",  "𒉡𒀸𒌋𒅆𒁀")
AKK_PR("mpfr-log10",        "nasqu-naṭāl-ešrim",  "𒉡𒀸𒌋𒅆𒌋")
AKK_PR("mpfr-hypot",        "nasqu-ibu-kilallān", "𒉡𒀸𒌋𒅁𒌋")
AKK_PR("mpfr-fma",          "nasqu-šutakūl-matāḫim", "𒉡𒀸𒌋𒈧𒋻")
AKK_PR("mpfr-rounding-mode","zikru-nasqi-turrim", "𒌋𒉡𒀸𒌋𒄀")
AKK_PR("call-with-precision", "epēš-ina-nasqim",  "𒇽𒀀𒉡𒀸𒌋")
/* inanna: "now" — genuine adverb, matching current-second's convention. */
AKK_PR("current-precision",   "nasqu-inanna",     "𒉡𒀸𒌋𒉡𒉡")

/* ---- Interval arithmetic ---- */
/* pūtu: "extent, width, front" — genuine noun, a fitting root for a
 * numeric range. */
AKK_PR("interval",           "pūtum",           "𒁀𒌋𒉡")
AKK_PR("make-interval",      "epēš-pūtim",      "𒇽𒁀𒌋𒉡")
AKK_PR("interval?",          "pūtum?",          "𒁀𒌋𒉡?")
AKK_PR("interval-lo",        "šapil-pūtim",     "𒆠𒁀𒌋𒉡")
AKK_PR("interval-hi",        "elû-pūtim",       "𒀭𒁀𒌋𒉡")
AKK_PR("interval-midpoint",  "mitḫar-pūtim",    "𒈠𒋻𒁀𒌋𒉡")
AKK_PR("interval-width",     "rupuš-pūtim",     "𒌋𒁀𒁀𒌋𒉡")
AKK_PR("interval-contains?", "libbu-pūtim?",    "𒉡𒁀𒌋𒉡?")

/* ---- Misc ---- */
/* labīru (old/former, reused) — legacy names for profiler-report/reset. */
AKK_PR("profiling-report", "ṭēm-manāḫti-labīrtim",  "𒅆𒈠𒉡𒉡")
AKK_PR("profiling-reset",  "turru-manāḫti-labīrtim","𒄀𒈠𒉡𒉡")
/* ašru (place, reused) — the location within a string where a substring is found. */
AKK_PR("string-contains",  "ašar-ṭuppim",     "𒀀𒉡𒌝")
AKK_PR("vec3-project-batch","šadād-kibrāti-puḫrim", "𒊻𒀀𒁹𒃲𒉌")

/* ---- More symbolic CAS (src/builtins_curry.c) ---- */
/* idātu: "sign, omen, mark, direction" — genuine, distinct from têrtu
 * (used for mobius/omens) and apt for a numeric sign function. */
AKK_PR("sign",          "idâtum",          "𒀸𒁹𒉡")
AKK_PR("∂",             "māḫir-ḫepîm",     "𒄭𒊕𒇲")   /* māḫirum (rate, reused) of a fraction */
AKK_PR("∫",             "eqlu-kalāma",     "𒀭𒆠𒉡")   /* eqlum (field/integral, reused), of everything */
AKK_PR("trigsimp",      "šuklul-šapaltim", "𒁹𒆠𒁹𒁹𒀸")
/* dūru: "wall, boundary, fortification" — genuine, approaching a boundary
 * is exactly a limit. */
AKK_PR("limit",         "qerbu-dūrim",     "𒂗𒉡𒁀𒌋")
AKK_PR("series",        "šiṭir-mala",      "𒌝𒁹𒉡")
AKK_PR("D",             "māḫir-kalāma",    "𒄭𒊕𒉡")
AKK_PR("partial",       "ḫepû-māḫirim",    "𒇲𒄭𒊕")
/* šadādu: "to pull, to drag" — genuine verb, a gradient pulls toward
 * steepest ascent. */
AKK_PR("grad",          "šadādum",         "𒊻𒀀𒁹")
AKK_PR("gradient",      "šadādu-arkûm",    "𒊻𒀀𒁹𒉡")
AKK_PR("divergence",    "waṣê-šadādim",    "𒉡𒉡𒊻𒀀")
/* saḫāru: "to turn around, to circle" — genuine, a direct fit for curl. */
AKK_PR("curl",          "saḫārum",         "𒀭𒀸𒄷")
AKK_PR("laplacian",     "šadādu-šanûm",    "𒊻𒀀𒁹𒁀")
AKK_PR("vec-laplacian", "šadādu-šanî-kibrātim", "𒊻𒀀𒁹𒁀𒃲")
AKK_PR("dot-product",   "napḫar-kilallān-ṣindim", "𒈷𒁹𒌋𒀸𒉡")
AKK_PR("cross-product", "ṣilip-ṣindim",    "𒆠𒃲𒌋𒀸")
/* nabalkutu: "to cross over" (reused from image-flip) — a transform
 * crosses from one domain to another. */
AKK_PR("laplace",       "nabalkut-eqlim",       "𒉡𒁀𒀭𒆠")
AKK_PR("ilaplace",      "nabalkut-eqli-turrum", "𒉡𒁀𒀭𒆠𒄀")
AKK_PR("fourier",       "nabalkut-eqli-šanûm",  "𒉡𒁀𒀭𒆠𒁀")
AKK_PR("ifourier",      "nabalkut-eqli-šanî-turrum", "𒉡𒁀𒀭𒆠𒁀𒄀")
AKK_PR("laurent",       "šiṭir-lā-kīnim",  "𒌝𒁹𒉡𒆠")   /* series admitting negative powers */
AKK_PR("puiseux",       "šiṭir-ḫepîm",     "𒌝𒁹𒇲")     /* series with fractional powers */
AKK_PR("poly-gcd",         "kabru-šiṭrim",       "𒃲𒁹𒁹𒌝𒁹")
AKK_PR("poly-resultant",   "napḫar-šiṭrim",      "𒈷𒈷𒌝𒁹")
AKK_PR("poly-squarefree",  "lā-mitḫartu-šiṭrim", "𒉡𒈠𒋻𒌝𒁹")
AKK_PR("poly-factor",      "zittu-šiṭrim",       "𒁀𒋻𒌝𒁹")
AKK_PR("partial-fractions","ḫepû-zittim",        "𒇲𒁀𒋻")
/* purussûm: "decision, verdict" (reused from catalan) — solving reaches
 * a verdict. */
AKK_PR("solve",        "purussûm",       "𒁀𒋻𒉌")
AKK_PR("solve-system",  "purussu-puḫrim", "𒁀𒋻𒉌𒉌")
AKK_PR("groebner",      "purussu-kalāma", "𒁀𒋻𒉌𒉡")
/* parṣu: "rite, ordinance, prescribed procedure" — genuine, a rule is a
 * prescribed procedure. */
AKK_PR("list-rules",    "šumū-parṣim",    "𒌋𒇲𒉌𒌋")
AKK_PR("clear-rules!",  "paṭār-parṣim",   "𒇲𒉌𒇲𒌋")
/* qabûm: "to say, to speak, to declare" — genuine, an assumption is a
 * declared premise. */
AKK_PR("assume!",           "qabûm",         "𒀸𒁀𒌋")
AKK_PR("can-assume?",       "qabû-maṣûm?",   "𒀸𒁀𒌋𒉌?")
AKK_PR("drop-assumption!",  "paṭār-qabîm",   "𒇲𒉌𒀸𒁀𒌋")
/* la-idûm (the unknown, reused) — CAS expression accessors. */
AKK_PR("sym-expr",       "banû-la-idûm",   "𒉡𒌋𒉡𒅆")
AKK_PR("sym-expr-nargs", "mīnu-la-idûm",   "𒈠𒉡𒅆")
AKK_PR("sym-expr-arg",   "leqû-la-idûm",   "𒅁𒉡𒅆")
AKK_PR("sym-expr-op",    "zikru-la-idûm",  "𒌋𒉡𒅆")
AKK_PR("sym-fn",         "šumu-pārisim",   "𒌋𒇽𒉌")
AKK_PR("sym-fn?",        "šumu-pārisim?",  "𒌋𒇽𒉌?")
AKK_PR("sym-fn-name",    "zikru-pārisim",  "𒌋𒇽𒉌𒉡")
AKK_PR("fn-apply",       "paqād-pārisim",  "𒇽𒄿𒇽𒉌")
AKK_PR("unspecified?",   "lā-zikrum?",     "𒉡𒌋𒉡?")
/* SICM classical-mechanics tuples: elûm/šaplûm (high/low, reused) mark
 * contravariant ("up") / covariant ("down") index position. */
AKK_PR("up",            "elû-malîm",       "𒀭𒉡𒉌")
AKK_PR("down",          "šapil-malîm",     "𒆠𒉌𒉌")
AKK_PR("up?",           "elû-malîm?",      "𒀭𒉡𒉌?")
AKK_PR("down?",         "šapil-malîm?",    "𒆠𒉌𒉌?")
/* riksum (bond/link, reused) — a tuple is a bound-together set of values. */
AKK_PR("tuple?",        "riksu-malîm?",    "𒇲𒁹𒉌?")
AKK_PR("ref",           "maḫār-malîm",     "𒌝𒉌𒉌")
AKK_PR("dimension",     "mīnu-malîm",      "𒈠𒉌𒉌𒉌")
AKK_PR("tuple->list",   "malûm-ana-nindabîm",      "𒉌𒀀𒇽")
AKK_PR("list->up",      "nindabûm-ana-elî-malîm",  "𒇽𒀀𒀭𒉌")
AKK_PR("list->down",    "nindabûm-ana-šapil-malîm","𒇽𒀀𒆠𒉌")
AKK_PR("quad",           "eqlu-nikkassim",       "𒀭𒆠𒈷𒉡")
AKK_PR("quad-frac-diff", "māḫir-ḫepî-nikkassim", "𒄭𒊕𒇲𒈷𒉡")
AKK_PR("quad-frac-int",  "eqlu-ḫepî-nikkassim",  "𒀭𒆠𒇲𒈷𒉡")
/* iṣu: "tree, wood" — genuine noun, the fallback tree-walking evaluator. */
AKK_PR("tree-eval",     "epēš-iṣim",       "𒇽𒄿𒍾")

/* ---- Parallel map/reduce (src/workpool.c via builtins_curry.c) ---- */
/* ēdiš: "alone, singly" — genuine adverb from ēdu, apt for sequential. */
AKK_PR("map/seq",       "šutakūlu-nindabî-ēdiš", "𒈷𒅆𒀸𒉡")
AKK_PR("reduce",        "kamār-nindabîm",        "𒃲𒇽")
AKK_PR("reduce/seq",    "kamār-nindabî-ēdiš",    "𒃲𒇽𒀸𒉡")
AKK_PR("map-parallel-threshold",     "puluggu-šutakūlim", "𒇲𒉡𒈷𒅆")
AKK_PR("set-map-parallel-threshold!","šakān-puluggi-šutakūlim", "𒁹𒇲𒉡𒈷𒅆")
/* gabbiš: "totally, entirely" — genuine adverb from gabbu, apt for "all
 * branches at once" = parallel. */
AKK_PR("for-each/par",  "ana-kālāma-gabbiš", "𒀀𒌋𒉡𒃲")
AKK_PR("hardware-concurrency", "mīnu-gabbêm", "𒈠𒉡𒃲")

/* ---- Random sources (src/builtins_curry.c) ---- */
/* pūrum: "lot" (as in casting lots) — genuine and apt: this is literally
 * how the Babylonians generated a random outcome. */
AKK_PR("random-real",     "pūrum",           "𒁀𒌋𒌝")
AKK_PR("random-integer",  "pūru-nikkassim",  "𒁀𒌋𒌝𒈷")
AKK_PR("make-random-source", "epēš-pūrim",   "𒇽𒁀𒌋𒌝")
AKK_PR("random-source?",     "pūrum?",       "𒁀𒌋𒌝?")
/* dalāḫu: "to stir up, to disturb" — genuine verb, apt for re-seeding. */
AKK_PR("random-source-randomize!",        "dalāḫ-pūrim",         "𒁀𒌑𒁀𒌋𒌝")
AKK_PR("random-source-pseudo-randomize!",  "dalāḫ-pūri-lā-kīnim", "𒁀𒌑𒁀𒌋𒌝𒆠")
AKK_PR("random-source->random-real",      "pūrum-ana-nikkassim",     "𒁀𒌋𒌝𒀀𒈷")
AKK_PR("random-source->random-integer",   "pūrum-ana-nikkassi-gamrim","𒁀𒌋𒌝𒀀𒈷𒃲")

/* ---- Special functions (src/builtins_curry.c) — genuinely modern,
 * no OB analog; gerru ("way, path, course") names families of
 * orthogonal polynomials, each distinguished by a genuine ordinal. */
AKK_PR("legendre",         "gerru-maḫrûm",         "𒁺𒋻𒉌")
AKK_PR("assoc-legendre",   "gerru-maḫrî-itâtim",   "𒁺𒋻𒉌𒁹")
AKK_PR("hermite",          "gerru-šanûm",          "𒁺𒋻𒉌𒁀")
AKK_PR("hermite-prob",     "gerru-šanî-kīnim",     "𒁺𒋻𒉌𒁀𒆠")
AKK_PR("chebyshev-t",      "gerru-šalšum",         "𒁺𒋻𒉌𒋻")
AKK_PR("chebyshev-u",      "gerru-rebûm",          "𒁺𒋻𒉌𒉡")
/* ḫamšu: "five" — genuine cardinal. */
AKK_PR("laguerre",         "gerru-ḫamšum",         "𒁺𒋻𒉌𒀸")
AKK_PR("assoc-laguerre",   "gerru-ḫamši-itâtim",   "𒁺𒋻𒉌𒀸𒁹")
AKK_PR("spherical-harmonic","gerru-kibrātim",      "𒁺𒋻𒉌𒃲")
/* rabûtu: "greatness" (abstract noun from rabû, distinct usage from the
 * adjective already used for max/great) — Gamma generalizes factorial. */
AKK_PR("gamma",        "rabûtum",         "𒃲𒉡𒌝")
AKK_PR("log-gamma",    "naṭāl-rabûtim",   "𒅆𒁹𒃲𒉡𒌝")
AKK_PR("digamma",      "rabûtu-šanītum",  "𒃲𒉡𒌝𒁀")
AKK_PR("beta",         "rabûtu-kilallān", "𒃲𒉡𒌝𒌋")
/* pūrum (lot/chance, reused) — erf relates to the normal distribution. */
AKK_PR("erf",          "pūru-gamrum",     "𒁀𒌋𒌝𒃲")
AKK_PR("erfc",         "pūru-gamru-šanûm","𒁀𒌋𒌝𒃲𒁀")
/* zīqu: "breath, waft" (reused from pwm) — Bessel functions arise in
 * wave equations. */
AKK_PR("bessel-j",     "zīqu-maḫrûm",     "𒌋𒉌𒉌𒉌")
AKK_PR("bessel-y",     "zīqu-šanûm",      "𒌋𒉌𒉌𒁀")
AKK_PR("bessel-i",     "zīqu-šalšum",     "𒌋𒉌𒉌𒋻")
AKK_PR("bessel-k",     "zīqu-rebûm",      "𒌋𒉌𒉌𒉡")
/* kippatu (circle/modulo, reused) — elliptic integrals measure arc length. */
AKK_PR("elliptic-k",   "kippat-gamrum",       "𒄀𒌋𒃲")
AKK_PR("elliptic-e",   "kippat-gamru-šanûm",  "𒄀𒌋𒃲𒁀")
AKK_PR("elliptic-f",   "kippat-ḫepûm",        "𒄀𒌋𒇲")
AKK_PR("elliptic-pi",  "kippat-ḫepû-šalšum",  "𒄀𒌋𒇲𒋻")

/* ---- STM / channels (src/stm.c, src/channel.c via builtins_curry.c) ---- */
/* maškattu: "deposit, pledge" — genuine noun, a transactional variable
 * holds a value like a bank deposit until the transaction commits. */
AKK_PR("make-tvar",   "epēš-maškatti", "𒇽𒈠𒂍")
AKK_PR("tvar-read",   "leqû-maškatti", "𒅁𒈠𒂍")
AKK_PR("tvar-write!", "šakān-maškatti","𒁹𒈠𒂍")
AKK_PR("tvar?",       "maškattum?",    "𒈠𒂍?")
/* ištēniš: "together, as one" — genuine adverb from ištēn (one, reused). */
AKK_PR("atomically",  "ana-ištēniš",   "𒀀𒀸𒌋𒉡")
AKK_PR("retry",       "šanā-epēšim",   "𒁀𒇽𒉌")
/* atappu (channel/irrigation-ditch, reused from i2c) — a fitting root
 * for a message channel too. */
AKK_PR("make-channel",   "epēš-atapp-šipri", "𒇽𒀀𒉌")
AKK_PR("channel-send!",  "šapār-atapp-šipri","𒌝𒀀𒉌")
AKK_PR("channel-recv!",  "maḫār-atapp-šipri","𒌝𒀀𒉌𒉡")
AKK_PR("channel-close!", "sakār-atapp-šipri","𒂍𒀀𒉌𒌋")
/* sakru: passive participle "closed, shut" — genuine, distinct from the
 * verb sakārum already reused throughout. */
AKK_PR("channel-closed?","sakru-atapp-šipri?","𒂍𒀀𒉌?")
AKK_PR("channel?",       "atapp-šipri?",      "𒀀𒉌𒉡?")

/* ---- LLVM JIT introspection (src/builtins_curry.c) ---- */
/* ḫamṭu (swift, reused from redis) — a fitting root for a JIT: literally
 * "just in time" = swift compiled execution. */
AKK_PR("curry-llvm-available?", "bašû-ḫamṭim?",       "𒀸𒉡𒉡?")
AKK_PR("curry-llvm-dump-last",  "šaṭār-ḫamṭi-warkîm",  "𒌝𒉡𒉡𒉡")
AKK_PR("curry-jit-call",        "epēš-ḫamṭim",         "𒇽𒉡𒉡")
AKK_PR("curry-jit-eval",        "epēš-ḫamṭi-awātim",   "𒇽𒉡𒉡𒀸")
/* šuplu: "depth" — genuine noun. */
AKK_PR("jit-call-depth",        "šupul-ḫamṭim",        "𒋻𒉡𒉡")
AKK_PR("jit-compile!",          "banû-ḫamṭim",         "𒉡𒉡𒉡𒉡")

/* ---- GC control (src/builtins_curry.c) ---- */
AKK_PR("gc-collect!",      "ebēb-kalāma",  "𒁀𒉡𒉡𒉡𒉡")   /* ebēbu, reused from plot-clear */
AKK_PR("gc-on-collection", "šūdû-ebēbim",  "𒅆𒉡𒉡𒉡")

/* ---- Optional C modules ---- */
/* These bind only inside a module's own environment until (import ...)
 * copies them into the importer's env — see modules_import()'s call to
 * akk_pr_lookup() in src/modules.c. Most of these concepts (LDAP, MCP,
 * TLS, HTTP, GraphQL, cryptographic hashing) have no Old Babylonian
 * referent at all; real dictionary words are chosen for their core
 * meaning (šâlum "to ask" for a GraphQL query, erištu "request/petition"
 * for an HTTP request, riksu "bond/link" for a graph edge, kunukku
 * "cylinder seal" for a cryptographic hash/signature) and compounded
 * honestly rather than invented from whole cloth. */

/* ---- (curry regex) ---- */
/* mašālum: "to resemble, to be like" — genuine verb for pattern matching. */
AKK_PR("regex-compile",      "mašālu-epēšum",   "𒈠𒃲𒇽")   /* prepare a resemblance-rule */
AKK_PR("regex-match",        "mašālum",         "𒈠𒃲")     /* does it resemble? */
AKK_PR("regex-match-string", "mašālu-ṭuppim",   "𒈠𒃲𒌝")   /* resemblance within the tablet */
AKK_PR("regex-replace",      "mašālu-šanûm",    "𒈠𒃲𒁀")   /* resemblance, then changed */
AKK_PR("regex-split",        "mašālu-zâzum",    "𒈠𒃲𒈧")   /* resemblance-divide */
AKK_PR("regex?",             "mašālum?",        "𒈠𒃲𒉡?")   /* is it a resemblance-rule? */
/* paṭārum: "to release, set free" — genuine verb for releasing a resource. */
AKK_PR("regex-free",         "mašālu-paṭārum",  "𒈠𒃲𒇲")   /* release the resemblance-rule */

/* ---- (curry ldap) ---- */
/* puḫru: "assembly" (reused) stands in for a directory of entries; nišū
 * ("people") specializes it to a directory-of-persons. */
AKK_PR("ldap-connect",      "erēb-puḫur-nišī",   "𒂗𒅁𒌋")   /* enter the assembly of people */
/* kanākum: "to seal" — the genuine OB verb for authenticating a document. */
AKK_PR("ldap-bind!",        "kanāk-puḫrim",      "𒁀𒅁𒌋")   /* seal oneself to the assembly */
/* šeʾûm: "to seek, to search for" — genuine verb. */
AKK_PR("ldap-search",       "šeʾû-puḫrim",       "𒅆𒅁𒌋")   /* seek within the assembly */
AKK_PR("ldap-close!",       "sakār-puḫrim",      "𒂍𒅁𒌋")   /* close (reused sakārum) the assembly */
AKK_PR("ldap-set-option!",  "šakān-ṭēm-puḫrim",  "𒁹𒅁𒌋")   /* ṭēmu = instruction/report */
/* naṣārum: "to guard, protect" — escaping a value guards against injection. */
AKK_PR("ldap-escape-value", "naṣār-puḫrim",      "𒉡𒅁𒌋")

/* ---- (curry neo4j) ---- */
/* riksu: "bond, knot, link" — the genuine word for a graph edge/relationship. */
AKK_PR("neo4j-connect",    "erēb-riksī",   "𒂗𒇲𒁹")   /* enter the bonds */
AKK_PR("neo4j-disconnect", "waṣê-riksī",   "𒉡𒇲𒁹")   /* exit the bonds (waṣûm, reused) */
AKK_PR("neo4j-run",        "epēš-riksī",   "𒇽𒇲𒁹")   /* perform upon the bonds */
/* šurrûm: "to begin" — genuine verb. */
AKK_PR("neo4j-begin-tx",   "šurrû-riksī",  "𒋻𒇲𒁹")
/* kânu: "to make firm, establish" (reused). */
AKK_PR("neo4j-commit",     "kunnu-riksī",  "𒆠𒇲𒁹")
/* turrum: "to turn back" (reused, as in mv-reverse). */
AKK_PR("neo4j-rollback",   "turru-riksī",  "𒄀𒇲𒁹")

/* ---- (curry mcp) ---- */
/* unūtu: "tool, implement, equipment"; šipru: "task, message, work" — both genuine. */
AKK_PR("mcp-tool",             "unūt-šipri",      "𒌋𒉡𒌋")
/* makkūru: "property, goods, resource" — genuine. */
AKK_PR("mcp-resource",         "makkūr-šipri",    "𒈠𒉌𒌋")
AKK_PR("mcp-text",             "awât-šipri",      "𒀸𒉌𒌋")   /* awātu = word (reused) */
AKK_PR("mcp-json",             "ṭuppu-šipri",     "𒌝𒉌𒌋")   /* tablet of the task */
/* šūdûm: "to announce, proclaim" — genuine causative verb. */
AKK_PR("mcp-notify-progress",  "šūdû-alāki",      "𒅆𒉌𒄿")   /* announce the going (alākum, reused) */
AKK_PR("mcp-serve",            "epēš-šipri",      "𒇽𒉌𒌋")   /* perform the task */
AKK_PR("mcp-serve-sse",        "epēš-šipri-šanûm","𒇽𒉌𒌋𒁀") /* the alternate/streaming form */

/* ---- (curry mcp) auth configuration ---- */
AKK_PR("mcp-auth-mode!",                 "šakān-kanāki",         "𒁹𒁀𒃲")
/* mār šipri: literally "son of the message" — the genuine OB term for a
 * messenger/agent, an apt fit for an API client. */
AKK_PR("mcp-register-client!",           "šaṭār-mār-šipri",      "𒌝𒉌𒌋𒁹")
/* adannu: "appointed time, deadline"; kanīku: "sealed document" — a token
 * IS a signed/sealed document with an expiry. */
AKK_PR("mcp-token-ttl!",                 "adan-kanīki",          "𒌑𒁀𒃲")
AKK_PR("mcp-introspection-endpoint!",    "bāb-amārim",           "𒂍𒅆𒄿")   /* gate of seeing */
AKK_PR("mcp-introspection-credentials!", "kanīk-amārim",         "𒁀𒃲𒅆𒄿")
/* maškanu: "storage place, threshing floor" — genuine word for a depot/cache. */
AKK_PR("mcp-introspection-cache-ttl!",   "adan-maškanim",        "𒌑𒌝𒂍")
/* epištu: "procedure, method, deed" — genuine. */
AKK_PR("mcp-jwt-algorithm!",             "epšet-kanīki",         "𒇽𒁀𒃲")
/* pirištu: "secret, mystery" — genuine. */
AKK_PR("mcp-jwt-secret!",                "pirišti-kanīki",       "𒉡𒁀𒃲")
/* pattû: "opener" (agent noun from petûm, reused) — the public key opens/
 * verifies what the private key sealed. */
AKK_PR("mcp-jwt-public-key!",            "pattu-kanīki",         "𒂍𒁀𒃲")
AKK_PR("mcp-jwt-public-key-pem!",        "pattu-kanīki-ṭuppam",  "𒂍𒁀𒃲𒌝")
/* nādinu: "giver" (agent noun from nadānu). */
AKK_PR("mcp-jwt-issuer!",                "nadin-kanīki",         "𒁀𒁀𒃲")
/* māḫiru: "receiver" (agent noun from maḫārum, reused). */
AKK_PR("mcp-jwt-audience!",              "māḫir-kanīki",         "𒌝𒁀𒃲𒉡")

/* ---- (curry profiling) ---- */
/* manaḫtu: "toil, labor, effort" — genuine word for exertion. */
AKK_PR("profiler-start",       "šurrû-manāḫtim",       "𒋻𒈠𒉡")   /* šurrûm = begin, reused */
AKK_PR("profiler-stop",        "gamār-manāḫtim",       "𒃲𒈠𒉡")   /* gamārum = to complete, genuine */
AKK_PR("profiler-reset",       "turru-manāḫtim",       "𒄀𒈠𒉡")   /* turrum = turn back, reused */
AKK_PR("profiler-level",       "šinīpat-manāḫtim",     "𒀸𒈠𒉡")   /* reused mv-grade root */
AKK_PR("profiler-report",      "ṭēm-manāḫtim",         "𒅆𒈠𒉡")   /* ṭēmu = report, genuine */
AKK_PR("profiler-report/top",  "ṭēm-manāḫtim-elûm",    "𒅆𒈠𒉡𒃲") /* the topmost report (elûm, reused) */

/* ---- (curry crypto) ---- */
AKK_PR("base64-encode",  "ṭuppu-šutēšurum", "𒌝𒋻𒌋")   /* put the document in order */
AKK_PR("base64-decode",  "ṭuppu-turrum",    "𒌝𒄀𒌋")   /* turn the document back */
/* kunukku: "cylinder seal" — the genuine OB authenticating device, a
 * natural metaphor for a cryptographic hash/checksum. */
AKK_PR("md5",            "kunukku-maḫrûm",       "𒁀𒃲𒁹")   /* the elder/former seal */
AKK_PR("md5-hex",        "kunukku-maḫrûm-ṭuppam","𒁀𒃲𒁹𒌝")
/* ištēn: "one" — the genuine cardinal numeral (distinct root from the
 * DIŠ-based ēdum used for "sole/prime" elsewhere in this file). */
AKK_PR("sha1",           "kunukku-ištēn",        "𒁀𒃲𒀸")
AKK_PR("sha1-hex",       "kunukku-ištēn-ṭuppam", "𒁀𒃲𒀸𒌝")
/* kabtu: "heavy, weighty, important" — genuine, marking the "heavier" hash. */
AKK_PR("sha256",         "kunukku-kabtum",       "𒁀𒃲𒆠")
AKK_PR("sha256-hex",     "kunukku-kabtum-ṭuppam","𒁀𒃲𒆠𒌝")
AKK_PR("hmac-sha256",    "kunukku-kabti-pirišti","𒁀𒃲𒆠𒉡") /* the heavy seal, of the secret */

/* ---- (curry network) TLS ---- */
/* puzru: "hidden thing, secret" — genuine noun. */
AKK_PR("tcp-connect-tls", "erēb-puzri",     "𒂗𒉡𒌋")   /* enter under secrecy */
/* qerēbum: "to approach, to draw near" — genuine, distinct from erēbum
 * (to enter) already used for import/connect-under-tls. */
AKK_PR("tcp-connect",  "qerēbum",         "𒂗𒉡𒁀")
/* maṣṣartu (watch-duty, reused) at a gate (bābu, reused) — listening. */
AKK_PR("tcp-listen",   "maṣṣar-bābim",    "𒈧𒉌𒂍𒉌")
AKK_PR("tcp-accept",   "maḫār-qerbim",    "𒌝𒉡𒂗𒁀")
AKK_PR("tcp-close",    "sakār-qerbim",    "𒂍𒉡𒂗𒉡")
/* kīnu (true/fixed, reused) negated — UDP is connectionless, "not fixed". */
AKK_PR("udp-socket",   "epēš-bābi-lā-kīnim",  "𒇽𒂍𒉡𒆠")
AKK_PR("udp-bind",     "rakās-bābi-lā-kīnim", "𒇲𒂍𒉡𒆠")
AKK_PR("udp-send",     "šapār-bābi-lā-kīnim", "𒌝𒂍𒉡𒆠")
AKK_PR("udp-recv",     "maḫār-bābi-lā-kīnim", "𒌝𒂍𒉡𒆠𒉡")
/* kalûm: "to hold back, to detain, to delay" — genuine, non-blocking is
 * "not delaying". */
AKK_PR("socket-set-nonblocking!", "bābum-lā-kalûm", "𒂍𒉡𒉡𒆠𒌋")
AKK_PR("socket-ready?",           "bašû-bābim?",    "𒀸𒂍𒉌?")

/* ---- (curry http) ---- */
/* erištu: "request, petition" — genuine noun, an exact fit. */
AKK_PR("http-request",   "erištum",        "𒂍𒉡𒁹")

/* ---- (curry graphql) ---- */
/* šâlum: "to ask, to inquire"; šāʾilu: agent noun, "one who asks". */
AKK_PR("graphql-client", "šāʾilum",        "𒅆𒀀𒇽")   /* the asker */
AKK_PR("graphql-query",  "šâlum",          "𒅆𒀀")     /* to ask */
AKK_PR("graphql-mutate", "šanûm-šâlim",    "𒅆𒀀𒁀")   /* a changing ask (šanûm, reused) */

/* ---- (curry sqlite) ---- */
/* nikkassu: "account(s)" (reused, as in number->string's nikkassum) —
 * genuine fit for a relational database. */
AKK_PR("sqlite-open",              "petû-nikkassim",       "𒂍𒈷𒉡")
AKK_PR("sqlite-open-memory",       "petû-nikkassi-ramāni", "𒂍𒈷𒍪")   /* ramānu = self, reused */
AKK_PR("sqlite-close",             "sakār-nikkassim",      "𒂍𒈷𒇲")   /* sakārum, reused */
AKK_PR("sqlite-exec",              "epēš-nikkassim",       "𒇽𒈷")
/* šitkunu: Gt-stem of šakānu, "to set in place, prepare" — genuine. */
AKK_PR("sqlite-prepare",           "šitkun-nikkassim",     "𒁹𒈷𒁹")
AKK_PR("sqlite-bind",              "rakās-nikkassim",      "𒇲𒈷")     /* rakāsum = bind, reused */
AKK_PR("sqlite-step",              "alāk-nikkassim",       "𒄿𒈷")     /* alākum = to go, reused */
AKK_PR("sqlite-finalize",          "gamār-nikkassim",      "𒃲𒈷𒃲")   /* gamārum, reused */
/* warkûm: "last, latest" — genuine. */
AKK_PR("sqlite-last-insert-rowid", "warki-nikkassim",      "𒉡𒈷")
AKK_PR("sqlite-changes",           "nakru-nikkassim",      "𒉡𒄿𒈷")   /* nakārum root, reused */

/* ---- (curry storage) — S3 / Swift / Azure object storage ---- */
/* maškanu: "storage place, threshing floor" — genuine, reused from
 * mcp-introspection-cache-ttl!. Distinguished per cloud by genuine OB
 * ordinals (šanûm "second"/"other" already reused elsewhere; šalšu
 * "third" introduced here). */
AKK_PR("s3-client",     "erēb-maškanim",         "𒂗𒂍")
AKK_PR("s3-put!",       "šakān-maškanim",        "𒁹𒂍")
AKK_PR("s3-get",        "leqû-maškanim",         "𒅁𒂍")     /* leqûm = to take, reused (let) */
AKK_PR("s3-delete!",    "nasāḫ-maškanim",        "𒋻𒂍𒉡")     /* nasāḫum = to tear out, genuine */
AKK_PR("swift-client",  "erēb-maškanim-šanûm",   "𒂗𒂍𒁀")
AKK_PR("swift-put!",    "šakān-maškanim-šanûm",  "𒁹𒂍𒁀")
AKK_PR("swift-get",     "leqû-maškanim-šanûm",   "𒅁𒂍𒁀")
/* šalšu: "third" — genuine cardinal ordinal. */
AKK_PR("azure-client",  "erēb-maškanim-šalšum",  "𒂗𒂍𒋻")
AKK_PR("azure-put!",    "šakān-maškanim-šalšum", "𒁹𒂍𒋻")
AKK_PR("azure-get",     "leqû-maškanim-šalšum",  "𒅁𒂍𒋻")
AKK_PR("azure-delete!", "nasāḫ-maškanim-šalšum", "𒋻𒂍𒋻")

/* ---- (curry sync) — mutex/condvar/semaphore ---- */
/* kanākum: "to seal" (reused) — locking IS sealing, a very natural fit. */
AKK_PR("make-mutex",         "epēš-kanākim",     "𒇽𒁀𒌑")
AKK_PR("mutex-destroy!",     "ḫepû-kanākim",     "𒇲𒁀𒌑")   /* ḫepûm = break, reused */
AKK_PR("mutex-lock!",        "kanākum",          "𒁀𒌑")
AKK_PR("mutex-unlock!",      "petû-kanākim",     "𒂍𒁀𒌑")   /* petûm = open, reused */
/* maṣûm: "to suffice, to be possible" — genuine, fits a non-blocking attempt. */
AKK_PR("mutex-trylock!",     "kanāk-maṣîm",      "𒁀𒌑𒉌")
AKK_PR("mutex?",             "kanākum?",         "𒁀𒌑?")
AKK_PR("with-mutex",         "ina-kanākim",      "𒀀𒁀𒌑")
/* dagālum: "to watch, to look out for" — genuine, a condvar waiter watches
 * for a signal. */
AKK_PR("make-condvar",       "epēš-dāgilim",     "𒇽𒅆𒉌")
AKK_PR("condvar-destroy!",   "ḫepû-dāgilim",     "𒇲𒅆𒉌")
AKK_PR("cond-wait!",         "dagālum",          "𒅆𒉌")
AKK_PR("cond-wait-timeout!", "dagāl-adannim",    "𒅆𒉌𒌑")   /* adannu = deadline, reused */
AKK_PR("cond-signal!",       "šūdû-dāgilim",     "𒅆𒉌𒅆")   /* šūdûm = announce, reused */
AKK_PR("cond-broadcast!",    "šūdû-dāgilim-kalāma", "𒅆𒉌𒅆𒉡") /* to ALL watchers, kalāma reused */
AKK_PR("condvar?",           "dāgilum?",         "𒅆𒉌?")
AKK_PR("make-semaphore",     "epēš-maṣîm",       "𒇽𒉌")
AKK_PR("semaphore-destroy!", "ḫepû-maṣîm",       "𒇲𒉌")
AKK_PR("sem-wait!",          "maṣûm",            "𒉌")
AKK_PR("sem-post!",          "nadān-maṣîm",      "𒁀𒉌")     /* nadānum = to give, reused */
/* mala: "as much as" (reused from partition-count) — a non-blocking
 * attempt takes as much permit as is currently available. */
AKK_PR("sem-trywait!",       "maṣû-mala",        "𒉌𒉡")
AKK_PR("sem-value",          "mīnu-maṣîm",       "𒈠𒉌")     /* mīnum = count, reused */
AKK_PR("semaphore?",         "maṣûm?",           "𒉌?")

/* ---- (curry git) ---- */
/* puḫur-ṭuppi: "assembly of tablets" (both roots reused) — a genuine-
 * feeling compound for a repository. */
AKK_PR("git-open",          "petû-puḫur-ṭuppi",      "𒂍𒅁𒌋𒌝")
AKK_PR("git-init",          "šurrû-puḫur-ṭuppi",     "𒋻𒅁𒌋𒌝")   /* šurrûm = begin, reused */
AKK_PR("git-clone",         "šanā-puḫur-ṭuppi",      "𒁀𒅁𒌋𒌝")   /* šanûm = other/duplicate, reused */
AKK_PR("git-close!",        "sakār-puḫur-ṭuppi",     "𒇲𒅁𒌋𒌝")
/* ittu: "sign, mark, present condition" — genuine, a fit for status. */
AKK_PR("git-status",        "itti-puḫur-ṭuppi",      "𒀸𒅁𒌋𒌝")
AKK_PR("git-head",          "rēš-puḫur-ṭuppi",       "𒊕𒅁𒌋𒌝")   /* rēšum = head, reused — literal match */
/* šiṭru: "inscription, writing" — genuine, the historical record. */
AKK_PR("git-log",           "šiṭir-puḫur-ṭuppi",     "𒌝𒁹𒅁𒌋𒌝")
AKK_PR("git-add!",          "šakān-puḫur-ṭuppi",     "𒁹𒅁𒌋𒌝")   /* šakānum = place/stage, reused */
AKK_PR("git-add-all!",      "šakān-puḫur-ṭuppi-kalāma", "𒁹𒅁𒌋𒌝𒉡")
AKK_PR("git-reset-file!",   "turru-puḫur-ṭuppi",     "𒄀𒅁𒌋𒌝")   /* turrum = turn back, reused */
/* kanākum (reused): a commit "seals" a snapshot, matching the JWT/mutex use. */
AKK_PR("git-commit!",       "kanāk-puḫur-ṭuppi",     "𒁀𒌑𒅁𒌋𒌝")
/* aḫu: "side, arm; branch (of a river/canal)" — genuine, and a fitting
 * irrigation-canal metaphor for a version-control branch. */
AKK_PR("git-branches",      "aḫū-puḫur-ṭuppi",       "𒅁𒌋𒌝𒁹")
AKK_PR("git-current-branch","aḫu-ina-qātim",         "𒅁𒌋𒌝𒁀")   /* qātu = hand, genuine: "in hand" */
AKK_PR("git-checkout!",     "ṣabāt-aḫim",            "𒅁𒌋𒁹")     /* ṣabātum = seize, reused */
/* banûm: "to build, to create" — genuine. */
AKK_PR("git-branch-create!","banû-aḫim",             "𒅁𒌋𒉡𒌋𒉡")
AKK_PR("git-diff",          "lā-mitḫārum",           "𒉡𒈠𒁹")
/* šaknu: passive participle of šakānu, "placed, set" — the staged diff. */
AKK_PR("git-diff-staged",   "lā-mitḫāru-šaknum",     "𒉡𒈠𒁹𒁹")
AKK_PR("git-tags",          "šumū-puḫur-ṭuppi",      "𒌋𒅁𒌋𒌝")   /* šumu = name, reused, pluralized */
AKK_PR("git-tag-create!",   "banû-šumim",            "𒅁𒌋𒉡𒁹")
/* rūqu: "distant, far" — genuine, a remote really is "the distant one". */
AKK_PR("git-remotes",       "rūqūtum",               "𒉡𒌋𒉡")
AKK_PR("git-fetch!",        "leqû-rūqim",            "𒅁𒉡𒌋")     /* leqûm = to take, reused */
AKK_PR("git-push!",         "šapār-rūqim",           "𒌝𒉡𒌋")     /* šapārum = to send, reused */

/* ---- (curry image) ---- */
/* ṣalmu: "image, statue, likeness" — genuine, exact fit. */
AKK_PR("image-load",            "leqû-ṣalmim",      "𒅁𒍪")
AKK_PR("image-save",            "šakān-ṣalmim",     "𒁹𒍪𒉡")
AKK_PR("image-make",            "banû-ṣalmim",      "𒉡𒌋𒍪")
/* rupšu: "width, breadth" — genuine. */
AKK_PR("image-width",           "rupuš-ṣalmim",     "𒌋𒁀𒍪")
/* šaqûm: "to be high" — genuine, root for height. */
AKK_PR("image-height",          "šaqût-ṣalmim",     "𒋻𒀸𒍪")
/* aḫu (reused, "side/branch"): an image's color channels are its "sides". */
AKK_PR("image-channels",        "aḫū-ṣalmim",       "𒅁𒌋𒍪")
AKK_PR("image-pixels",          "ṣibtū-ṣalmim",     "𒁹𒍪")     /* ṣibtu = sign/character, reused */
AKK_PR("image-ref",             "maḫār-ṣalmim",     "𒌝𒍪")
AKK_PR("image-set!",            "šakān-ṣibti-ṣalmim","𒁹𒁹𒍪")
AKK_PR("image-crop",            "ḫarāṣ-ṣalmim",     "𒇲𒌑𒍪")   /* ḫarāṣum = to cut, reused */
AKK_PR("image-scale",           "mašālu-ṣalmim",    "𒈠𒃲𒍪")   /* mašālum = resemble, reused */
/* nabalkutu: "to cross over, to overturn" — genuine verb, an exact fit. */
AKK_PR("image-flip-horizontal", "nabalkut-ṣalmim",  "𒉡𒁀𒍪")
AKK_PR("image-flip-vertical",   "nabalkut-ṣalmi-šaplānu", "𒉡𒁀𒍪𒆠") /* šaplānu = below, genuine */
/* peṣûm: "to be white, pale" — genuine adjective root. */
AKK_PR("image-grayscale",       "peṣû-ṣalmim",      "𒁀𒉡𒍪")
AKK_PR("image-format",          "zikru-ṣalmim",     "𒌋𒍪")     /* zikru = designation, reused */

/* ---- (curry mqtt) ---- */
/* bīt šipri: "house of messages" (both roots reused) — a fitting compound
 * for a broker connection. */
AKK_PR("mqtt-connect",      "erēb-bīt-šipri",        "𒂗𒂍𒉌𒌋")
AKK_PR("mqtt-connect*",     "erēb-bīt-šipri-šanûm",  "𒂗𒂍𒉌𒌋𒁀")
AKK_PR("mqtt-disconnect",   "waṣê-bīt-šipri",        "𒉡𒂍𒉌𒌋")
AKK_PR("mqtt-connected?",   "erēb-bīt-šipri?",       "𒂗𒂍𒉌𒌋?")
/* maqtu: passive participle of maqātu, "to fall" — genuine, for dropped
 * (undelivered) messages. */
AKK_PR("mqtt-dropped",      "maqtū-šipri",           "𒈠𒉌𒌋𒉡")
AKK_PR("mqtt-publish",      "šūdû-šipri",            "𒅆𒉌𒌋")     /* šūdûm = announce, reused */
AKK_PR("mqtt-subscribe",    "šeʾû-šipri",            "𒅆𒅁𒉌𒌋")   /* šeʾûm = to seek, reused */
/* pašārum: "to release, to loosen" — reused (also unquote's root). */
AKK_PR("mqtt-unsubscribe",  "pašār-šipri",           "𒉡𒉌𒌋")
AKK_PR("mqtt-receive",      "maḫār-šipri",           "𒌝𒉌𒌋𒉡")     /* maḫārum = receive, reused */
AKK_PR("mqtt-connect-tls",  "erēb-bīt-šipri-puzri",  "𒂗𒂍𒉌𒌋𒉡")

/* ---- (curry redis) ---- */
/* ḫamṭu: "quick, swift" — genuine adjective; redis is exactly a fast
 * key-value tablet-store, reusing ṭuppu (tablet) for a stored value. */
AKK_PR("redis-connect",     "erēb-ṭuppi-ḫamṭi",      "𒂗𒌝𒉡")
AKK_PR("redis-connect-tls", "erēb-ṭuppi-ḫamṭi-puzri","𒂗𒌝𒉡𒌋")
AKK_PR("redis-close!",      "sakār-ṭuppi-ḫamṭi",     "𒂍𒌝𒉡")
/* šulmu: "peace, well-being" — genuine, a health-check is asking after
 * well-being. */
AKK_PR("redis-ping",        "šapār-šulmim",          "𒌝𒉡𒂗")
/* ašābum: "to sit, to dwell" — genuine, selecting a DB is "dwelling" in it. */
AKK_PR("redis-select",      "ašāb-ṭuppi-ḫamṭi",      "𒀸𒌝𒉡")
AKK_PR("redis-command",     "epēš-ṭuppi-ḫamṭi",      "𒇽𒌝𒉡")
AKK_PR("redis-set!",        "šakān-ṭuppi-ḫamṭi",     "𒁹𒌝𒉡")
AKK_PR("redis-get",         "leqû-ṭuppi-ḫamṭi",      "𒅁𒌝𒉡")
AKK_PR("redis-del!",        "nasāḫ-ṭuppi-ḫamṭi",     "𒋻𒌝𒉡")
/* bašûm: "to exist, to be" — genuine. */
AKK_PR("redis-exists?",     "bašû-ṭuppi-ḫamṭi?",     "𒀸𒌝𒉡?")
AKK_PR("redis-incr!",       "matāḫ-ištēn",           "𒋻𒁹𒀸")   /* add one (ištēn, reused) */
AKK_PR("redis-incrby!",     "matāḫ-mala",            "𒋻𒁹𒉡")   /* add as-much-as (mala, reused) */
AKK_PR("redis-expire!",     "adan-ṭuppi-ḫamṭi",      "𒌑𒌝𒉡")   /* adannu, reused */
/* šīmtu: "fate, determined lifespan" — genuine, an apt fit for TTL. */
AKK_PR("redis-ttl",         "šīm-adannim",           "𒁹𒌑𒌝")
AKK_PR("redis-keys",        "šumū-ṭuppi-ḫamṭi",      "𒌋𒌝𒉡")
/* libbu: "heart, interior" — genuine, a hash's fields live "inside" it. */
AKK_PR("redis-hset!",       "šakān-libbi",           "𒁹𒉡𒂗")
AKK_PR("redis-hget",        "leqû-libbi",            "𒅁𒉡𒂗")
AKK_PR("redis-hgetall",     "leqû-libbi-kalāma",     "𒅁𒉡𒂗𒉡")
AKK_PR("redis-hdel!",       "nasāḫ-libbi",           "𒋻𒉡𒂗")
AKK_PR("redis-hkeys",       "šumū-libbi",            "𒌋𒉡𒂗")
AKK_PR("redis-hvals",       "ṭuppū-libbi",           "𒌝𒉡𒂗𒉡")
AKK_PR("redis-hexists?",    "bašû-libbi?",           "𒀸𒉡𒂗?")
/* nindabûm: "list" (reused) — a redis LIST really is one. */
AKK_PR("redis-lpush!",      "šakān-rēš-nindabîm",    "𒁹𒊕𒇽")
AKK_PR("redis-rpush!",      "šakān-zibbat-nindabîm", "𒁹𒆜𒇽")
AKK_PR("redis-lpop",        "leqû-rēš-nindabîm",     "𒅁𒊕𒇽")
AKK_PR("redis-rpop",        "leqû-zibbat-nindabîm",  "𒅁𒆜𒇽")
AKK_PR("redis-llen",        "mīnu-nindabîm",         "𒈠𒇽")
AKK_PR("redis-lrange",      "zittu-nindabîm",        "𒁀𒋻𒇽")
/* puḫru: "assembly" (reused) — a redis SET is a genuine assembly. */
AKK_PR("redis-sadd!",       "šakān-puḫri",           "𒁹𒉌")
AKK_PR("redis-srem!",       "nasāḫ-puḫri",           "𒋻𒉌")
/* zumru: "body; also members (of a group)" — genuine. */
AKK_PR("redis-smembers",    "zumur-puḫrim",          "𒂍𒉌")
AKK_PR("redis-sismember",   "zumur-puḫrim?",         "𒂍𒉌?")
AKK_PR("redis-scard",       "mīnu-puḫrim",           "𒈠𒉌𒉡")
/* manûm: "to count, to reckon" — genuine, fits a ranked/scored set. */
AKK_PR("redis-zadd!",       "šakān-manîm",           "𒁹𒈠𒉌")
AKK_PR("redis-zrange",      "zittu-manîm",           "𒁀𒋻𒈠𒉌")
AKK_PR("redis-zrange-withscores", "zittu-manî-kalāma","𒁀𒋻𒈠𒉌𒉡")
AKK_PR("redis-zscore",      "manûm",                 "𒈠𒉌𒉌")
AKK_PR("redis-zcard",       "mīnu-manîm",            "𒈠𒈠𒉌")
/* ašru: "place, location" (reused) — one's rank IS one's place. */
AKK_PR("redis-zrank",       "ašar-manîm",            "𒀀𒈠𒉌")
AKK_PR("redis-publish",     "šūdû-kalāma",           "𒅆𒉡𒉡")
AKK_PR("redis-flushdb",     "ḫepû-ṭuppi-ḫamṭi-kalāma","𒇲𒌝𒉡𒉡")
AKK_PR("redis-dbsize",      "mīnu-ṭuppi-ḫamṭim",     "𒈠𒌝𒉡")
AKK_PR("redis-info",        "ṭēm-ṭuppi-ḫamṭi",       "𒅆𒌝𒉡")

/* ---- (curry rpi) — GPIO / I2C / SPI / PWM / camera / UART / 1-wire / watchdog ---- */
/* bābu: "gate" (reused) — a GPIO pin genuinely is a gate. */
AKK_PR("gpio-open",      "petû-bābim",       "𒂍𒁀𒉌")
AKK_PR("gpio-read",      "amār-bābim",       "𒅆𒁀𒉌")
AKK_PR("gpio-write",     "šakān-bābim",      "𒁹𒁀𒉌")
AKK_PR("gpio-close",     "sakār-bābim",      "𒂍𒁀𒉌𒉡")
AKK_PR("gpio?",          "bāb-šipri?",           "𒂍𒃲𒉌?")
AKK_PR("gpio-wait-edge", "dagāl-bābim",      "𒅆𒉌𒁀𒉌")
AKK_PR("gpio-watch",     "naṣār-bābim",      "𒉡𒁀𒉌")
AKK_PR("gpio-unwatch",   "paṭār-bābim",      "𒇲𒁀𒉌")
/* nāṣiru: agent noun from naṣārum, "guardian, watcher" — genuine. */
AKK_PR("watcher?",       "nāṣirum?",         "𒉡𒁀𒉌?")
/* atappu: "irrigation ditch, channel" — genuine, apt for a comms bus. */
AKK_PR("i2c-open",       "petû-atappim",     "𒂍𒀀𒉌")
AKK_PR("i2c-read",       "amār-atappim",     "𒅆𒀀𒉌")
AKK_PR("i2c-write",      "šakān-atappim",    "𒁹𒀀𒉌")
AKK_PR("i2c-close",      "sakār-atappim",    "𒂍𒀀𒉌𒉡")
AKK_PR("i2c?",           "atappum?",         "𒀀𒉌?")
/* palgu: "canal, channel" — genuine, a second irrigation term for SPI. */
AKK_PR("spi-open",       "petû-palgim",      "𒂍𒉡𒉌")
AKK_PR("spi-transfer",   "šapār-palgim",     "𒌝𒉡𒉌")
AKK_PR("spi-close",      "sakār-palgim",     "𒂍𒉡𒉌𒉡")
AKK_PR("spi?",           "palgum?",          "𒉡𒉌?")
/* zīqu: "breath, breeze, waft" — genuine, evokes a rhythmic pulsed signal. */
AKK_PR("pwm-open",       "petû-zīqim",       "𒂍𒌋𒉌")
AKK_PR("pwm-set!",       "šakān-zīqim",      "𒁹𒌋𒉌")
/* napāḫu: "to blow, to kindle" — genuine verb, apt for enabling a pulse. */
AKK_PR("pwm-enable!",    "napāḫ-zīqim",      "𒈷𒌋𒉌")
/* pašāḫu: "to become calm, to rest" — genuine antonym of napāḫu. */
AKK_PR("pwm-disable!",   "pašāḫ-zīqim",      "𒉡𒌋𒉌")
AKK_PR("pwm-close",      "sakār-zīqim",      "𒂍𒌋𒉌𒉡")
AKK_PR("pwm?",           "zīqum?",           "𒌋𒉌?")
/* ṣalmu (image, reused) + nāṣiru (guardian/watcher, reused) — a camera
 * is the device that watches and keeps images. */
AKK_PR("camera-open",    "petû-ṣalmi-nāṣirim", "𒂍𒍪𒉡")
/* ṣabātum: "to seize" (reused) — to capture IS to seize. */
AKK_PR("camera-capture", "ṣabāt-ṣalmim",       "𒅁𒍪𒉌")
AKK_PR("camera-close",   "sakār-ṣalmi-nāṣirim","𒂍𒍪𒉡𒉡")
AKK_PR("camera?",        "ṣalmi-nāṣirum?",     "𒍪𒉡?")
AKK_PR("camera-width",   "rupuš-ṣalmi-nāṣirim","𒌋𒁀𒍪𒉡")
AKK_PR("camera-height",  "šaqût-ṣalmi-nāṣirim","𒋻𒀸𒍪𒉡")
AKK_PR("camera-format",  "zikru-ṣalmi-nāṣirim","𒌋𒍪𒉡")
/* egertu: "letter, message" — genuine noun for serial correspondence. */
AKK_PR("uart-open",       "petû-egertim",       "𒂍𒂗𒁹")
AKK_PR("uart-read",       "amār-egertim",       "𒅆𒂗𒁹")
AKK_PR("uart-write",      "šapār-egertim",      "𒌝𒂗𒁹𒉡")
AKK_PR("uart-read-line",  "amār-šiṭir-egertim", "𒅆𒁹𒂗𒁹")   /* šiṭru = line/writing, reused */
AKK_PR("uart-available?", "bašû-egertim?",      "𒀸𒂗𒁹?")
AKK_PR("uart-close",      "sakār-egertim",      "𒂍𒂗𒁹𒉡")
AKK_PR("uart?",           "egertum?",           "𒂗𒁹?")
/* qû: "thread, cord, string" — genuine, a literal fit for a 1-Wire bus. */
AKK_PR("w1-devices",     "nāṣirū-qîm",  "𒉡𒌋𒌑")
/* ummu: "heat, fever, warmth" — genuine noun covering temperature. */
AKK_PR("w1-temperature", "ummu-qîm",    "𒌋𒌑𒌑")
/* ebbu: "pure, unrefined, as-is" — genuine adjective, fits raw data. */
AKK_PR("w1-raw",         "ebbu-qîm",    "𒌋𒌑𒁀")
/* maṣṣartu: "watch, guard duty" (the act, distinct from nāṣiru the agent). */
AKK_PR("watchdog-open",    "petû-maṣṣartim",    "𒂍𒈧𒉌")
AKK_PR("watchdog-kick",    "šūdû-maṣṣartim",    "𒅆𒈧𒉌")   /* "announce I'm alive" */
AKK_PR("watchdog-timeout", "adan-maṣṣartim",    "𒌑𒈧𒉌")
AKK_PR("watchdog-close",   "sakār-maṣṣartim",   "𒂍𒈧𒉌𒉡")
AKK_PR("watchdog?",        "maṣṣartum?",        "𒈧𒉌?")
/* lē'u: "writing board" — genuine, a fitting metaphor for a circuit board. */
AKK_PR("rpi-model",      "zikru-lē'im",         "𒌋𒇲𒉌")
AKK_PR("rpi-serial",     "šumu-lē'im",          "𒌋𒇲𒉌𒉡")
/* ḫasīsu: "understanding, memory, wisdom" — genuine noun for memory. */
AKK_PR("rpi-memory-mb",  "mīnu-ḫasīsi-lē'im",   "𒈠𒄷𒇲")
AKK_PR("rpi-os-info",    "ṭēm-lē'im",           "𒅆𒇲𒉌")

/* ---- (curry f64vector) ---- */
/* minâtu: plural of mīnum, "measures, counts, dimensions" — genuine,
 * a fitting root for a numeric vector/array. */
AKK_PR("make-f64vector",     "epēš-minâtim",       "𒇽𒈠𒉡")
AKK_PR("f64vector",          "minâtum",            "𒈠𒉡")
AKK_PR("f64vector-copy",     "šutur-minâtim",      "𒁹𒁹𒈠𒉡")
AKK_PR("f64vector-iota",     "minât-mala",         "𒈠𒉡𒉌")
/* mīšaru: "equity, straightness, evenness" — genuine, apt for linspace. */
AKK_PR("f64vector-linspace", "minât-mīšarim",      "𒈠𒉡𒈠𒉌")
AKK_PR("f64vector?",         "minâtum?",           "𒈠𒉡?")
AKK_PR("f64vector-length",   "mīnu-minâtim",       "𒈠𒈠𒉡")
AKK_PR("f64vector-ref",      "maḫār-minâtim",      "𒌝𒈠𒉡")
AKK_PR("f64vector-set!",     "šakān-minâtim",      "𒁹𒈠𒉡")
AKK_PR("f64vector->list",    "minâtum-ana-nindabîm","𒈠𒉡𒀀𒇽")
AKK_PR("list->f64vector",    "nindabûm-ana-minâtim","𒇽𒀀𒈠𒉡")
AKK_PR("f64vector->vector",  "minâtum-ana-ṣindim", "𒈠𒉡𒀀𒀸")
AKK_PR("vector->f64vector",  "ṣindum-ana-minâtim", "𒀸𒀀𒈠𒉡")
AKK_PR("f64vector-fill!",    "malû-minâtim",       "𒌋𒁹𒈠𒉡")
AKK_PR("f64vector-scale!",   "šutakūl-minâtim",    "𒈧𒁹𒈠𒉡")
AKK_PR("f64vector-offset!",  "matāḫ-minâtim",      "𒋻𒁹𒈠𒉡")
AKK_PR("f64vector-fma!",     "šutakūl-matāḫ-minâtim","𒈧𒁹𒋻𒈠𒉡")
/* nakārum: "to become other" (reused) — a fitting sign-flip metaphor. */
AKK_PR("f64vector-neg!",     "nakār-minâtim",      "𒉡𒄿𒈠𒉡")
/* puluggu: "boundary, district" — genuine, clamping bounds a value. */
AKK_PR("f64vector-clamp!",   "pulug-minâtim",      "𒇲𒉡𒈠𒉡")
AKK_PR("f64vector-abs!",     "kīttu-minâtim",      "𒆠𒀸𒈠𒉡")
AKK_PR("f64vector-sqrt!",    "ibu-minâtim",        "𒅁𒁹𒈠𒉡")
AKK_PR("f64vector-exp!",     "napḫar-ṣīr-minâtim", "𒈷𒁹𒀸𒈠𒉡")
AKK_PR("f64vector-log!",     "naṭāl-ṣīr-minâtim",  "𒅆𒁹𒈠𒉡")
AKK_PR("f64vector-sin!",     "šapalti-ṣīr-minâtim","𒁹𒀸𒁹𒈠𒉡")
AKK_PR("f64vector-cos!",     "ašarēdi-ṣīr-minâtim","𒁹𒁹𒀸𒈠𒉡")
AKK_PR("f64vector-tan!",     "ippeš-minâtim",      "𒁹𒀸𒀸𒈠𒉡")
/* kilallān: "both (of two)" — genuine dual-number word, apt for binary
 * elementwise operations. */
AKK_PR("f64vector-add!",     "matāḫ-minâti-kilallān", "𒋻𒁹𒈠𒉡𒌋")
AKK_PR("f64vector-sub!",     "ḫarāṣ-minâti-kilallān", "𒇲𒌑𒈠𒉡𒌋")
AKK_PR("f64vector-mul!",     "šutakūl-minâti-kilallān","𒈧𒁹𒈠𒉡𒌋")
AKK_PR("f64vector-div!",     "zâzu-minâti-kilallān",  "𒈧𒈠𒉡𒌋")
AKK_PR("f64vector-sum",      "kamār-minâtim",      "𒃲𒈠𒉡𒌋")
/* gimru: "totality, entirety" — genuine. */
AKK_PR("f64vector-product",  "šutakūl-gimri-minâtim","𒈧𒁹𒌋𒈠𒉡")
AKK_PR("f64vector-min",      "ṣiḫru-minâtim",      "𒉡𒃲𒈠𒉡")
AKK_PR("f64vector-max",      "rabû-minâtim",       "𒃲𒈠𒉡𒉡")
AKK_PR("f64vector-mean",     "mitḫar-minâtim",     "𒈠𒋻𒈠𒉡")
AKK_PR("f64vector-dot",      "napḫar-kilallān-minâtim","𒈷𒁹𒌋𒈠𒉡𒉡")
AKK_PR("f64vector-norm",     "ibu-napḫar-minâtim", "𒅁𒁹𒈷𒁹𒈠𒉡")
AKK_PR("f64vector-argmin",   "ašar-ṣiḫri-minâtim", "𒀀𒉡𒃲𒈠𒉡")
AKK_PR("f64vector-argmax",   "ašar-rabîm-minâtim", "𒀀𒃲𒈠𒉡")
AKK_PR("f64vector-map",      "epēš-kalāma-minâtim","𒇽𒉡𒈠𒉡")
AKK_PR("f64vector-map2",     "epēš-kalāma-minâti-kilallān","𒇽𒉡𒈠𒉡𒌋")
AKK_PR("f64vector-for-each", "ana-kālāma-minâtim", "𒀀𒌋𒈠𒉡")
AKK_PR("f64vector-slice",    "zittu-minâtim",      "𒁀𒋻𒈠𒉡")
AKK_PR("f64vector-append",   "redû-minâtim",       "𒈠𒂗𒈠𒉡")
AKK_PR("f64vector-reverse",  "turru-minâtim",      "𒋻𒀀𒈠𒉡")
/* šutēšuru: "to put in order" — genuine, reused concept from base64. */
AKK_PR("f64vector-sort",     "šutēšur-minâtim",    "𒋻𒌋𒈠𒉡")
AKK_PR("f64vector=",         "mitḫar-minâtim?",    "𒈠𒋻𒈠𒉡?")

/* ---- (curry plplot) ---- */
/* uṣurtu: "drawing, plan, design" — genuine, a direct fit for a plot. */
AKK_PR("plot-init",  "šurrû-uṣurtim", "𒋻𒉡𒌋")
AKK_PR("plot-end",   "gamār-uṣurtim", "𒃲𒉡𒌋")
/* manzāzu: "position, station" — genuine, the device/station a plot renders to. */
AKK_PR("plot-device","manzāz-uṣurtim","𒈠𒉡𒌋𒉡")
AKK_PR("plot-output","waṣê-uṣurtim",  "𒉡𒉡𒌋")
AKK_PR("plot-font-size", "rabi-šiṭrim",     "𒃲𒁹𒉡𒌋")   /* size of the writing (šiṭru, reused) */
AKK_PR("plot-env",       "šitkun-uṣurtim",  "𒁹𒉡𒌋𒉡")     /* šitkunu, reused from sqlite-prepare */
/* eliš: "above, upward" — genuine adverb, suggests log-scale growth. */
AKK_PR("plot-env-log",   "šitkun-uṣurti-elîš","𒁹𒉡𒌋𒌋")
AKK_PR("plot-labels",    "šumū-uṣurtim",    "𒌋𒉡𒌋𒉡")
/* kilīlu: "wreath, frame, enclosure" — genuine, apt for a plot box. */
AKK_PR("plot-box",       "kilīl-uṣurtim",   "𒆜𒉡𒌋")
/* melammu: "radiance, splendor" — genuine noun, loosely covers appearance/color. */
AKK_PR("plot-color",     "melam-uṣurtim",   "𒈠𒇲𒈷𒉡𒌋")
AKK_PR("plot-color-rgb", "melam-uṣurti-šalšim", "𒈠𒇲𒈷𒉡𒌋𒋻")
/* qanû: "reed, stalk; a measuring rod/line" — genuine, apt for a drawn line. */
AKK_PR("plot-width",     "rupuš-qanîm",     "𒌋𒁀𒌋𒉡")
/* kutallu: "back, rear" — genuine, a fitting background metaphor. */
AKK_PR("plot-background-color", "melam-kutalli-uṣurtim", "𒈠𒇲𒈷𒉡𒌋𒉡")
AKK_PR("plot-line",      "qanûm",           "𒌋𒉡𒉌")
AKK_PR("plot-points",    "ṣibtū-uṣurtim",   "𒁹𒉡𒌋")
AKK_PR("plot-histogram", "kilīlū-mināti",   "𒆜𒉡𒌋𒈠𒉡")
/* ḫiṭītu (fault/error, reused) — error bars are exactly that: fault margins. */
AKK_PR("plot-error-y",   "ḫiṭīt-uṣurti-šaplānim", "𒄷𒉡𒌋𒆠")   /* šaplānu = below, genuine */
/* mēḫru: "opposite, counterpart, corresponding" — genuine, loosely the
 * horizontal counterpart to šaplānu's vertical. */
AKK_PR("plot-error-x",   "ḫiṭīt-uṣurti-mēḫrim",   "𒄷𒉡𒌋𒉡𒉡")
AKK_PR("plot-3d-init",    "šurrû-uṣurti-kibrātim",   "𒋻𒉡𒌋𒃲")
AKK_PR("plot-3d-box",     "kilīl-uṣurti-kibrātim",   "𒆜𒉡𒌋𒃲")
AKK_PR("plot-3d-line",    "qanû-kibrātim",           "𒌋𒉡𒉌𒃲")
/* pānu: "face" (reused from mv-e) — a surface is a face of space. */
AKK_PR("plot-3d-surface", "pānu-kibrātim-uṣurtim",   "𒅆𒃲𒉡𒌋")
/* riksu: "bond, link" (reused from neo4j) — a mesh is a network of links. */
AKK_PR("plot-3d-mesh",    "riksū-kibrāti-uṣurtim",   "𒇲𒁹𒃲𒉡𒌋")
AKK_PR("plot-subplot",    "zittu-uṣurtim",   "𒁀𒋻𒉡𒌋")
AKK_PR("plot-advance",    "alāk-uṣurtim",    "𒄿𒉡𒌋")
AKK_PR("plot-text",       "šaṭār-uṣurtim",   "𒌝𒉡𒌋𒉡")
AKK_PR("plot-mtex",       "šaṭār-uṣurti-kilīlim", "𒌝𒉡𒌋𒆜")
/* ebēbu: "to become clean/pure" — genuine verb, distinct from the
 * adjective ebbu used for w1-raw. */
AKK_PR("plot-clear",      "ebēb-uṣurtim",    "𒁀𒉡𒌋𒉡")
/* šūṣû: causative of waṣûm, "to bring out, force out" — genuine, apt for flush. */
AKK_PR("plot-flush",      "šūṣû-uṣurtim",    "𒉡𒉡𒌋𒉡")
AKK_PR("plot-version",    "zikru-uṣurtim",   "𒌋𒉡𒌋𒌋")
AKK_PR("plot-page-dimensions", "minât-uṣurtim", "𒈠𒉡𒉡𒌋")

/* ---- Surreal numbers ---- */

/* dāriš: "forever, for eternity" — appears in royal inscriptions as "ana dāriš"
 * = "for ever and ever."  The surreal numbers extend the number line into the
 * transfinite (ω) and the infinitesimal (ε = 1/ω). */
AKK_PR("surreal?",              "ša-dāriš?",           "𒀭𒁹?")   /* AN.DIŠ?   = is it eternal? */
AKK_PR("surreal-infinite?",     "dāriš?",              "𒀭𒀭?")   /* AN.AN?    = doubly eternal? */
AKK_PR("surreal-finite?",       "la-dāriš?",           "𒉡𒀭?")   /* NU.AN?    = not eternal */
AKK_PR("surreal-infinitesimal?","ṣiḫrum-ṣīrum?",       "𒉡𒉡𒀀?") /* NU.NU.A?  = supremely tiny? */
AKK_PR("surreal-real-part",     "ṣīrum-ša-dāriš",      "𒀭𒄿")    /* AN.I      = the standard going part */
AKK_PR("surreal-omega-part",    "ša-dāriš-kīnum",      "𒀭𒀭𒁹")  /* AN.AN.DIŠ = the ω-coefficient */
AKK_PR("surreal-epsilon-part",  "ša-ṣiḫrim",           "𒉡𒉡𒈷") /* NU.NU.ME  = the ε-essence */
AKK_PR("surreal-birthday",      "ūm-wulludim",         "𒌑𒅁")    /* UD.IB     = day-hold = birth-day */
AKK_PR("surreal-nterms",        "mīnum-ša-dāriš",      "𒀭𒈠")    /* AN.MA     = count of the eternal */
AKK_PR("surreal->number",       "ša-dāriš-ana-nikkassim","𒀭𒌑")  /* AN.UD     = eternal to temporal */
AKK_PR("make-surreal",          "epēšum-ša-dāriš",     "𒀭𒇽")    /* AN.LU2   = make the eternal */
AKK_PR("surreal-terms",         "nindabûm-ša-dāriš",   "𒀭𒌝")    /* AN.UM    = the eternal's tablets */

/* ---- Quantum superposition ---- */

/* kalāma: "everything, all things at once" — a quantum state holds all branches
 * simultaneously.  amārum: "to see, to look upon" — observation collapses the state. */
AKK_PR("superpose",       "kalāma",              "𒊕𒊕𒊕")  /* SAG.SAG.SAG = many-headed = all at once */
AKK_PR("quantum-uniform", "kalāma-mitḫārum",     "𒊕𒊕𒁹")  /* SAG.SAG.DIŠ = all-equal heads */
AKK_PR("observe",         "amārum",              "𒅆𒄿")    /* IGI.I = eye-going = to look upon */
AKK_PR("quantum?",        "kalāma?",             "𒊕𒊕?")   /* SAG.SAG? */
AKK_PR("quantum-states",  "kalāma-nindabûm",     "𒊕𒊕𒌝")  /* SAG.SAG.UM = all-states-tablet */
AKK_PR("quantum-n",       "mīnum-kalāma",        "𒊕𒊕𒄿")  /* SAG.SAG.I  = count-of-all */

/* ---- Multivectors / Clifford algebra ---- */

/* kibrātim: genitive of kibrātum, "the four quarters of the world" — the Babylonian
 * name for the totality of 3D space ("šar kibrāt arba'im" = king of the four quarters).
 * A multivector lives in the full Clifford algebra over that space. */
AKK_PR("make-mv",         "epēšum-kibrātim",     "𒆠𒃲𒇽")   /* KI.GAL.LU2 = great-space-make */
AKK_PR("mv?",             "kibrātim?",           "𒆠𒃲?")    /* KI.GAL?    = is it a great space? */
AKK_PR("mv-signature",    "ṣimdat-kibrātim",     "𒆠𒃲𒋻")   /* KI.GAL.TAR = space-signature */
AKK_PR("mv-ref",          "maḫārum-kibrātim",    "𒆠𒃲𒊕")   /* KI.GAL.SAG = space-head */
AKK_PR("mv-set!",         "šakānum-kibrātim",    "𒆠𒃲𒁹")   /* KI.GAL.DIŠ = place-in-space */
AKK_PR("mv+",             "matāḫum-kibrātim",    "𒆠𒃲𒋻𒁹") /* KI.GAL.TAR.DIŠ = space-add */
AKK_PR("mv-",             "ḫarāṣum-kibrātim",   "𒆠𒃲𒇲𒌑") /* KI.GAL.LAL.UD  = space-subtract */
AKK_PR("mv*",             "šutakūlum-kibrātim",  "𒆠𒃲𒈧")   /* KI.GAL.MAŠ     = geometric product */
AKK_PR("mv-scale",        "zâzum-kibrātim",      "𒆠𒃲𒈧𒁹") /* KI.GAL.MAŠ.DIŠ = space-scale */
AKK_PR("mv-wedge",        "ṣilippum-kibrātim",   "𒆠𒃲𒌋")   /* KI.GAL.U  = diagonal/outer product */
AKK_PR("mv-lcontract",    "ṣibûm-kibrātim",      "𒆠𒃲𒅁")   /* KI.GAL.IB = left-hold of space */
AKK_PR("mv-reverse",      "turrum-kibrātim",     "𒆠𒃲𒄀𒁹") /* KI.GAL.GI.DIŠ  = space-return */
AKK_PR("mv-involute",     "nakārum-kibrātim",    "𒆠𒃲𒉡𒄿") /* KI.GAL.NU.I    = space-become-other */
AKK_PR("mv-conjugate",    "mitḫurtum-kibrātim",  "𒆠𒃲𒈠𒋻") /* KI.GAL.MA.TAR  = space-complement */
AKK_PR("mv-dual",         "šanûm-kibrātim",      "𒆠𒃲𒁀𒀀") /* KI.GAL.BA.A    = the other of space */
AKK_PR("mv-grade",        "šinīpat-kibrātim",    "𒆠𒃲𒀸")   /* KI.GAL.AŠ2 = grade level */
AKK_PR("mv-scalar",       "ṣifrum-kibrātim",     "𒆠𒃲𒉡𒉡") /* KI.GAL.NU.NU   = zero-grade = scalar */
AKK_PR("mv-norm2",        "napḫarum-kibrātim",   "𒆠𒃲𒈷𒈷") /* KI.GAL.ME.ME   = squared-sum */
AKK_PR("mv-norm",         "ibum-kibrātim",       "𒆠𒃲𒅁𒁹") /* KI.GAL.IB.DIŠ  = space-side (ibum = square root) */
AKK_PR("mv-normalize",    "ibum-ṣīrum-kibrātim", "𒆠𒃲𒅁𒌑") /* KI.GAL.IB.UD   = supreme-side */
AKK_PR("mv-e",            "pānum-kibrātim",      "𒆠𒃲𒅆")   /* KI.GAL.IGI = face/eye of space = basis blade */
AKK_PR("mv-from-list",    "kibrātim-maḫārum",    "𒆠𒃲𒌝")   /* KI.GAL.UM  = space from tablet */
AKK_PR("quaternion->mv",  "rebûm-ana-kibrātim",  "𒆠𒃲𒂗")   /* KI.GAL.EN  = enter the great space */
AKK_PR("mv->quaternion",  "kibrātum-ana-rebîm",  "𒆠𒃲𒂗𒉡") /* KI.GAL.EN.NU = exit space to fourfold */

/* ---- Quaternions and Octonions ---- */

/* rebûm: "fourfold, the fourth" — a quaternion is the 4D hypercomplex number.
 * samānûm: "eightfold, the eighth" — an octonion has eight components. */
AKK_PR("make-quaternion", "epēšum-rebûm",        "𒅁𒈷")    /* IB.ME  = hold-essence = 4D */
AKK_PR("quaternion?",     "rebûm?",              "𒅁𒈷?")   /* IB.ME? = is it fourfold? */
AKK_PR("make-octonion",   "epēšum-samānûm",      "𒅁𒈷𒈷")  /* IB.ME.ME  = double-hold-essence = 8D */
AKK_PR("octonion?",       "samānûm?",            "𒅁𒈷𒈷?") /* IB.ME.ME? = is it eightfold? */
AKK_PR("octonion-ref",    "maḫārum-samānûm",     "𒅁𒈷𒊕")  /* IB.ME.SAG = eightfold-head */

/* ---- Procedures: lists (additional) ---- */

AKK_PR("make-list",    "epēšum-nindabîm",    "𒇽𒄿𒌝")   /* LU2.I.UM = person-going-tablet = make list */

/* ---- Procedures: process context (R7RS §6.13) ---- */

/* awātum bītim: "the word of the house" — awātum = word/command, bītum = house.
 * An environment variable is a named word belonging to the surrounding context. */
AKK_PR("get-environment-variable",  "awāt-bīti",    "𒂍𒈷")    /* E2.ME = house-essence = one env var */
AKK_PR("get-environment-variables", "awātāt-bīti",  "𒂍𒈷𒈷")  /* E2.ME.ME = all-house-essences */
/* aṣûm-dannum: "forceful exit" — aṣûm = to go out, dannum = strong/forceful */
AKK_PR("emergency-exit",            "aṣûm-dannum",  "𒄿𒃲")    /* I.GAL = going-great = urgent/forced exit */

/* ---- Procedures: time (R7RS §6.14) ---- */

/* Akkadian has no atomic time units below the day.  These use existing vocabulary
 * as modern neologisms: ṭarādum (a stroke/beat) for a jiffy, ūmum (day) for elapsed time. */
AKK_PR("current-second",     "ūmum-ēṭum-inanna",  "𒌑𒄿")   /* UD.I = day-going = elapsed seconds */
AKK_PR("current-jiffy",      "ṭarādum-inanna",     "𒌑𒁹")   /* UD.DIŠ = day-one = one time-beat now */
AKK_PR("jiffies-per-second", "ṭarādū-ina-ūmim",   "𒌑𒌑")   /* UD.UD = beats-per-elapsed analogue */

/* ---- Procedures: error objects (R7RS §6.11) ---- */

/* awāt-ḫiṭītim: "the word of the fault" — the error's message string */
AKK_PR("error-object-message",  "awāt-ḫiṭītim",   "𒄷𒌝")   /* ḪI.UM = fault-tablet = the error's word */
AKK_PR("read-error?",           "ḫiṭītum-šemûm?", "𒄷𒅆?")  /* ḪI.IGI? = fault-eye? = reading fault? */
AKK_PR("file-error?",           "ḫiṭītum-ṭuppi?", "𒄷𒂍?")  /* ḪI.E2? = fault-house? = tablet/file fault? */

/* ---- Procedures: (curry json) module ---- */
/* No genuine OB analogue for JSON — ṭuppum (tablet/document) already
 * covers "structured written record" well enough to reuse honestly. */
AKK_PR("json-parse",     "ṭuppu-šemûm",     "𒌝𒅆𒌋")   /* read a tablet-document */
AKK_PR("json-stringify", "ṭuppu-šaṭārum",   "𒌝𒌝𒁹")   /* write a tablet-document */

/* ---- Conditions and restarts (CL-style condition system) ---- */
/* Continuing the ḫiṭītum ("fault") root already used for error/error-object,
 * extended with genuine attribute/trace/remedy vocabulary. */

/* ṣabātum: "to seize, to take hold of" — a caught/signaled condition. */
AKK_PR("condition?",            "ḫiṭītu-ṣabtum?",     "𒄷𒅁?")   /* ḪI.IB? = held fault? */
/* zikru: "utterance, name, designation" — the condition's classification. */
AKK_PR("condition-type",        "zikru-ḫiṭītim",      "𒌋𒄷")    /* U.ḪI = the fault's designation */
/* atmû: "utterance, word" (distinct root from error-object-message's awātum,
 * since these are two different accessors on two different object kinds). */
AKK_PR("condition-message",     "atmû-ḫiṭītim",       "𒄷𒀸")    /* ḪI.AŠ2 = the fault's utterance */
/* simtu: "fitting quality, characteristic attribute" — genuine OB term for
 * an intrinsic property, a good fit for a condition's field set. */
AKK_PR("condition-fields",      "simāt-ḫiṭītim",      "𒄷𒈠𒈠")  /* ḪI.MA.MA = the fault's attributes */
AKK_PR("condition-field",       "simtu-ḫiṭītim",      "𒄷𒈠")    /* ḪI.MA = one attribute of the fault */
AKK_PR("condition-is-a?",       "zikru-mitḫārum?",    "𒌋𒄷𒈠?")  /* is its designation the same? */
/* šumu: "name" (reused root, as in symbol->string's šumum). */
AKK_PR("condition-code",        "šumu-ḫiṭītim",       "𒌋𒄷𒁹")   /* the fault's stable name */
/* redûm: "to follow, to trace" + ašru: "place, location" — the trail of
 * call-frame locations back to the signal site. */
AKK_PR("condition-backtrace",   "redû-ašrī",          "𒆠𒄷𒌋")   /* KI.ḪI.U = the trace of places */
/* terṣītu: "correction, remedy" — a restart is exactly that: an offered
 * way to correct and resume past the fault. */
AKK_PR("restart?",              "terṣītum?",          "𒋻𒄷?")    /* TAR.ḪI? = a decided remedy? */
AKK_PR("restart-name",          "šumu-terṣītim",      "𒌋𒋻𒄷")   /* the remedy's name */
AKK_PR("restart-description",   "atmû-terṣītim",      "𒋻𒄷𒀸")   /* the remedy's description */

#undef AKK_SF
#undef AKK_PR
