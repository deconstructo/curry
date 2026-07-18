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
