/*
 * akkadian.h — Mandatory Akkadian (Standard Babylonian) error preambles.
 *
 * 𒀭 "ḫiṭītu" (fault/error) — the scribal tradition demands it.
 *
 * Transliteration follows standard Assyriological conventions.
 * Cuneiform prefix 𒀭 is the divine determinative (DINGIR), used
 * as an authenticating mark by Babylonian scribes.
 */

#ifndef CURRY_AKKADIAN_H
#define CURRY_AKKADIAN_H

#include <string.h>

typedef struct {
    const char *keyword;   /* match against error message */
    const char *akkadian;  /* transliterated Akkadian phrase */
    const char *gloss;     /* English gloss for the curious */
} AkkadianEntry;

/*
 * Match table: first keyword found in the error string wins.
 * Phrases are Standard Babylonian, middle period dialect.
 */
static const AkkadianEntry akkadian_table[] = {
    /* Binding / naming errors */
    { "unbound",        "šumu lā šakin",            "name is not established"         },
    { "undefined",      "šumu lā šakin",            "name is not established"         },
    { "not defined",    "šumu lā šakin",            "name is not established"         },

    /* Arity errors */
    { "too few",        "qalil ša pī",              "too few of the mouth [args]"     },
    { "too many",       "mādu ša pī",               "too many of the mouth [args]"    },
    { "argument",       "pūm lā kīniš",             "the mouth-count is not correct"  },

    /* Type errors */
    { "not a pair",     "lā qitnum",                "not a small thing [pair]"        },
    { "not a number",   "lā nikkassum",             "not a count"                     },
    { "not a string",   "lā ṭupšarrum",             "not a scribal tablet"            },
    { "not a procedure","lā pārisum",               "not a resolver/judge"            },
    { "not a list",     "lā nindabûm",              "not an offering-list"            },
    { "not a vector",   "lā nindabûm",              "not an offering-list"            },
    { "not a symbol",   "lā šumum",                 "not a name"                      },
    { "not a boolean",  "lā kēnum ū lā sarrum",     "neither truth nor falsehood"     },
    { "not a port",     "lā bābum",                 "not a gate"                      },
    { "not a char",     "lā ṣibûtum",               "not a sign"                      },
    { "not a",          "ṣimtum lā šalmat",         "the type-nature is not whole"    },

    /* Division */
    { "division by zero","ina ṣifri pašāṭum lā leqû","cannot erase with the void"    },
    { "divide by zero", "ina ṣifri pašāṭum lā leqû","cannot erase with the void"     },
    { "zero",           "ṣifrum ana pānim",         "the void stands before us"       },

    /* File / I/O errors */
    { "cannot open",    "ṭuppu lā petûm",           "the tablet cannot be opened"     },
    { "file",           "ṭuppu lā ibašši",          "the tablet does not exist"       },
    { "port",           "bābum sakrum",             "the gate is barred"              },
    { "read",           "lā ṭarādum ṭuppim",        "the tablet cannot be read"       },

    /* Recursion / stack */
    { "overflow",       "elûm mādu — qerbu kabit",  "too high — the inside is heavy"  },
    { "stack",          "šipṭu elînum",             "the nest has risen too high"     },
    { "recursion",      "tāru ša lā qīpu",          "returning without trust"         },

    /* Syntax */
    { "syntax",         "ṭupšarrum ikkib",          "the scribal form is taboo"       },
    { "invalid",        "lā damqu",                 "not good"                        },
    { "malformed",      "ṣalam lā šalim",           "the image is not complete"       },
    { "expected",       "lā kīma ṣimtim",           "not as the nature requires"      },

    /* Continuations */
    { "continuation",   "riksum ikpud",             "the binding has snapped"         },

    /* Actors */
    { "dead",           "ana erṣetim ittalak",      "it has gone to the underworld"   },
    { "mailbox",        "nēmettum malât",           "the message-pouch is full"       },

    /* Module */
    { "module",         "bīt ṭuppi lā ibašši",      "the tablet house does not exist" },
    { "import",         "erēbu ikkib",              "entry is forbidden"              },

    /* Catch-all */
    { NULL,             "ḫiṭītu rabîtum",           "great fault"                    },
};

/* Returns an Akkadian phrase appropriate for the given error message. */
static inline const char *akkadian_phrase(const char *errmsg) {
    if (!errmsg) return "ḫiṭītu rabîtum";
    for (const AkkadianEntry *e = akkadian_table; e->keyword; e++) {
        if (strstr(errmsg, e->keyword))
            return e->akkadian;
    }
    return "ḫiṭītu rabîtum";
}

/* Full formatted preamble: "𒀭 ḫiṭītu — <phrase>:" */
static inline void akkadian_preamble(char *buf, size_t bufsz, const char *errmsg) {
    const char *phrase = akkadian_phrase(errmsg);
    snprintf(buf, bufsz, "\xf0\x92\x80\xad ḫiṭītu \xe2\x80\x94 %s", phrase);
    /* 0xF0 0x92 0x80 0xAD = U+12009 CUNEIFORM SIGN AN (𒀭) in UTF-8 */
}

#endif /* CURRY_AKKADIAN_H */
