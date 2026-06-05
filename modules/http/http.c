/*
 * curry_http — General-purpose HTTP client module built on libcurl.
 *
 * Scheme API:
 *   (http-request method url)                   -> (status . body)
 *   (http-request method url headers)           -> (status . body)
 *   (http-request method url headers body)      -> (status . body)
 *
 *   method:  string — "GET", "POST", "PUT", "PATCH", "DELETE", etc.
 *   url:     string
 *   headers: alist of ("Name" . "value") string pairs, or '()
 *   body:    optional string (request body)
 *
 *   Returns: pair (fixnum-status . body-string)
 *   On network error: raises a Scheme error.
 *
 * Always returns the raw body even for non-2xx responses; callers
 * inspect the status code and handle errors themselves.
 */

#include <curry.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>

typedef struct { char *data; size_t len; size_t cap; } Buf;

static size_t buf_cb(const void *ptr, size_t sz, size_t n, void *ud) {
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

static Buf buf_new(void) {
    Buf b; b.cap = 4096; b.len = 0;
    b.data = malloc(b.cap); b.data[0] = '\0';
    return b;
}

static curry_val fn_http_request(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *method  = curry_string(av[0]);
    const char *url     = curry_string(av[1]);
    curry_val   hdrs_v  = (ac > 2) ? av[2] : curry_nil();
    const char *body    = (ac > 3 && curry_is_string(av[3])) ? curry_string(av[3]) : NULL;
    size_t      body_len = body ? strlen(body) : 0;

    static int curl_inited = 0;
    if (!curl_inited) { curl_global_init(CURL_GLOBAL_ALL); curl_inited = 1; }

    CURL *curl = curl_easy_init();
    if (!curl) curry_error("http: failed to init curl");

    Buf resp = buf_new();
    struct curl_slist *headers = NULL;

    /* User-supplied headers */
    for (curry_val l = hdrs_v; !curry_is_nil(l); l = curry_cdr(l)) {
        curry_val kv = curry_car(l);
        const char *name = curry_string(curry_car(kv));
        const char *val  = curry_string(curry_cdr(kv));
        /* Reject headers containing CR or LF */
        if (strchr(name, '\r') || strchr(name, '\n') ||
            strchr(val,  '\r') || strchr(val,  '\n'))
            curry_error("http: header contains CR/LF — injection rejected");
        char hdr[4096];
        snprintf(hdr, sizeof(hdr), "%s: %s", name, val);
        headers = curl_slist_append(headers, hdr);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buf_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
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
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        curry_error("http: %s — %s", method, curl_easy_strerror(rc));

    curry_val body_str = curry_make_string(resp.data);
    free(resp.data);
    return curry_make_pair(curry_make_fixnum((intptr_t)code), body_str);
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "http-request", fn_http_request, 2, 4, NULL);
}
