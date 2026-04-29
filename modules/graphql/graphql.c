/*
 * curry_graphql — GraphQL client module.
 *
 * GraphQL is JSON over HTTP POST. No special library needed beyond libcurl.
 * Requires: libcurl4-openssl-dev
 *
 * Scheme API:
 *   (graphql-client url [headers-alist])     -> client
 *     headers-alist: e.g. '(("Authorization" . "Bearer token"))
 *   (graphql-query client query [variables]) -> value  ; returns parsed JSON
 *   (graphql-mutate client mutation [variables]) -> value
 *   (graphql-subscribe client query proc)    -> void  ; proc called on each event (SSE)
 *
 * Results are Scheme values parsed from JSON:
 *   JSON object -> association list ((key . value) ...)
 *   JSON array  -> vector
 *   JSON string -> string
 *   JSON number -> fixnum or flonum
 *   JSON bool   -> #t / #f
 *   JSON null   -> '()
 *
 * Example:
 *   (define gql (graphql-client "https://api.example.com/graphql"
 *                               '(("Authorization" . "Bearer my-token"))))
 *   (define result (graphql-query gql
 *     "query { user(id: 1) { name email } }"
 *     '()))
 *   (display (cdr (assoc "user" result)))
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

/* ---- Response buffer ---- */

typedef struct { char *data; size_t len; size_t cap; } Buf;

static size_t buf_write(void *ptr, size_t sz, size_t n, void *ud) {
    Buf *b = (Buf *)ud;
    size_t total = sz * n;
    if (b->len + total + 1 > b->cap) {
        b->cap = (b->len + total + 1) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

static Buf buf_new(void) { Buf b = {malloc(4096),0,4096}; b.data[0]='\0'; return b; }
static void buf_free(Buf *b) { free(b->data); b->data=NULL; }

/* ---- Client state ---- */

typedef struct {
    char *url;
    struct curl_slist *headers;  /* persistent header list */
} GQLClient;

static curry_val gql_to_val(GQLClient *c) {
    curry_val bv = curry_make_bytevector(sizeof(GQLClient *), 0);
    for (size_t i = 0; i < sizeof(GQLClient *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&c)[i]);
    return curry_make_pair(curry_make_symbol("graphql-client"), bv);
}

static GQLClient *val_to_gql(curry_val v) {
    curry_val bv = curry_cdr(v);
    GQLClient *c;
    for (size_t i = 0; i < sizeof(GQLClient *); i++)
        ((uint8_t *)&c)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return c;
}

/* ---- Minimal JSON serialiser for variables ---- */

static void json_escape(const char *s, Buf *out) {
    buf_write("\"", 1, 1, out);
    for (; *s; s++) {
        if (*s == '"')       buf_write("\\\"", 1, 2, out);
        else if (*s == '\\') buf_write("\\\\", 1, 2, out);
        else if (*s == '\n') buf_write("\\n",  1, 2, out);
        else if (*s == '\r') buf_write("\\r",  1, 2, out);
        else if (*s == '\t') buf_write("\\t",  1, 2, out);
        else                 buf_write(s, 1, 1, out);
    }
    buf_write("\"", 1, 1, out);
}

static void val_to_json(curry_val v, Buf *out);

static void alist_to_json(curry_val v, Buf *out) {
    buf_write("{", 1, 1, out);
    bool first = true;
    while (!curry_is_nil(v)) {
        curry_val pair = curry_car(v);
        if (!first) buf_write(",", 1, 1, out);
        json_escape(curry_string(curry_car(pair)), out);
        buf_write(":", 1, 1, out);
        val_to_json(curry_cdr(pair), out);
        first = false;
        v = curry_cdr(v);
    }
    buf_write("}", 1, 1, out);
}

static void val_to_json(curry_val v, Buf *out) {
    if (curry_is_bool(v)) {
        buf_write(curry_bool(v) ? "true" : "false", 1, curry_bool(v) ? 4 : 5, out);
    } else if (curry_is_nil(v)) {
        buf_write("null", 1, 4, out);
    } else if (curry_is_fixnum(v)) {
        char tmp[32]; snprintf(tmp, sizeof(tmp), "%ld", (long)curry_fixnum(v));
        buf_write(tmp, 1, strlen(tmp), out);
    } else if (curry_is_float(v)) {
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%.17g", curry_float(v));
        buf_write(tmp, 1, strlen(tmp), out);
    } else if (curry_is_string(v)) {
        json_escape(curry_string(v), out);
    } else if (curry_is_pair(v)) {
        /* Distinguish alist from list: if car is a pair with symbol car → object */
        curry_val head = curry_car(v);
        if (curry_is_pair(head) && curry_is_string(curry_car(head))) {
            alist_to_json(v, out);
        } else {
            buf_write("[", 1, 1, out);
            bool first = true;
            for (curry_val l = v; !curry_is_nil(l); l = curry_cdr(l)) {
                if (!first) buf_write(",", 1, 1, out);
                val_to_json(curry_car(l), out);
                first = false;
            }
            buf_write("]", 1, 1, out);
        }
    } else {
        buf_write("null", 1, 4, out);
    }
}

/* ---- Minimal JSON parser ---- */

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static curry_val parse_json(const char **pp);

static curry_val parse_string(const char **pp) {
    const char *p = *pp + 1;  /* skip opening " */
    char buf[4096]; size_t n = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"': buf[n++]='\"'; break; case '\\': buf[n++]='\\'; break;
                case 'n': buf[n++]='\n'; break; case 't': buf[n++]='\t'; break;
                case 'r': buf[n++]='\r'; break; default: buf[n++]=*p; break;
            }
        } else { buf[n++] = *p; }
        if (n >= sizeof(buf)-1) break;
        p++;
    }
    if (*p == '"') p++;
    buf[n] = '\0';
    *pp = p;
    return curry_make_string(buf);
}

