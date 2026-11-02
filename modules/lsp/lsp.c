/*
 * curry_lsp — Language Server Protocol server module for Curry Scheme.
 *
 * First milestone: stdio transport, Content-Length framing (LSP's own
 * framing, distinct from MCP's newline-delimited stdio), diagnostics
 * driven by the real reader (src/reader.c), and hover text driven by the
 * generated symbol table (src/editor_symbols_gen.h, produced by
 * tools/gen-editor-syntax.py from the same C sources the editor grammars
 * are generated from).
 *
 * Handled methods:
 *   initialize, initialized, shutdown, exit
 *   textDocument/didOpen, textDocument/didChange (full sync),
 *   textDocument/didClose
 *   textDocument/hover
 *
 * Diagnostics are published after didOpen/didChange by running the reader
 * over the buffer and reporting the first read error found, if any.
 *
 * Scheme API:
 *   (lsp-serve)   — stdio, blocks until the client sends `exit`.
 */

#include <curry.h>
#include "eval.h"    /* SCM_PROTECT, ExnHandler, current_handler */
#include "object.h"  /* ErrorObj, vis_error, as_err */
#include "gc.h"      /* gc_register_thread */
#include "reader.h"  /* scm_read */
#include "port.h"    /* port_open_input_string, port_line */
#include "value.h"   /* V_EOF */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#include "editor_assist.h"  /* hover/completion tables, local-binding walk, depth guard */

#define MAX_DOCS 512

/* ---- Document store (full-text sync only, for the first milestone) ---- */

typedef struct {
    char  uri[512];
    char *text;
} Doc;

static Doc s_docs[MAX_DOCS];
static int s_ndocs = 0;

static Doc *find_doc(const char *uri) {
    for (int i = 0; i < s_ndocs; i++)
        if (strcmp(s_docs[i].uri, uri) == 0) return &s_docs[i];
    return NULL;
}

static Doc *open_doc(const char *uri) {
    Doc *d = find_doc(uri);
    if (d) return d;
    if (s_ndocs >= MAX_DOCS) {
        fprintf(stderr, "curry-lsp: dropping %s — %d open documents (limit reached)\n",
                uri, MAX_DOCS);
        return NULL;
    }
    d = &s_docs[s_ndocs++];
    snprintf(d->uri, sizeof(d->uri), "%s", uri);
    d->text = NULL;
    return d;
}

static void close_doc(const char *uri) {
    for (int i = 0; i < s_ndocs; i++) {
        if (strcmp(s_docs[i].uri, uri) != 0) continue;
        free(s_docs[i].text);
        s_docs[i] = s_docs[s_ndocs - 1];
        s_ndocs--;
        return;
    }
}

/* Returns false on strdup failure (OOM); caller must not use d->text then. */
static bool set_doc_text(Doc *d, const char *text) {
    char *copy = strdup(text);
    if (!copy) return false;
    free(d->text);
    d->text = copy;
    return true;
}

/* ---- String builder ---- */

typedef struct { char *buf; size_t len; size_t cap; } SB;

static void sb_init(SB *b) { b->cap = 256; b->len = 0; b->buf = malloc(b->cap); b->buf[0] = '\0'; }
static void sb_free(SB *b) { free(b->buf); }
static void sb_grow(SB *b, size_t n) {
    if (b->len + n + 1 <= b->cap) return;
    while (b->len + n + 1 > b->cap) b->cap *= 2;
    b->buf = realloc(b->buf, b->cap);
}
static void sb_c(SB *b, char c)        { sb_grow(b, 1); b->buf[b->len++] = c; b->buf[b->len] = '\0'; }
static void sb_s(SB *b, const char *s) { while (*s) sb_c(b, *s++); }
static void sb_n(SB *b, long n)        { char t[32]; snprintf(t, sizeof(t), "%ld", n); sb_s(b, t); }

static void sb_quoted(SB *b, const char *s) {
    sb_c(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  sb_s(b, "\\\""); break;
            case '\\': sb_s(b, "\\\\"); break;
            case '\n': sb_s(b, "\\n");  break;
            case '\r': sb_s(b, "\\r");  break;
            case '\t': sb_s(b, "\\t");  break;
            default:
                if (c < 0x20) { char t[8]; snprintf(t, sizeof(t), "\\u%04x", c); sb_s(b, t); }
                else sb_c(b, (char)c);
        }
    }
    sb_c(b, '"');
}

/* ---- Minimal JSON parser -> curry_val alists (mirrors modules/mcp) ---- */

