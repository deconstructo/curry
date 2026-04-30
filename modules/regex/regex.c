/* POSIX regex module for Curry Scheme.
 * Uses public curry.h API only.
 * Regex handles are tagged pairs: (regex . bytevector-holding-RegexData*)
 * Requires libregex (system POSIX regex.h — glibc, musl, Darwin).
 * Call (regex-free rx) to release the compiled pattern before GC.
 */

#include <curry.h>
#include <regex.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    regex_t re;
    size_t  nmatch;   /* 0 if REG_NOSUB */
    int     compiled;
} RegexData;

/* ---- pointer packing into bytevector ---- */
static curry_val pack_ptr(void *ptr) {
    curry_val bv = curry_make_bytevector(sizeof(void *), 0);
    for (size_t i = 0; i < sizeof(void *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&ptr)[i]);
    return bv;
}
static void *unpack_ptr(curry_val bv) {
    void *ptr = NULL;
    for (size_t i = 0; i < sizeof(void *); i++)
        ((uint8_t *)&ptr)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return ptr;
}

static curry_val wrap_regex(RegexData *rx) {
    return curry_make_pair(curry_make_symbol("regex"), pack_ptr(rx));
}
static RegexData *get_regex(curry_val v, const char *ctx) {
    if (!curry_is_pair(v) ||
        !curry_is_symbol(curry_car(v)) ||
        strcmp(curry_symbol(curry_car(v)), "regex") != 0)
        curry_error("%s: expected regex handle", ctx);
    return (RegexData *)unpack_ptr(curry_cdr(v));
}

/* ---- (regex-compile pattern [flags]) → regex ---- */
static curry_val fn_regex_compile(int ac, curry_val *av, void *ud) {
    (void)ud;
    if (!curry_is_string(av[0]))
        curry_error("regex-compile: expected string pattern");
    int flags = REG_EXTENDED;
    if (ac >= 2 && curry_is_fixnum(av[1])) flags = (int)curry_fixnum(av[1]);

    RegexData *rx = malloc(sizeof(RegexData));
    if (!rx) curry_error("regex-compile: out of memory");
    rx->compiled = 0; rx->nmatch = 0;

    char errbuf[256];
    int rc = regcomp(&rx->re, curry_string(av[0]), flags);
    if (rc != 0) {
        regerror(rc, &rx->re, errbuf, sizeof(errbuf));
        free(rx);
        curry_error("regex-compile: %s", errbuf);
    }
    rx->compiled = 1;
    rx->nmatch   = (flags & REG_NOSUB) ? 0 : rx->re.re_nsub + 1;
    return wrap_regex(rx);
}

/* (regex-free rx) — release the compiled pattern */
static curry_val fn_regex_free(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    RegexData *rx = get_regex(av[0], "regex-free");
    if (rx->compiled) { regfree(&rx->re); rx->compiled = 0; }
    free(rx);
    return curry_void();
}

/* ---- helpers ---- */
#define MAX_GROUPS 32

static int do_exec(RegexData *rx, const char *str, int eflags,
                   regmatch_t *m, size_t nm) {
    return regexec(&rx->re, str, nm, m, eflags);
}

/* ---- (regex-match rx string [eflags]) → #f | list-of-(start . end) ---- */
static curry_val fn_regex_match(int ac, curry_val *av, void *ud) {
    (void)ud;
    RegexData *rx = get_regex(av[0], "regex-match");
    if (!curry_is_string(av[1])) curry_error("regex-match: expected string");
    int eflags = (ac >= 3 && curry_is_fixnum(av[2])) ? (int)curry_fixnum(av[2]) : 0;

    size_t nm = rx->nmatch < MAX_GROUPS ? rx->nmatch : MAX_GROUPS;
    regmatch_t m[MAX_GROUPS];
    int rc = do_exec(rx, curry_string(av[1]), eflags, m, nm);
    if (rc == REG_NOMATCH) return curry_make_bool(false);
    if (rc != 0) {
        char errbuf[256]; regerror(rc, &rx->re, errbuf, sizeof(errbuf));
        curry_error("regex-match: %s", errbuf);
    }
    curry_val result = curry_nil();
    for (size_t i = nm; i-- > 0; ) {
        curry_val start = m[i].rm_so >= 0 ? curry_make_fixnum(m[i].rm_so) : curry_make_bool(false);
        curry_val end   = m[i].rm_eo >= 0 ? curry_make_fixnum(m[i].rm_eo) : curry_make_bool(false);
        result = curry_make_pair(curry_make_pair(start, end), result);
    }
    return result;
}

