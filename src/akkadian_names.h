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
AKK_PR("write-string",    "šaṭārum-ṭuppam",    "𒅆𒌝")   /* IGI.UM = see-tablet */
AKK_PR("open-input-file", "petûm-ṭuppi-erēbim","𒂍𒂗")
AKK_PR("open-output-file","petûm-ṭuppi-waṣîm", "𒂍𒉡")
AKK_PR("close-port",      "sakārum",           "𒂍𒇲")   /* E2.LAL = bind the house */
AKK_PR("flush-output-port","pašārum-bābim",    "𒂍𒁀")
AKK_PR("eof-object?",     "qātum?",            "𒉡𒌝?")  /* NU.UM = no more tablet */

/* ---- Procedures: strings ---- */

AKK_PR("make-string",     "epēšum-ṭuppam",     "𒇽𒌝")
AKK_PR("string",          "ṭuppum",            "𒌑𒌝")   /* UD.UM = day-tablet */
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

/* ---- Procedures: characters ---- */

AKK_PR("char->integer",   "ṣibtum-ana-nikkassim","𒁀𒈷")   /* BA.ME = give essence */
AKK_PR("integer->char",   "nikkassum-ana-ṣibtim","𒈷𒁹")

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

#undef AKK_SF
#undef AKK_PR
