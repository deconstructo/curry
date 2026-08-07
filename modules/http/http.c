/*
 * curry_http — General-purpose HTTP client module built on libcurl.
 *
 * Scheme API:
 *   (http-request method url)                   -> (status . body)
 *   (http-request method url headers)           -> (status . body)
 *   (http-request method url headers body)      -> (status . body)
 *
 *   (http-request/headers method url)               -> (status headers body)
 *   (http-request/headers method url headers)        -> (status headers body)
 *   (http-request/headers method url headers body)   -> (status headers body)
 *
 *   method:  string — "GET", "POST", "PUT", "PATCH", "DELETE", etc.
 *   url:     string
 *   headers: alist of ("Name" . "value") string pairs, or '()
 *   body:    optional string (request body)
 *
 *   http-request returns: pair (fixnum-status . body-string)
 *   http-request/headers returns: list (fixnum-status headers-alist body-string),
 *     where headers-alist pairs have lowercased header names (HTTP header
 *     names are case-insensitive) — e.g. (assoc "etag" headers). Only
 *     headers from the final response are kept: the accumulated list is
 *     reset whenever a new status line arrives, so redirect hops
 *     (CURLOPT_FOLLOWLOCATION) don't leak stale headers from earlier hops.
 *
 *   On network error: raises a Scheme error.
 *
 * Always returns the raw body even for non-2xx responses; callers
 * inspect the status code and handle errors themselves.
 */

#include <curry.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>

typedef struct { char *data; size_t len; size_t cap; } Buf;