/* ---- (regex-match-string rx string [eflags]) → #f | list-of-strings ---- */
static curry_val fn_regex_match_string(int ac, curry_val *av, void *ud) {
    (void)ud;
    RegexData *rx = get_regex(av[0], "regex-match-string");
    if (!curry_is_string(av[1])) curry_error("regex-match-string: expected string");
    const char *src = curry_string(av[1]);
    int eflags = (ac >= 3 && curry_is_fixnum(av[2])) ? (int)curry_fixnum(av[2]) : 0;

    size_t nm = rx->nmatch < MAX_GROUPS ? rx->nmatch : MAX_GROUPS;
    regmatch_t m[MAX_GROUPS];
    int rc = do_exec(rx, src, eflags, m, nm);
    if (rc == REG_NOMATCH) return curry_make_bool(false);
    if (rc != 0) {
        char errbuf[256]; regerror(rc, &rx->re, errbuf, sizeof(errbuf));
        curry_error("regex-match-string: %s", errbuf);
    }
    curry_val result = curry_nil();
    for (size_t i = nm; i-- > 0; ) {
        regoff_t so = m[i].rm_so, eo = m[i].rm_eo;
        curry_val s;
        if (so < 0) {
            s = curry_make_bool(false);
        } else {
            size_t len = (size_t)(eo - so);
            char *buf = malloc(len + 1);
            if (!buf) curry_error("regex-match-string: out of memory");
            memcpy(buf, src + so, len); buf[len] = '\0';
            s = curry_make_string(buf);
            free(buf);
        }
        result = curry_make_pair(s, result);
    }
    return result;
}

/* ---- (regex-replace rx string replacement [all?]) → string ---- */
static curry_val fn_regex_replace(int ac, curry_val *av, void *ud) {
    (void)ud;
    RegexData *rx = get_regex(av[0], "regex-replace");
    if (!curry_is_string(av[1])) curry_error("regex-replace: expected string");
    if (!curry_is_string(av[2])) curry_error("regex-replace: expected replacement string");
    const char *src  = curry_string(av[1]);
    const char *repl = curry_string(av[2]);
    int replace_all  = (ac >= 4 && curry_is_true(av[3]));

    size_t out_cap = strlen(src) * 2 + 64;
    char  *out     = malloc(out_cap);
    if (!out) curry_error("regex-replace: out of memory");
    size_t out_len = 0;

#define ENSURE(n) do { \
    while (out_len + (n) >= out_cap) { \
        out_cap *= 2; char *_t = realloc(out, out_cap); \
        if (!_t) { free(out); curry_error("regex-replace: out of memory"); } \
        out = _t; \
    } \
} while(0)

    size_t nm = rx->nmatch < MAX_GROUPS ? rx->nmatch : MAX_GROUPS;
    regmatch_t m[MAX_GROUPS];
    const char *pos = src;
    int did_one = 0;
    while (*pos) {
        if (!replace_all && did_one) {
            size_t rest = strlen(pos);
            ENSURE(rest); memcpy(out + out_len, pos, rest); out_len += rest; break;
        }
        int rc = regexec(&rx->re, pos, nm, m, pos == src ? 0 : REG_NOTBOL);
        if (rc == REG_NOMATCH) {
            size_t rest = strlen(pos);
            ENSURE(rest); memcpy(out + out_len, pos, rest); out_len += rest; break;
        }
        regoff_t so = m[0].rm_so, eo = m[0].rm_eo;
        ENSURE((size_t)so);
        memcpy(out + out_len, pos, (size_t)so); out_len += (size_t)so;
        for (const char *r = repl; *r; r++) {
            if (*r == '\\' && r[1] >= '1' && r[1] <= '9') {
                int gi = r[1] - '0';
                if ((size_t)gi < nm && m[gi].rm_so >= 0) {
                    size_t glen = (size_t)(m[gi].rm_eo - m[gi].rm_so);
                    ENSURE(glen);
                    memcpy(out + out_len, pos + m[gi].rm_so, glen); out_len += glen;
                }
                r++;
            } else {
                ENSURE(1); out[out_len++] = *r;
            }
        }
        pos += eo; did_one = 1;
        if (eo == so) { if (*pos) { ENSURE(1); out[out_len++] = *pos++; } else break; }
    }