static void skip_ws(const char **p) { while (**p && (unsigned char)**p <= ' ') (*p)++; }

/* True if p[0..n-1] are all present before the string's NUL terminator. */
static bool has_n_bytes(const char *p, int n) {
    for (int i = 0; i < n; i++) if (p[i] == '\0') return false;
    return true;
}

static curry_val parse_str(const char **p) {
    (*p)++;
    size_t cap = 128, len = 0; char *buf = malloc(cap);
    while (**p && **p != '"') {
        char c = *(*p)++;
        if (c == '\\' && **p == '\0') break; /* trailing backslash at EOF */
        if (c == '\\') {
            char e = *(*p)++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"';  break;
                case '\\': c = '\\'; break;
                case '/': c = '/';  break;
                case 'u': {
                    /* Emit raw UTF-8 for the codepoint. A high surrogate
                       (0xD800-0xDBFF) must be followed by \uXXXX low
                       surrogate (0xDC00-0xDFFF) — combine into one astral
                       codepoint, needed for cuneiform (U+12000-U+1247F)
                       which is outside the BMP and always sent as a pair
                       by strict JSON encoders (ensure_ascii=True, etc).
                       Every hex read is bounds-checked against the NUL
                       terminator first — a truncated \u escape at the end
                       of the buffer must not read past it. */
                    if (!has_n_bytes(*p, 4)) goto unterminated;
                    char hex[5] = { (*p)[0], (*p)[1], (*p)[2], (*p)[3], 0 };
                    *p += 4;
                    unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        (*p)[0] == '\\' && (*p)[1] == 'u' && has_n_bytes(*p + 2, 4)) {
                        char hex2[5] = { (*p)[2], (*p)[3], (*p)[4], (*p)[5], 0 };
                        unsigned lo = (unsigned)strtoul(hex2, NULL, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            *p += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    if (cp < 0x80) {
                        buf[len++] = (char)cp;
                    } else if (cp < 0x800) {
                        buf[len++] = (char)(0xC0 | (cp >> 6));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        buf[len++] = (char)(0xE0 | (cp >> 12));
                        buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[len++] = (char)(0xF0 | (cp >> 18));
                        buf[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                        buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                    }
                    if (len + 4 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                    continue;
                }
                default: break;
            }
        }
        buf[len++] = c;
        if (len + 4 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
unterminated:
    if (**p == '"') (*p)++;
    buf[len] = '\0';
    curry_val r = curry_make_string(buf);
    free(buf);
    return r;
}

/* Caps nesting depth so a message of thousands of consecutive '[' or '{'
   can't exhaust the native stack via unbounded recursion. */
#define JSON_MAX_DEPTH 500
static int s_json_depth = 0;

static curry_val parse_val(const char **p) {
    skip_ws(p);
    if (!**p) return curry_nil();
    if (**p == '"') return parse_str(p);
    if (**p == '{' || **p == '[') {
        if (s_json_depth >= JSON_MAX_DEPTH) { (*p)++; return curry_nil(); }
        s_json_depth++;
    }
    if (**p == '{') {
        (*p)++;
        curry_val al = curry_nil(); skip_ws(p);
        if (**p != '}') {
            while (**p) {
                skip_ws(p); if (**p != '"') break;
                curry_val k = parse_str(p); skip_ws(p);
                if (**p == ':') (*p)++;
                curry_val v = parse_val(p);
                al = curry_make_pair(curry_make_pair(k, v), al);
                skip_ws(p);
                if (**p == ',') { (*p)++; continue; }
                if (**p == '}') break;
            }
        }
        if (**p == '}') (*p)++;
        s_json_depth--;
        return al;
    }
    if (**p == '[') {
        (*p)++;
        curry_val elems[1024]; int n = 0; skip_ws(p);
        if (**p != ']') {
            while (**p && n < 1024) {
                elems[n++] = parse_val(p); skip_ws(p);
                if (**p == ',') { (*p)++; continue; }
                if (**p == ']') break;
            }
        }
        if (**p == ']') (*p)++;
        curry_val lst = curry_nil();
        for (int i = n - 1; i >= 0; i--) lst = curry_make_pair(elems[i], lst);
        s_json_depth--;
        return lst;
    }
    if (strncmp(*p, "true", 4) == 0)  { *p += 4; return curry_make_bool(true); }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; return curry_make_bool(false); }
    if (strncmp(*p, "null", 4) == 0)  { *p += 4; return curry_nil(); }
    char num[64]; int i = 0; bool fp = false;
    #define NUMW(c) do { if (i < (int)sizeof(num) - 1) num[i++] = (c); } while (0)
    if (**p == '-') NUMW(*(*p)++);
    while (isdigit((unsigned char)**p)) NUMW(*(*p)++);
    if (**p == '.') { fp = true; NUMW(*(*p)++); while (isdigit((unsigned char)**p)) NUMW(*(*p)++); }
    if (**p == 'e' || **p == 'E') {
        fp = true; NUMW(*(*p)++);
        if (**p == '+' || **p == '-') NUMW(*(*p)++);
        while (isdigit((unsigned char)**p)) NUMW(*(*p)++);
    }
    num[i] = '\0';
    #undef NUMW
    return fp ? curry_make_float(atof(num)) : curry_make_fixnum(atol(num));
}

static curry_val aget(curry_val al, const char *key) {
    while (curry_is_pair(al)) {
        curry_val kv = curry_car(al); al = curry_cdr(al);
        if (!curry_is_pair(kv)) continue;
        curry_val k = curry_car(kv);
        if (curry_is_string(k) && strcmp(curry_string(k), key) == 0) return curry_cdr(kv);
    }
    return curry_nil();
}

static bool has_key(curry_val al, const char *key) {
    while (curry_is_pair(al)) {
        curry_val kv = curry_car(al); al = curry_cdr(al);
        if (!curry_is_pair(kv)) continue;
        curry_val k = curry_car(kv);
        if (curry_is_string(k) && strcmp(curry_string(k), key) == 0) return true;
    }
    return false;
}

/* ---- Content-Length framed stdio transport ---- */

/* Reject a Content-Length above this before ever calling malloc. Real LSP
   messages (even whole-document didChange on a large file) stay well
   under 64 MB; anything past that is a hostile or corrupt client. */
#define LSP_MAX_MESSAGE (64L * 1024 * 1024)

static char *read_message(void) {
    long content_len = -1;
    char line[1024];
    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) return NULL;
        /* Blank line (just CRLF or LF) ends the header block. */
        if (line[0] == '\r' || line[0] == '\n') break;
        long v;
        if (sscanf(line, "Content-Length: %ld", &v) == 1) content_len = v;
    }
    if (content_len < 0 || content_len > LSP_MAX_MESSAGE) return NULL;
    char *body = malloc((size_t)content_len + 1);
    if (!body) return NULL;
    size_t got = fread(body, 1, (size_t)content_len, stdin);
    body[got] = '\0';
    return body;
}

static pthread_mutex_t s_out_lock = PTHREAD_MUTEX_INITIALIZER;

static void write_message(const char *json) {
    pthread_mutex_lock(&s_out_lock);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
    fflush(stdout);
    pthread_mutex_unlock(&s_out_lock);
}

static void sb_id(SB *b, curry_val id) {
    if (curry_is_fixnum(id)) sb_n(b, (long)curry_fixnum(id));
    else if (curry_is_string(id)) sb_quoted(b, curry_string(id));
    else sb_s(b, "null");
}

static void emit_result(curry_val id, const char *result_json) {
    SB b; sb_init(&b);
    sb_s(&b, "{\"jsonrpc\":\"2.0\",\"id\":"); sb_id(&b, id);
    sb_s(&b, ",\"result\":"); sb_s(&b, result_json); sb_s(&b, "}");
    write_message(b.buf); sb_free(&b);
}

static void emit_error(curry_val id, int code, const char *msg) {
    SB b; sb_init(&b);
    sb_s(&b, "{\"jsonrpc\":\"2.0\",\"id\":"); sb_id(&b, id);
    sb_s(&b, ",\"error\":{\"code\":"); sb_n(&b, code);
    sb_s(&b, ",\"message\":"); sb_quoted(&b, msg); sb_s(&b, "}}");
    write_message(b.buf); sb_free(&b);
}

static void emit_notification(const char *method, const char *params_json) {
    SB b; sb_init(&b);
    sb_s(&b, "{\"jsonrpc\":\"2.0\",\"method\":"); sb_quoted(&b, method);
    sb_s(&b, ",\"params\":"); sb_s(&b, params_json); sb_s(&b, "}");
    write_message(b.buf); sb_free(&b);
}

/* ---- Error text from a caught exception (mirrors modules/mcp) ---- */

static void exn_msg(curry_val exn, char *buf, size_t cap) {
    if (curry_is_string(exn))   { snprintf(buf, cap, "%s", curry_string(exn)); return; }
    if (vis_error(exn))         { snprintf(buf, cap, "%s", curry_string(as_err(exn)->message)); return; }
    if (curry_is_pair(exn) && curry_is_string(curry_cdr(exn)))
                                 { snprintf(buf, cap, "%s", curry_string(curry_cdr(exn))); return; }
    snprintf(buf, cap, "error");
}

/* ---- Diagnostics: run the real reader over the buffer ---- */

static void publish_diagnostics(const char *uri, const char *text) {
    SB diags; sb_init(&diags); sb_s(&diags, "[");

    if (assist_nesting_too_deep(text)) {
        sb_s(&diags, "{\"range\":{\"start\":{\"line\":0,\"character\":0},"
                     "\"end\":{\"line\":0,\"character\":999}},\"severity\":2,"
                     "\"source\":\"curry\",\"message\":");
        char msg[128];
        snprintf(msg, sizeof(msg), "nesting too deep to analyze (limit %d levels)",
                 ASSIST_MAX_READER_DEPTH);
        sb_quoted(&diags, msg);
        sb_s(&diags, "}");
        sb_s(&diags, "]");
        SB params; sb_init(&params);
        sb_s(&params, "{\"uri\":"); sb_quoted(&params, uri);
        sb_s(&params, ",\"diagnostics\":"); sb_s(&params, diags.buf); sb_s(&params, "}");
        emit_notification("textDocument/publishDiagnostics", params.buf);
        sb_free(&diags); sb_free(&params);
        return;
    }

    val_t port = port_open_input_string(text, (uint32_t)strlen(text));
    bool failed = false;
    char errbuf[512] = "";
    int err_line = 0;
    ExnHandler h;
    SCM_PROTECT(h,
        {
            for (;;) {
                val_t form = scm_read(port);
                if (form == V_EOF) break;
            }
        },
        {
            failed = true;
            /* g_reader_last_line is the line the failing top-level form
               started on (stamped before the parse attempt that threw),
               not wherever the reader's cursor ended up — port_line(port)
               here would overshoot by one whenever the source has a
               trailing newline past the unterminated form. */
            err_line = g_reader_last_line - 1;
            if (err_line < 0) err_line = 0;
            exn_msg(h.exn, errbuf, sizeof(errbuf));
        }
    );

    if (failed) {
        sb_s(&diags, "{\"range\":{\"start\":{\"line\":"); sb_n(&diags, err_line);
        sb_s(&diags, ",\"character\":0},\"end\":{\"line\":"); sb_n(&diags, err_line);
        sb_s(&diags, ",\"character\":999}},\"severity\":1,\"source\":\"curry\",\"message\":");
        sb_quoted(&diags, errbuf);
        sb_s(&diags, "}");
    }
    sb_s(&diags, "]");

    SB params; sb_init(&params);
    sb_s(&params, "{\"uri\":"); sb_quoted(&params, uri);
    sb_s(&params, ",\"diagnostics\":"); sb_s(&params, diags.buf); sb_s(&params, "}");
    emit_notification("textDocument/publishDiagnostics", params.buf);

    sb_free(&diags); sb_free(&params);
}

/* ---- Request handlers ---- */

static void handle_initialize(curry_val id) {
    const char *result =
        "{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true,"
        "\"completionProvider\":{\"resolveProvider\":false}},"
        "\"serverInfo\":{\"name\":\"curry-lsp\",\"version\":\"0.1.0\"}}";
    emit_result(id, result);
}

static void handle_did_open(curry_val params) {
    curry_val td = aget(params, "textDocument");
    curry_val uri_v = aget(td, "uri");
    curry_val text_v = aget(td, "text");
    if (!curry_is_string(uri_v) || !curry_is_string(text_v)) return;
    Doc *d = open_doc(curry_string(uri_v));
    if (!d) return;
    if (!set_doc_text(d, curry_string(text_v))) return;
    publish_diagnostics(d->uri, d->text);
}

static void handle_did_change(curry_val params) {
    curry_val td = aget(params, "textDocument");
    curry_val uri_v = aget(td, "uri");
    if (!curry_is_string(uri_v)) return;
    curry_val changes = aget(params, "contentChanges");
    if (!curry_is_pair(changes)) return;
    /* Full-document sync: last change in the array is the whole new text. */
    curry_val last = curry_nil();
    for (curry_val c = changes; curry_is_pair(c); c = curry_cdr(c)) last = curry_car(c);
    curry_val text_v = aget(last, "text");
    if (!curry_is_string(text_v)) return;
    Doc *d = open_doc(curry_string(uri_v));
    if (!d) return;
    if (!set_doc_text(d, curry_string(text_v))) return;
    publish_diagnostics(d->uri, d->text);
}

static void handle_did_close(curry_val params) {
    curry_val td = aget(params, "textDocument");
    curry_val uri_v = aget(td, "uri");
    if (curry_is_string(uri_v)) close_doc(curry_string(uri_v));
}

static void handle_hover(curry_val id, curry_val params) {
    curry_val td = aget(params, "textDocument");
    curry_val uri_v = aget(td, "uri");
    curry_val pos = aget(params, "position");
    curry_val line_v = aget(pos, "line");
    curry_val char_v = aget(pos, "character");
    if (!curry_is_string(uri_v) || !curry_is_fixnum(line_v) || !curry_is_fixnum(char_v)) {
        emit_result(id, "null");
        return;
    }
    Doc *d = find_doc(curry_string(uri_v));
    if (!d || !d->text) { emit_result(id, "null"); return; }
    char *tok = assist_token_at(d->text, (long)curry_fixnum(line_v), (long)curry_fixnum(char_v));
    if (!tok) { emit_result(id, "null"); return; }
    char note[256];
    bool found = assist_describe(tok, note, sizeof(note));
    free(tok);
    if (!found) { emit_result(id, "null"); return; }
    SB r; sb_init(&r);
    sb_s(&r, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
    sb_quoted(&r, note);
    sb_s(&r, "}}");
    emit_result(id, r.buf);
    sb_free(&r);
}

/* CompletionItemKind values (LSP spec): Function=3, Variable=6, Keyword=14 */
static void sb_completion_item(SB *b, const char *label, int kind, const char *detail) {
    sb_s(b, "{\"label\":"); sb_quoted(b, label);
    sb_s(b, ",\"kind\":"); sb_n(b, kind);
    if (detail) { sb_s(b, ",\"detail\":"); sb_quoted(b, detail); }
    sb_s(b, "}");
}

/* Returns the full candidate set every time rather than prefix-filtering
   server-side — standard practice for non-incremental completion providers;
   the client filters against the identifier it's currently typing. */
typedef struct { SB items; bool first; } CompletionCtx;

static void completion_item_cb(const char *label, int kind, const char *detail, void *ud) {
    CompletionCtx *c = ud;
    if (!c->first) sb_c(&c->items, ',');
    c->first = false;
    sb_completion_item(&c->items, label, kind, detail);
}

static void handle_completion(curry_val id, curry_val params) {
    curry_val td = aget(params, "textDocument");
    curry_val uri_v = aget(td, "uri");
    if (!curry_is_string(uri_v)) { emit_result(id, "[]"); return; }
    Doc *d = find_doc(curry_string(uri_v));
    if (!d || !d->text) { emit_result(id, "[]"); return; }

    CompletionCtx ctx = { .first = true };
    sb_init(&ctx.items);
    sb_s(&ctx.items, "[");
    assist_candidates(d->text, completion_item_cb, &ctx);
    SB items = ctx.items;
    sb_s(&items, "]");
    emit_result(id, items.buf);
    sb_free(&items);
}

/* ---- Top-level dispatch loop ---- */

static curry_val fn_lsp_serve(int argc, curry_val *argv, void *ud) {
    (void)argc; (void)argv; (void)ud;
    bool shutdown_received = false;

    for (;;) {
        char *body = read_message();
        if (!body) break;

        const char *p = body;
        curry_val req = parse_val(&p);
        free(body);
        if (!curry_is_pair(req)) continue;

        curry_val method_v = aget(req, "method");
        if (!curry_is_string(method_v)) continue;
        const char *method = curry_string(method_v);
        curry_val params = aget(req, "params");
        bool is_request = has_key(req, "id");
        curry_val id = aget(req, "id");

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(id);
        } else if (strcmp(method, "initialized") == 0) {
            /* notification, nothing to do */
        } else if (strcmp(method, "shutdown") == 0) {
            shutdown_received = true;
            if (is_request) emit_result(id, "null");
        } else if (strcmp(method, "exit") == 0) {
            return curry_make_fixnum(shutdown_received ? 0 : 1);
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            handle_did_open(params);
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            handle_did_change(params);
        } else if (strcmp(method, "textDocument/didClose") == 0) {
            handle_did_close(params);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            handle_hover(id, params);
        } else if (strcmp(method, "textDocument/completion") == 0) {
            handle_completion(id, params);
        } else if (is_request) {
            emit_error(id, -32601, "method not found");
        }
        /* unknown notification: ignore */
    }
    return curry_make_fixnum(0);
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "lsp-serve", fn_lsp_serve, 0, 0, NULL);
}