static size_t buf_cb(const void *ptr, size_t sz, size_t n, void *ud) {
    Buf *b = (Buf *)ud;
    size_t total = sz * n;
    if (b->len + total + 1 > b->cap) {
        size_t new_cap = (b->len + total + 1) * 2;
        char *grown = realloc(b->data, new_cap);
        if (!grown) return 0; /* abort transfer; do_request's caller reports the curl error */
        b->data = grown;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

static Buf buf_new(void) {
    Buf b; b.cap = 4096; b.len = 0;
    b.data = malloc(b.cap); b.data[0] = '\0';
    return b;
}

typedef struct { char *name; char *value; size_t name_len, value_len; } HdrPair;
typedef struct { HdrPair *items; size_t len, cap; } HdrList;

/* Hard cap on captured response headers — bounds memory growth against a
 * malicious/misbehaving server that sends an unreasonable header count. */
#define HTTP_MAX_HEADERS 4096

static void hdrlist_init(HdrList *h) { h->items = NULL; h->len = 0; h->cap = 0; }

static void hdrlist_clear(HdrList *h) {
    for (size_t i = 0; i < h->len; i++) {
        free(h->items[i].name);
        free(h->items[i].value);
    }
    h->len = 0;
}

static void hdrlist_free(HdrList *h) {
    hdrlist_clear(h);
    free(h->items);
    h->items = NULL;
    h->cap = 0;
}

/* libcurl invokes this once per raw header line, including the status
 * line and the blank line terminating each response's header block — and
 * once per hop when following redirects. Reset on every status line so
 * only the final response's headers survive. */
static size_t hdr_cb(char *buf, size_t sz, size_t n, void *ud) {
    HdrList *h = (HdrList *)ud;
    size_t total = sz * n;

    if (total >= 5 && strncasecmp(buf, "HTTP/", 5) == 0) {
        hdrlist_clear(h);
        return total;
    }

    char *colon = memchr(buf, ':', total);
    if (!colon) return total; /* status line already handled; blank line; malformed */

    size_t name_len = (size_t)(colon - buf);
    const char *val_start = colon + 1;
    size_t val_len = total - name_len - 1;
    while (val_len > 0 && *val_start == ' ') { val_start++; val_len--; }
    while (val_len > 0 && (val_start[val_len - 1] == '\r' || val_start[val_len - 1] == '\n'))
        val_len--;

    /* Abort the transfer (rather than silently dropping headers) if the
     * server sends more than we're willing to buffer — do_request's
     * caller sees this as a normal curl error and cleans up as usual. */
    if (h->len >= HTTP_MAX_HEADERS) return 0;

    if (h->len + 1 > h->cap) {
        size_t new_cap = h->cap ? h->cap * 2 : 8;
        HdrPair *grown = realloc(h->items, new_cap * sizeof(HdrPair));
        if (!grown) return 0;
        h->items = grown;
        h->cap = new_cap;
    }

    char *name = malloc(name_len + 1);
    char *val  = malloc(val_len + 1);
    if (!name || !val) { free(name); free(val); return 0; }

    memcpy(name, buf, name_len);
    name[name_len] = '\0';
    for (size_t i = 0; i < name_len; i++)
        name[i] = (char)tolower((unsigned char)name[i]);

    memcpy(val, val_start, val_len);
    val[val_len] = '\0';

    h->items[h->len].name = name;
    h->items[h->len].value = val;
    h->items[h->len].name_len = name_len;
    h->items[h->len].value_len = val_len;
    h->len++;
    return total;
}

/* Shared request/perform core. hdrs may be NULL to skip header capture
 * entirely (the original http-request path — no CURLOPT_HEADERFUNCTION
 * is installed, so its behavior is unchanged). */
static CURLcode do_request(const char *method, const char *url, curry_val hdrs_v,
                            const char *body, size_t body_len,
                            Buf *resp, HdrList *hdrs, long *code_out) {
    static int curl_inited = 0;
    if (!curl_inited) { curl_global_init(CURL_GLOBAL_ALL); curl_inited = 1; }

    CURL *curl = curl_easy_init();
    if (!curl) curry_error("http: failed to init curl");

    struct curl_slist *req_headers = NULL;

    for (curry_val l = hdrs_v; !curry_is_nil(l); l = curry_cdr(l)) {
        curry_val kv = curry_car(l);
        const char *name = curry_string(curry_car(kv));
        const char *val  = curry_string(curry_cdr(kv));
        if (strchr(name, '\r') || strchr(name, '\n') ||
            strchr(val,  '\r') || strchr(val,  '\n'))
            curry_error("http: header contains CR/LF — injection rejected");
        char hdr[4096];
        snprintf(hdr, sizeof(hdr), "%s: %s", name, val);
        req_headers = curl_slist_append(req_headers, hdr);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buf_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
    if (hdrs) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, hdr_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, hdrs);
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curry-http/1.0");

    if (body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    } else {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }

    CURLcode rc = curl_easy_perform(curl);
    *code_out = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, code_out);

    curl_slist_free_all(req_headers);
    curl_easy_cleanup(curl);
    return rc;
}

static curry_val fn_http_request(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *method   = curry_string(av[0]);
    const char *url      = curry_string(av[1]);
    curry_val   hdrs_v   = (ac > 2) ? av[2] : curry_nil();
    const char *body     = (ac > 3 && curry_is_string(av[3])) ? curry_string(av[3]) : NULL;
    size_t      body_len = body ? strlen(body) : 0;

    Buf resp = buf_new();
    long code = 0;
    CURLcode rc = do_request(method, url, hdrs_v, body, body_len, &resp, NULL, &code);

    if (rc != CURLE_OK)
        curry_error("http: %s — %s", method, curl_easy_strerror(rc));

    /* curry_make_string_n + resp.len, not curry_make_string: an HTTP
     * response body is arbitrary bytes (binary content types are common)
     * and may contain embedded NUL bytes; strlen-based construction would
     * silently truncate at the first one. */
    curry_val body_str = curry_make_string_n(resp.data, (uint32_t)resp.len);
    free(resp.data);
    return curry_make_pair(curry_make_fixnum((intptr_t)code), body_str);
}

static curry_val fn_http_request_headers(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *method   = curry_string(av[0]);
    const char *url      = curry_string(av[1]);
    curry_val   hdrs_v   = (ac > 2) ? av[2] : curry_nil();
    const char *body     = (ac > 3 && curry_is_string(av[3])) ? curry_string(av[3]) : NULL;
    size_t      body_len = body ? strlen(body) : 0;

    Buf resp = buf_new();
    HdrList hdrs; hdrlist_init(&hdrs);
    long code = 0;
    CURLcode rc = do_request(method, url, hdrs_v, body, body_len, &resp, &hdrs, &code);

    if (rc != CURLE_OK) {
        free(resp.data);
        hdrlist_free(&hdrs);
        curry_error("http: %s — %s", method, curl_easy_strerror(rc));
    }

    curry_val body_str = curry_make_string_n(resp.data, (uint32_t)resp.len);
    free(resp.data);

    curry_val hdr_alist = curry_nil();
    for (size_t i = hdrs.len; i-- > 0; ) {
        curry_val pair = curry_make_pair(
            curry_make_string_n(hdrs.items[i].name, (uint32_t)hdrs.items[i].name_len),
            curry_make_string_n(hdrs.items[i].value, (uint32_t)hdrs.items[i].value_len));
        hdr_alist = curry_make_pair(pair, hdr_alist);
    }
    hdrlist_free(&hdrs);

    return curry_make_pair(curry_make_fixnum((intptr_t)code),
             curry_make_pair(hdr_alist,
               curry_make_pair(body_str, curry_nil())));
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "http-request", fn_http_request, 2, 4, NULL);
    curry_define_fn(vm, "http-request/headers", fn_http_request_headers, 2, 4, NULL);
}
