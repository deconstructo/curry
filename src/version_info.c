/* SRFI-176 version information.
 *
 * curry_version_alist() builds the association list that the Scheme
 * procedure (version-alist) returns; curry_version_print_lose() writes the
 * same list in LOSE (line-oriented S-expression) form for the -V flag.
 * Both read the same data, so -V and (version-alist) cannot differ.
 *
 * Build properties come from two generated headers (see CMakeLists.txt and
 * cmake/BuildInfo.cmake): build_static.h (platform, compiler, flags,
 * configure options) and build_dynamic.h (date, git state).
 *
 * A property whose value is unknown is left out, as the SRFI allows.
 */
#include "version_info.h"
#include "version.h"
#include "object.h"
#include "symbol.h"
#include "builtins.h"
#include "curry_features.h"
#include "modules.h"
#include "gc.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "build_static.h"
#include "build_dynamic.h"

/* SRFI numbers curry provides, ascending. Every N.sld or N.scm under
 * lib/curry/modules/srfi/ must appear here, plus the two built-in forms
 * 0 (cond-expand) and 61 (cond =>). tests/test_cli.sh checks the list
 * against the directory. */
static const int SRFI_NUMBERS[] = {
    0, 1, 4, 8, 9, 14, 18, 19, 26, 27, 28, 29, 31, 41, 45, 54, 59, 61, 64,
    69, 78, 90, 95, 98, 106, 111, 112, 113, 120, 125, 126, 128, 132, 133,
    141, 145, 158, 160, 170, 174, 176, 189, 194, 195, 208, 209, 210, 212,
    215, 224, 225, 227, 238, 252, 253, 263, 272, 273, 279,
};

static val_t make_str(const char *s) {
    size_t len = strlen(s);
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type = T_STRING; str->hdr.flags = 0; str->len = (uint32_t)len;
    str->hash = 0; str->orig_cap = (uint32_t)len; str->ext = NULL;
    memcpy(str->data, s, len);
    str->data[len] = '\0';
    return vptr(str);
}

static val_t sym(const char *name) { return sym_intern_cstr(name); }

/* (key v1 v2 ...) from an already-built value list. */
static val_t prop(const char *key, val_t values) {
    return scm_cons(sym(key), values);
}

static val_t prop_string(const char *key, const char *value) {
    return prop(key, scm_cons(make_str(value), V_NIL));
}

/* (key "s1" "s2" ...) from a NULL-terminated array of C strings. */
static val_t prop_strings(const char *key, const char *const *items) {
    int n = 0;
    while (items[n]) n++;
    val_t list = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        list = scm_cons(make_str(items[i]), list);
    return prop(key, list);
}

static val_t type_bits_entry(const char *name, size_t bytes) {
    return scm_cons(sym(name),
                    scm_cons(vfix((intptr_t)(bytes * CHAR_BIT)), V_NIL));
}

static val_t c_type_bits(void) {
    val_t list = V_NIL;
#define ENTRY(name, type) list = scm_cons(type_bits_entry(name, sizeof(type)), list)
    /* Built in reverse so the list reads char first. */
    ENTRY("pointer",   void *);
    ENTRY("size_t",    size_t);
    ENTRY("double",    double);
    ENTRY("float",     float);
    ENTRY("long-long", long long);
    ENTRY("long",      long);
    ENTRY("int",       int);
    ENTRY("short",     short);
    ENTRY("char",      char);
#undef ENTRY
    return prop("c.type-bits", list);
}

val_t curry_version_alist(void) {
    /* Collected in order, then reversed once at the end. */
    val_t acc = V_NIL;
#define ADD(p) acc = scm_cons((p), acc)

    ADD(prop_string("command", "curry"));
    ADD(prop_string("website", "https://github.com/deconstructo/curry"));
    ADD(prop_string("version", CURRY_VERSION));
    ADD(prop("languages", scm_cons(sym("scheme"), scm_cons(sym("r7rs"), V_NIL))));

    ADD(prop_string("build.date", CURRY_BUILD_DATE));
    ADD(prop_string("build.platform", CURRY_BUILD_PLATFORM));
    ADD(prop_strings("build.configure", curry_build_configure));
#ifdef CURRY_HAVE_GIT
#ifdef CURRY_BUILD_GIT_TAG
    ADD(prop_string("build.git.tag", CURRY_BUILD_GIT_TAG));
#endif
#ifdef CURRY_BUILD_GIT_BRANCH
    ADD(prop_string("build.git.branch", CURRY_BUILD_GIT_BRANCH));
#endif
    ADD(prop_string("build.git.commit", CURRY_BUILD_GIT_COMMIT));
    if (curry_build_git_modified[0])
        ADD(prop_strings("build.git.modified", curry_build_git_modified));
#endif

    ADD(prop("scheme.id", scm_cons(sym("curry"), V_NIL)));
    {
        val_t list = V_NIL;
        int n = (int)(sizeof(SRFI_NUMBERS) / sizeof(SRFI_NUMBERS[0]));
        for (int i = n - 1; i >= 0; i--)
            list = scm_cons(vfix(SRFI_NUMBERS[i]), list);
        ADD(prop("scheme.srfi", list));
    }
    ADD(prop("scheme.features", features_list()));
    {
        const char *const *dirs;
        int n = modules_search_dirs(&dirs);
        val_t list = V_NIL;
        for (int i = n - 1; i >= 0; i--)
            list = scm_cons(make_str(dirs[i]), list);
        if (n > 0) ADD(prop("scheme.path", list));
    }

    ADD(prop_string("c.version", CURRY_BUILD_C_VERSION));
    ADD(prop_strings("c.compile", curry_build_c_compile));
    ADD(prop_strings("c.link", curry_build_c_link));
    ADD(c_type_bits());
#undef ADD
    return scm_reverse(acc);
}

/* ---- LOSE output ---- */

/* LOSE strings have exactly two escapes, \" and \\, and may not contain a
 * newline. Control characters are replaced by a space. */
static void put_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 0x20 || *p == 0x7f) fputc(' ', out);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static void put_datum(FILE *out, val_t v) {
    if (vis_fixnum(v)) {
        fprintf(out, "%ld", (long)vunfix(v));
    } else if (vis_symbol(v)) {
        fputs(sym_cstr(v), out);
    } else if (vis_string(v)) {
        String *s = as_str(v);
        put_string(out, str_data(s));
    } else if (vis_pair(v)) {
        fputc('(', out);
        bool first = true;
        for (; vis_pair(v); v = vcdr(v)) {
            if (!first) fputc(' ', out);
            put_datum(out, vcar(v));
            first = false;
        }
        fputc(')', out);
    } else if (vis_nil(v)) {
        fputs("()", out);
    } else {
        fputs("#f", out);
    }
}

void curry_version_print_lose(FILE *out) {
    for (val_t alist = curry_version_alist(); vis_pair(alist); alist = vcdr(alist))
        {
            put_datum(out, vcar(alist));
            fputc('\n', out);
        }
}
