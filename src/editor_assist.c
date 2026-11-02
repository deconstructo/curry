/* Editor assistance shared by the LSP module and the Jupyter kernel.
 *
 * Hover text for a symbol, completion candidates (the generated builtin and
 * special-form tables plus names bound in the buffer itself), and a
 * recursion guard for feeding arbitrary text to the reader. Lifted out of
 * modules/lsp/lsp.c, which is a dlopen'd module and cannot be linked into
 * the standalone kernel binary; this file lives in curry_core so both can
 * use it. */
#include "editor_assist.h"

#include <curry.h>
#include "eval.h"    /* SCM_PROTECT, ExnHandler */
#include "env.h"     /* GLOBAL_ENV, frame_snapshot_bindings */
#include "symbol.h"  /* sym_cstr */
#include "object.h"
#include "reader.h"  /* scm_read */
#include "port.h"    /* port_open_input_string */
#include "value.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_symbols_gen.h"

/* ---- Depth guard: src/reader.c's read_list/read_datum recurse once per
 * open paren/bracket with no depth limit — fine for trusted script files,
 * but this module feeds arbitrary editor-buffer text into that recursive
 * reader on every keystroke. A document with tens of thousands of levels
 * of nesting (trivially producible, e.g. many nested parens) blows the
 * native call stack before any Scheme-level exception can fire, so
 * SCM_PROTECT can't catch it. Bail out before ever calling scm_read() if
 * nesting looks pathological.
 *
 * This is a conservative textual pre-scan, not a real parse: it tracks
 * paren/bracket depth while skipping line comments, nestable #| |#
 * block comments, and string literals the same way the real reader does,
 * so it won't misfire on deep-looking text inside a comment or string.
 * False positives (flagging a merely unusual file) are safe; the only
 * thing that matters is never under-counting real reader recursion depth.
 */
bool assist_nesting_too_deep(const char *text) {
    int depth = 0;
    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == ';') {
            while (*p && *p != '\n') p++;
            if (!*p) break;
            continue;
        }
        if (c == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
            if (!*p) break;
            continue;
        }
        if (c == '#' && p[1] == '|') {
            int bd = 1; p += 2;
            while (*p && bd > 0) {
                if (p[0] == '#' && p[1] == '|') { bd++; p += 2; }
                else if (p[0] == '|' && p[1] == '#') { bd--; p += 2; }
                else p++;
            }
            if (!*p) break;
            p--; /* compensate for the for-loop's p++ */
            continue;
        }
        if (c == '#' && p[1] == '\\') {
            /* Character literal: #\<char> or #\<name> (#\newline, #\x41, …).
               read_char_literal() in src/reader.c consumes exactly one
               character unconditionally — even if that character is " or ;
               — then keeps consuming while the reader's own is_delimiter()
               says no. Without this case, "#\"" would be misread by the
               generic string-skip branch above as opening an unterminated
               string, silently swallowing the rest of the buffer (including
               any real nesting in it) and defeating the whole guard. */
            p += 2;
            if (!*p) break;
            p++; /* the literal character itself, whatever it is */
            while (*p && !isspace((unsigned char)*p) &&
                   *p != '(' && *p != ')' && *p != '[' && *p != ']' &&
                   *p != '"' && *p != ';') p++;
            if (!*p) break;
            p--; /* compensate for the for-loop's p++ */
            continue;
        }
        if (c == '(' || c == '[') { if (++depth > ASSIST_MAX_READER_DEPTH) return true; }
        else if (c == ')' || c == ']') { if (depth > 0) depth--; }
    }
    return false;
}


/* ---- Local bindings: structural walk over the real reader's output ----
 *
 * Rather than re-implement comment/string skipping in a hand-rolled
 * tokenizer, this reads the buffer with the same scm_read() diagnostics
 * uses and walks the resulting curry_val trees looking for define/lambda/
 * let-family/do forms. That means a `;` comment or a string literal that
 * happens to contain the text "define" can never produce a phantom
 * binding — the reader already threw it away before we ever see it.
 */

static void add_name(AssistNames *out, const char *name) {
    if (out->n >= ASSIST_MAX_LOCAL_NAMES) return;
    for (int i = 0; i < out->n; i++) if (strcmp(out->names[i], name) == 0) return;
    char *dup = strdup(name);
    if (!dup) return; /* OOM: skip rather than store a NULL entry */
    out->names[out->n++] = dup;
}

void assist_names_free(AssistNames *out) {
    for (int i = 0; i < out->n; i++) free(out->names[i]);
}

