/*
 * X-macro list of pre-interned symbols.
 * Usage: #define SYM(var, str) ... then #include "symbol_list.h"
 */

/* Core special forms */
SYM(S_QUOTE,              "quote")
SYM(S_QUASIQUOTE,         "quasiquote")
SYM(S_UNQUOTE,            "unquote")
SYM(S_UNQUOTE_SPLICING,   "unquote-splicing")
SYM(S_DEFINE,             "define")
SYM(S_DEFINE_SYNTAX,      "define-syntax")
SYM(S_DEFINE_VALUES,      "define-values")
SYM(S_DEFINE_RECORD_TYPE, "define-record-type")
SYM(S_LAMBDA,             "lambda")
SYM(S_IF,                 "if")
SYM(S_BEGIN,              "begin")
SYM(S_SET,                "set!")

/* Let forms */
SYM(S_LET,                "let")
SYM(S_LET_STAR,           "let*")
SYM(S_LETREC,             "letrec")
SYM(S_LETREC_STAR,        "letrec*")
SYM(S_LET_VALUES,         "let-values")
SYM(S_LET_STAR_VALUES,    "let*-values")
SYM(S_LET_SYNTAX,         "let-syntax")
SYM(S_LETREC_SYNTAX,      "letrec-syntax")
SYM(S_SYNTAX_RULES,       "syntax-rules")

/* Conditionals */
SYM(S_AND,                "and")
SYM(S_OR,                 "or")
SYM(S_COND,               "cond")
SYM(S_CASE,               "case")
SYM(S_WHEN,               "when")
SYM(S_UNLESS,             "unless")
SYM(S_DO,                 "do")

/* Lazy evaluation */
SYM(S_DELAY,              "delay")
SYM(S_DELAY_FORCE,        "delay-force")
SYM(S_MAKE_PROMISE,       "make-promise")

/* Dynamic binding / control */
SYM(S_PARAMETERIZE,       "parameterize")
SYM(S_GUARD,              "guard")
SYM(S_INCLUDE,            "include")
SYM(S_COND_EXPAND,        "cond-expand")
SYM(S_VALUES,             "values")
SYM(S_CALL_WITH_VALUES,   "call-with-values")
SYM(S_CALL_CC,            "call/cc")
SYM(S_CALL_WITH_CC,       "call-with-current-continuation")

/* Module system */
SYM(S_DEFINE_LIBRARY,     "define-library")
SYM(S_IMPORT,             "import")
SYM(S_EXPORT,             "export")
SYM(S_ONLY,               "only")
SYM(S_EXCEPT,             "except")
SYM(S_RENAME,             "rename")
SYM(S_PREFIX,             "prefix")

/* Syntax helpers */
SYM(S_ELSE,               "else")
SYM(S_ARROW,              "=>")
SYM(S_DOT,                ".")
SYM(S_REST,               "...")

/* Actor / concurrency primitives */
SYM(S_SPAWN,              "spawn")
SYM(S_SEND,               "send!")
SYM(S_RECEIVE,            "receive")
SYM(S_SELF,               "self")
SYM(S_LINK,               "link!")
SYM(S_MONITOR,            "monitor!")

/* Symbolic / CAS */
SYM(S_SYMBOLIC,           "symbolic")

/* Common identifiers */
SYM(S_ERROR,              "error")
SYM(S_APPLY,              "apply")
SYM(S_MAP,                "map")
SYM(S_FOR_EACH,           "for-each")