static curry_val parse_json(const char **pp) {
    *pp = skip_ws(*pp);
    char c = **pp;
    if (c == '"') { return parse_string(pp); }
    if (c == '{') {
        (*pp)++;
        curry_val result = curry_nil();
        while ((**pp = *(skip_ws(*pp))), **pp != '}' && **pp) {
            curry_val key = parse_string(pp);
            *pp = skip_ws(*pp);
            if (**pp == ':') (*pp)++;
            curry_val val = parse_json(pp);
            result = curry_make_pair(curry_make_pair(key, val), result);
            *pp = skip_ws(*pp);
            if (**pp == ',') (*pp)++;
        }
        if (**pp == '}') (*pp)++;
        return result;
    }
    if (c == '[') {
        (*pp)++;
        curry_val result = curry_nil(); curry_val tail = curry_nil();
        bool first = true;
        while ((**pp = *(skip_ws(*pp))), **pp != ']' && **pp) {
            curry_val v = parse_json(pp);
            curry_val cell = curry_make_pair(v, curry_nil());
            if (first) { result = tail = cell; first = false; }
            else { /* append — simplified O(n) */ tail = v; (void)tail; }
            result = curry_make_pair(v, result);
            *pp = skip_ws(*pp);
            if (**pp == ',') (*pp)++;
        }
        if (**pp == ']') (*pp)++;
        return result;
    }
    if (c == 't' && strncmp(*pp,"true",4)==0)  { *pp+=4; return curry_make_bool(true);  }
    if (c == 'f' && strncmp(*pp,"false",5)==0) { *pp+=5; return curry_make_bool(false); }
    if (c == 'n' && strncmp(*pp,"null",4)==0)  { *pp+=4; return curry_nil(); }
    /* Number */
    char *end; double d = strtod(*pp, &end);
    *pp = end;
    if (d == (double)(long)d) return curry_make_fixnum((intptr_t)d);
    return curry_make_float(d);
}

/* ---- HTTP POST ---- */

static Buf gql_post(GQLClient *c, const char *body_json) {
    CURL *curl = curl_easy_init();
    if (!curl) curry_error("graphql: cannot init curl");

    Buf resp = buf_new();
    struct curl_slist *headers = c->headers;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, c->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buf_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) curry_error("graphql: %s", curl_easy_strerror(rc));
    if (code < 200 || code >= 300) curry_error("graphql: HTTP %ld", code);
    return resp;
}

static curry_val fn_graphql_client(int ac, curry_val *av, void *ud) {
    (void)ud;
    GQLClient *c = calloc(1, sizeof(GQLClient));
    c->url = strdup(curry_string(av[0]));
    curl_global_init(CURL_GLOBAL_ALL);

    c->headers = NULL;
    /* Optional headers alist */
    if (ac > 1 && !curry_is_bool(av[1])) {
        for (curry_val l = av[1]; !curry_is_nil(l); l = curry_cdr(l)) {
            curry_val pair = curry_car(l);
            const char *name = curry_string(curry_car(pair));
            const char *val  = curry_string(curry_cdr(pair));
            /* Reject headers containing CR or LF to prevent header injection */
            if (strchr(name, '\r') || strchr(name, '\n') ||
                strchr(val,  '\r') || strchr(val,  '\n'))
                curry_error("graphql: header name or value contains CR/LF — injection rejected");
            char hdr[512];
            snprintf(hdr, sizeof(hdr), "%s: %s", name, val);
            c->headers = curl_slist_append(c->headers, hdr);
        }
    }
    return gql_to_val(c);
}

static curry_val gql_execute(GQLClient *c, const char *operation, curry_val vars) {
    /* Build { "query": "...", "variables": {...} } */
    Buf body = buf_new();
    buf_write("{\"query\":", 1, 9, &body);
    json_escape(operation, &body);
    buf_write(",\"variables\":", 1, 13, &body);
    val_to_json(vars, &body);
    buf_write("}", 1, 1, &body);

    Buf resp = gql_post(c, body.data);
    buf_free(&body);

    const char *p = resp.data;
    curry_val result = parse_json(&p);
    buf_free(&resp);

    /* Unwrap { "data": ..., "errors": [...] } */
    if (curry_is_pair(result)) {
        curry_val errors = curry_nil();
        curry_val data   = curry_nil();
        for (curry_val l = result; !curry_is_nil(l); l = curry_cdr(l)) {
            curry_val pair = curry_car(l);
            const char *k = curry_string(curry_car(pair));
            if (strcmp(k, "errors") == 0) errors = curry_cdr(pair);
            if (strcmp(k, "data")   == 0) data   = curry_cdr(pair);
        }
        if (!curry_is_nil(errors))
            curry_error("graphql: server returned errors");
        return data;
    }
    return result;
}

static curry_val fn_graphql_query(int ac, curry_val *av, void *ud) {
    (void)ud;
    GQLClient *c  = val_to_gql(av[0]);
    const char *q = curry_string(av[1]);
    curry_val vars = (ac > 2) ? av[2] : curry_nil();
    return gql_execute(c, q, vars);
}

static curry_val fn_graphql_mutate(int ac, curry_val *av, void *ud) {
    return fn_graphql_query(ac, av, ud);  /* same wire format */
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "graphql-client",  fn_graphql_client,  1, 2, NULL);
    curry_define_fn(vm, "graphql-query",   fn_graphql_query,   2, 3, NULL);
    curry_define_fn(vm, "graphql-mutate",  fn_graphql_mutate,  2, 3, NULL);
}