/* Lambda-style formal list: proper list, dotted rest arg, or a single
   symbol standing for the whole (unlisted) argument list. */
static void collect_formals(curry_val f, AssistNames *out) {
    while (curry_is_pair(f)) {
        curry_val a = curry_car(f);
        if (curry_is_symbol(a)) add_name(out, curry_symbol(a));
        f = curry_cdr(f);
    }
    if (curry_is_symbol(f)) add_name(out, curry_symbol(f));
}

/* (let ((name val) ...) ...) / named let / let* / letrec / letrec* —
   all share the same "bindings list of (name val) pairs" shape, with
   named let additionally binding a leading symbol before the list. */
static void collect_let_bindings(curry_val rest, AssistNames *out) {
    if (curry_is_pair(rest) && curry_is_symbol(curry_car(rest))) {
        add_name(out, curry_symbol(curry_car(rest))); /* named let */
        rest = curry_cdr(rest);
    }
    if (!curry_is_pair(rest)) return;
    curry_val bindings = curry_car(rest);
    while (curry_is_pair(bindings)) {
        curry_val b = curry_car(bindings);
        if (curry_is_pair(b) && curry_is_symbol(curry_car(b)))
            add_name(out, curry_symbol(curry_car(b)));
        bindings = curry_cdr(bindings);
    }
}

/* assist_nesting_too_deep() already keeps scm_read() from ever handing this
   function a tree deeper than ASSIST_MAX_READER_DEPTH, but that pre-check living
   in a different function is exactly the kind of invariant that quietly
   stops holding after a future refactor — cap the recursion directly too. */
#define MAX_WALK_DEPTH 2000

static void walk_bindings(curry_val v, AssistNames *out, int depth) {
    if (depth > MAX_WALK_DEPTH) return;
    if (!curry_is_pair(v)) return;
    curry_val head = curry_car(v);
    if (curry_is_symbol(head)) {
        const char *h = curry_symbol(head);
        curry_val rest = curry_cdr(v);
        if (strcmp(h, "define") == 0 && curry_is_pair(rest)) {
            curry_val target = curry_car(rest);
            if (curry_is_pair(target) && curry_is_symbol(curry_car(target))) {
                add_name(out, curry_symbol(curry_car(target)));   /* (define (f x) ...) */
                collect_formals(curry_cdr(target), out);
            } else if (curry_is_symbol(target)) {
                add_name(out, curry_symbol(target));              /* (define x ...) */
            }
        } else if (strcmp(h, "define-syntax") == 0 && curry_is_pair(rest)) {
            curry_val target = curry_car(rest);
            if (curry_is_symbol(target)) add_name(out, curry_symbol(target));
        } else if (strcmp(h, "lambda") == 0 && curry_is_pair(rest)) {
            collect_formals(curry_car(rest), out);
        } else if ((strcmp(h, "let") == 0 || strcmp(h, "let*") == 0 ||
                    strcmp(h, "letrec") == 0 || strcmp(h, "letrec*") == 0)) {
            collect_let_bindings(rest, out);
        } else if (strcmp(h, "do") == 0 && curry_is_pair(rest)) {
            curry_val bindings = curry_car(rest);
            while (curry_is_pair(bindings)) {
                curry_val b = curry_car(bindings);
                if (curry_is_pair(b) && curry_is_symbol(curry_car(b)))
                    add_name(out, curry_symbol(curry_car(b)));
                bindings = curry_cdr(bindings);
            }
        }
    }
    /* Keep walking regardless — bindings nest arbitrarily deep. */
    walk_bindings(curry_car(v), out, depth + 1);
    curry_val rest = curry_cdr(v);
    while (curry_is_pair(rest)) { walk_bindings(curry_car(rest), out, depth + 1); rest = curry_cdr(rest); }
}

void assist_collect_local_names(const char *text, AssistNames *out) {
    if (assist_nesting_too_deep(text)) return; /* see assist_nesting_too_deep()'s comment */
    val_t port = port_open_input_string(text, (uint32_t)strlen(text));
    ExnHandler h;
    SCM_PROTECT(h,
        {
            for (;;) {
                val_t form = scm_read(port);
                if (form == V_EOF) break;
                walk_bindings(form, out, 0);
            }
        },
        { /* stop at the first syntax error — whatever was collected
             before it is still a useful (if partial) completion set */ }
    );
}


/* ---- Hover: token-at-position, looked up in the generated symbol table ---- */