#undef ENSURE
    out = realloc(out, out_len + 1);
    if (out) out[out_len] = '\0';
    curry_val s = curry_make_string(out ? out : "");
    free(out);
    return s;
}

/* ---- (regex-split rx string) → list-of-strings ---- */
static curry_val fn_regex_split(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    RegexData *rx = get_regex(av[0], "regex-split");
    if (!curry_is_string(av[1])) curry_error("regex-split: expected string");
    const char *src = curry_string(av[1]);

    size_t nm = rx->nmatch < MAX_GROUPS ? rx->nmatch : MAX_GROUPS;
    regmatch_t m[MAX_GROUPS];
    curry_val result = curry_nil();
    const char *pos = src;

    while (*pos) {
        int rc = regexec(&rx->re, pos, nm, m, pos == src ? 0 : REG_NOTBOL);
        if (rc == REG_NOMATCH || m[0].rm_eo == 0) {
            size_t len = strlen(pos);
            char *buf = malloc(len + 1);
            if (!buf) curry_error("regex-split: out of memory");
            memcpy(buf, pos, len + 1);
            result = curry_make_pair(curry_make_string(buf), result);
            free(buf);
            break;
        }
        regoff_t so = m[0].rm_so, eo = m[0].rm_eo;
        size_t len = (size_t)so;
        char *buf = malloc(len + 1);
        if (!buf) curry_error("regex-split: out of memory");
        memcpy(buf, pos, len); buf[len] = '\0';
        result = curry_make_pair(curry_make_string(buf), result);
        free(buf);
        pos += eo;
        if (eo == so) { if (*pos) pos++; else break; }
    }

    /* reverse */
    curry_val rev = curry_nil();
    while (!curry_is_nil(result)) {
        rev    = curry_make_pair(curry_car(result), rev);
        result = curry_cdr(result);
    }
    return rev;
}

/* ---- (regex? x) ---- */
static curry_val fn_regex_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(
        curry_is_pair(av[0]) &&
        curry_is_symbol(curry_car(av[0])) &&
        strcmp(curry_symbol(curry_car(av[0])), "regex") == 0);
}

/* ---- Module init ---- */
void curry_module_init(CurryVM *vm) {
#define DEF(n, f, mn, mx) curry_define_fn(vm, n, f, mn, mx, NULL)
    DEF("regex-compile",      fn_regex_compile,      1, 2);
    DEF("regex-free",         fn_regex_free,         1, 1);
    DEF("regex-match",        fn_regex_match,        2, 3);
    DEF("regex-match-string", fn_regex_match_string, 2, 3);
    DEF("regex-replace",      fn_regex_replace,      3, 4);
    DEF("regex-split",        fn_regex_split,        2, 2);
    DEF("regex?",             fn_regex_p,            1, 1);
#undef DEF

    curry_define_val(vm, "REG_EXTENDED", curry_make_fixnum(REG_EXTENDED));
    curry_define_val(vm, "REG_ICASE",    curry_make_fixnum(REG_ICASE));
    curry_define_val(vm, "REG_NOSUB",    curry_make_fixnum(REG_NOSUB));
    curry_define_val(vm, "REG_NEWLINE",  curry_make_fixnum(REG_NEWLINE));
    curry_define_val(vm, "REG_NOTBOL",   curry_make_fixnum(REG_NOTBOL));
    curry_define_val(vm, "REG_NOTEOL",   curry_make_fixnum(REG_NOTEOL));
}