static bool is_delim(unsigned char c) {
    return c == '\0' || c == '\n' || c == '\r' || c == ' ' || c == '\t' ||
           c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '\'' || c == '`' || c == '"' || c == ',' || c == ';';
}

char *assist_token_at(const char *text, long line, long character) {
    const char *p = text;
    long cur = 0;
    while (cur < line && *p) { if (*p == '\n') cur++; p++; }
    const char *line_start = p;
    const char *q = line_start;
    long col = 0;
    while (col < character && *q && *q != '\n') { q++; col++; }
    const char *s = q;
    while (s > line_start && !is_delim((unsigned char)*(s - 1))) s--;
    const char *e = q;
    while (*e && *e != '\n' && !is_delim((unsigned char)*e)) e++;
    if (e == s) return NULL;
    size_t len = (size_t)(e - s);
    char *tok = malloc(len + 1);
    memcpy(tok, s, len);
    tok[len] = '\0';
    return tok;
}

bool assist_describe(const char *tok, char *out, size_t cap) {
    for (int i = 0; i < lsp_forms_n; i++)
        if (strcmp(lsp_forms[i].name, tok) == 0) {
            snprintf(out, cap, "**%s** — %s", lsp_forms[i].name, lsp_forms[i].kind);
            return true;
        }
    for (int i = 0; i < lsp_builtins_n; i++)
        if (strcmp(lsp_builtins[i].name, tok) == 0) {
            snprintf(out, cap, "**%s** — %s", lsp_builtins[i].name, lsp_builtins[i].kind);
            return true;
        }
    for (int i = 0; i < lsp_akk_forms_n; i++)
        if (strcmp(lsp_akk_forms[i].syn, tok) == 0) {
            snprintf(out, cap, "**%s** — Akkadian synonym for `%s` (%s)",
                     tok, lsp_akk_forms[i].canon, lsp_akk_forms[i].kind);
            return true;
        }
    for (int i = 0; i < lsp_akk_builtins_n; i++)
        if (strcmp(lsp_akk_builtins[i].syn, tok) == 0) {
            snprintf(out, cap, "**%s** — Akkadian synonym for `%s` (%s)",
                     tok, lsp_akk_builtins[i].canon, lsp_akk_builtins[i].kind);
            return true;
        }
    return false;
}


/* ---- Completion candidates ---- */

void assist_candidates(const char *text, AssistCandidateFn fn, void *ud) {
    for (int i = 0; i < lsp_forms_n; i++)
        fn(lsp_forms[i].name, ASSIST_KEYWORD, lsp_forms[i].kind, ud);
    for (int i = 0; i < lsp_builtins_n; i++)
        fn(lsp_builtins[i].name, ASSIST_FUNCTION, lsp_builtins[i].kind, ud);
    for (int i = 0; i < lsp_akk_forms_n; i++)
        fn(lsp_akk_forms[i].syn, ASSIST_KEYWORD, lsp_akk_forms[i].canon, ud);
    for (int i = 0; i < lsp_akk_builtins_n; i++)
        fn(lsp_akk_builtins[i].syn, ASSIST_FUNCTION, lsp_akk_builtins[i].canon, ud);

    AssistNames locals = {0};
    assist_collect_local_names(text, &locals);
    for (int i = 0; i < locals.n; i++)
        fn(locals.names[i], ASSIST_VARIABLE, "local binding", ud);
    assist_names_free(&locals);
}

/* Names bound in the live global environment, for a REPL-style client
 * whose earlier cells defined things the current text doesn't mention.
 * Uses the same seqlock-validated snapshot as the REPL's ,env command:
 * actors may define globals concurrently. */
void assist_global_names(AssistCandidateFn fn, void *ud) {
    EnvFrame *f = as_env(GLOBAL_ENV);
    val_t *syms, *vals;
    uint32_t n = frame_snapshot_bindings(f, &syms, &vals);
    for (uint32_t i = 0; i < n; i++) {
        if (!vis_symbol(syms[i])) continue;
        fn(sym_cstr(syms[i]), vis_proc(vals[i]) ? ASSIST_FUNCTION : ASSIST_VARIABLE,
           "global binding", ud);
    }
}

/* ---- Token at a byte offset ---- */

bool assist_token_range(const char *text, size_t offset, size_t *start, size_t *end) {
    size_t len = strlen(text);
    if (offset > len) offset = len;
    size_t s = offset;
    while (s > 0 && !is_delim((unsigned char)text[s - 1])) s--;
    size_t e = offset;
    while (e < len && !is_delim((unsigned char)text[e])) e++;
    *start = s;
    *end = e;
    return e > s;
}
