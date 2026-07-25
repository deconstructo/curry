/*
 * mcp_auth.c — OAuth 2.0 authentication / authorisation for curry_mcp.
 *
 * Implements three modes selectable at runtime via (mcp-auth-mode! 'mode):
 *
 *   self-contained  RFC 6749 §4.4 Client Credentials
 *   introspect      RFC 7662 Token Introspection
 *   jwt             RFC 7519 JWT Bearer (HS256 or RS256)
 *
 * All cryptographic operations go through OpenSSL (mandatory dep).
 */

#include "mcp_auth.h"
#include <curry.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

/* =========================================================================
 * Global mode state
 * ========================================================================= */

static McpAuthMode s_mode = MCP_AUTH_NONE;

/* =========================================================================
 * Utility — UUID generation (RFC 4122 v4, via OpenSSL RAND_bytes)
 * ========================================================================= */

static void gen_uuid(char out[37]) {
    unsigned char b[16];
    RAND_bytes(b, 16);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
        b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}

/* SHA-256 of src, written as lowercase hex into out (must be ≥ 65 bytes). */
static void sha256_hex(const char *src, char out[65]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)src, strlen(src), digest);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out + 2 * i, "%02x", digest[i]);
    out[64] = '\0';
}

/* =========================================================================
 * Utility — URL-encoded form parsing
 * ========================================================================= */

static void url_decode(const char *src, char *dst, size_t cap) {
    size_t n = 0;
    while (*src && n + 1 < cap) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[n++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[n++] = ' ';
            src++;
        } else {
            dst[n++] = *src++;
        }
    }
    dst[n] = '\0';
}

/* Find key in an application/x-www-form-urlencoded body.
 * Copies URL-decoded value into val[0..cap-1].  Returns true if found. */
static bool form_get(const char *body, const char *key, char *val, size_t cap) {
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *eq = strchr(p, '=');
        if (!eq) break;
        size_t flen = (size_t)(eq - p);
        const char *amp = strchr(eq + 1, '&');
        size_t vlen = amp ? (size_t)(amp - eq - 1) : strlen(eq + 1);
        if (flen == klen && strncmp(p, key, klen) == 0) {
            char raw[1024]; size_t copy = vlen < sizeof(raw) - 1 ? vlen : sizeof(raw) - 1;
            memcpy(raw, eq + 1, copy); raw[copy] = '\0';
            url_decode(raw, val, cap);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

/* =========================================================================
 * Utility — minimal JSON field reader (no Curry values — auth hot path)
 * ========================================================================= */

/* Find "key": <value> in a flat JSON object.
 * Writes the raw value string (no quotes) into val[0..cap-1].
 * Returns true if found. */
static bool json_get(const char *json, const char *key, char *val, size_t cap) {
    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    bool quoted = (*p == '"');
    if (quoted) p++;
    size_t n = 0;
    while (*p && n + 1 < cap) {
        if (quoted && *p == '"') break;
        if (!quoted && (*p == ',' || *p == '}' || *p == '\n')) break;
        val[n++] = *p++;
    }
    val[n] = '\0';
    return true;
}

/* =========================================================================
 * Utility — simple blocking HTTP/HTTPS POST (used by introspection)
 * ========================================================================= */

typedef struct { char host[256]; int port; char path[512]; bool ssl; } ParsedUrl;

static bool parse_url(const char *url, ParsedUrl *out) {
    memset(out, 0, sizeof(*out));
    if (strncmp(url, "https://", 8) == 0) { out->ssl = true; url += 8; }
    else if (strncmp(url, "http://", 7) == 0) { out->ssl = false; url += 7; }
    else return false;
    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');
    size_t host_end = slash ? (size_t)(slash - url) : strlen(url);
    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - url);
        if (hlen >= sizeof(out->host)) return false;
        memcpy(out->host, url, hlen); out->host[hlen] = '\0';
        out->port = atoi(colon + 1);
    } else {
        if (host_end >= sizeof(out->host)) return false;
        memcpy(out->host, url, host_end); out->host[host_end] = '\0';
        out->port = out->ssl ? 443 : 80;
    }
    strncpy(out->path, slash ? slash : "/", sizeof(out->path) - 1);
    return out->host[0] != '\0';
}

/*
 * Blocking HTTP(S) POST.  Writes response body into resp_body[0..resp_cap-1].
 * extra_header may be NULL.  Returns true on HTTP 2xx.
 */
static bool http_post(const char *url, const char *post_body,
                      const char *extra_header,
                      char *resp_body, size_t resp_cap) {
    ParsedUrl pu;
    if (!parse_url(url, &pu)) return false;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", pu.port);

    /* TCP connect via getaddrinfo */
    struct addrinfo hints = {0}, *ai = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(pu.host, port_str, &hints, &ai) != 0) return false;
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) { freeaddrinfo(ai); return false; }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
        freeaddrinfo(ai); close(fd); return false;
    }
    freeaddrinfo(ai);

    SSL_CTX *ctx = NULL;
    SSL     *ssl_conn = NULL;

    if (pu.ssl) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { close(fd); return false; }
        SSL_CTX_set_default_verify_paths(ctx);
        ssl_conn = SSL_new(ctx);
        SSL_set_fd(ssl_conn, fd);
        SSL_set_tlsext_host_name(ssl_conn, pu.host);
        if (SSL_connect(ssl_conn) <= 0) {
            SSL_free(ssl_conn); SSL_CTX_free(ctx); close(fd); return false;
        }
    }

#define WRITE_ALL(buf, n) do { \
    size_t _n = (n); const char *_b = (buf); \
    if (pu.ssl) SSL_write(ssl_conn, _b, (int)_n); \
    else        (void)send(fd, _b, _n, 0); \
} while(0)

    size_t body_len = post_body ? strlen(post_body) : 0;
    char req_hdr[2048];
    int hdr_len = snprintf(req_hdr, sizeof(req_hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        pu.path, pu.host, body_len,
        extra_header ? extra_header : "");
    WRITE_ALL(req_hdr, (size_t)hdr_len);
    if (body_len) WRITE_ALL(post_body, body_len);
#undef WRITE_ALL

    /* Read response */
    char raw[8192]; size_t total = 0;
    while (total < sizeof(raw) - 1) {
        int r;
        if (pu.ssl) r = SSL_read(ssl_conn, raw + total, (int)(sizeof(raw) - 1 - total));
        else        r = (int)recv(fd, raw + total, sizeof(raw) - 1 - total, 0);
        if (r <= 0) break;
        total += (size_t)r;
    }
    raw[total] = '\0';

    if (ssl_conn) { SSL_shutdown(ssl_conn); SSL_free(ssl_conn); }
    if (ctx)      SSL_CTX_free(ctx);
    close(fd);

    /* Check HTTP status */
    bool ok2xx = (strncmp(raw, "HTTP/1.", 7) == 0 && raw[9] == '2');

    /* Skip headers — find double CRLF */
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) body_start += 4;
    else            body_start = raw;

    strncpy(resp_body, body_start, resp_cap - 1);
    resp_body[resp_cap - 1] = '\0';
    return ok2xx;
}

/* =========================================================================
 * Utility — write a complete HTTP response to fd
 * ========================================================================= */

static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *body) {
    char hdr[512]; size_t blen = body ? strlen(body) : 0;
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        status, status_text,
        content_type ? content_type : "application/json",
        blen);
    send(fd, hdr, (size_t)n, MSG_NOSIGNAL);
    if (blen) send(fd, body, blen, MSG_NOSIGNAL);
}

/* =========================================================================
 * Mode A — self-contained: RFC 6749 §4.4 Client Credentials
 * ========================================================================= */

#define MAX_CLIENTS   32
#define MAX_TOKENS   512
#define DEFAULT_TTL 3600

typedef struct { char id[128]; char secret[256]; } OAuthClient;

typedef struct {
    char   token[37];
    char   client_id[128];
    time_t expires_at;
    bool   active;
} AccessToken;

static OAuthClient     s_clients[MAX_CLIENTS];
static int             s_nclient    = 0;
static pthread_mutex_t s_client_mu  = PTHREAD_MUTEX_INITIALIZER;

static AccessToken     s_tokens[MAX_TOKENS];
static pthread_rwlock_t s_token_rw  = PTHREAD_RWLOCK_INITIALIZER;
static int             s_token_ttl  = DEFAULT_TTL;

static bool sc_issue_token(const char *client_id, char out[37]) {
    gen_uuid(out);
    time_t now   = time(NULL);
    bool   issued = false;
    pthread_rwlock_wrlock(&s_token_rw);
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (!s_tokens[i].active || s_tokens[i].expires_at <= now) {
            strncpy(s_tokens[i].token,     out,       36);
            strncpy(s_tokens[i].client_id, client_id, 127);
            s_tokens[i].expires_at = now + s_token_ttl;
            s_tokens[i].active     = true;
            issued = true;
            break;
        }
    }
    pthread_rwlock_unlock(&s_token_rw);
    return issued;
}

static bool sc_validate(const char *token) {
    time_t now = time(NULL);
    bool   ok  = false;
    pthread_rwlock_rdlock(&s_token_rw);
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (s_tokens[i].active && strcmp(s_tokens[i].token, token) == 0) {
            ok = s_tokens[i].expires_at > now;
            break;
        }
    }
    pthread_rwlock_unlock(&s_token_rw);
    return ok;
}

bool mcp_auth_has_token_endpoint(void) { return s_mode == MCP_AUTH_SELF_CONTAINED; }

void mcp_auth_handle_token_endpoint(int fd, const char *body, size_t body_len) {
    (void)body_len;

    char grant_type[64]="", client_id[128]="", client_secret[256]="";
    form_get(body, "grant_type",    grant_type,    sizeof(grant_type));
    form_get(body, "client_id",     client_id,     sizeof(client_id));
    form_get(body, "client_secret", client_secret, sizeof(client_secret));

    if (strcmp(grant_type, "client_credentials") != 0) {
        send_response(fd, 400, "Bad Request", NULL,
            "{\"error\":\"unsupported_grant_type\","
            "\"error_description\":\"Only client_credentials is supported\"}");
        return;
    }

    bool found = false;
    pthread_mutex_lock(&s_client_mu);
    for (int i = 0; i < s_nclient; i++) {
        if (strcmp(s_clients[i].id, client_id) == 0) {
            found = (strcmp(s_clients[i].secret, client_secret) == 0);
            break;
        }
    }
    pthread_mutex_unlock(&s_client_mu);

    if (!found) {
        send_response(fd, 401, "Unauthorized", NULL,
            "{\"error\":\"invalid_client\","
            "\"error_description\":\"Unknown client or wrong secret\"}");
        return;
    }

    char token[37];
    if (!sc_issue_token(client_id, token)) {
        send_response(fd, 503, "Service Unavailable", NULL,
            "{\"error\":\"server_error\","
            "\"error_description\":\"Token store is full — try again later\"}");
        return;
    }

    char body_out[256];
    snprintf(body_out, sizeof(body_out),
        "{\"access_token\":\"%s\",\"token_type\":\"Bearer\",\"expires_in\":%d}",
        token, s_token_ttl);
    send_response(fd, 200, "OK", NULL, body_out);
}

/* =========================================================================
 * Mode B1 — introspect: RFC 7662 Token Introspection
 * ========================================================================= */

#define MAX_ICACHE 256

typedef struct {
    char   token_hash[65];  /* SHA-256 hex — never stores raw tokens */
    bool   active;
    time_t cached_at;
} ICache;

static char            s_introspect_url[512]    = "";
static char            s_introspect_cred_id[128] = "";
static char            s_introspect_cred_sec[256] = "";
static int             s_introspect_cache_ttl   = 60;
static ICache          s_icache[MAX_ICACHE];
static pthread_mutex_t s_icache_mu = PTHREAD_MUTEX_INITIALIZER;

/* -1 = miss, 0 = cached inactive, 1 = cached active */
static int ic_lookup(const char *hash) {
    time_t now = time(NULL);
    int    ret = -1;
    pthread_mutex_lock(&s_icache_mu);
    for (int i = 0; i < MAX_ICACHE; i++) {
        if (strcmp(s_icache[i].token_hash, hash) == 0) {
            if (now - s_icache[i].cached_at < s_introspect_cache_ttl)
                ret = s_icache[i].active ? 1 : 0;
            break;
        }
    }
    pthread_mutex_unlock(&s_icache_mu);
    return ret;
}

static void ic_store(const char *hash, bool active) {
    pthread_mutex_lock(&s_icache_mu);
    int slot = 0;
    time_t oldest = LONG_MAX;
    for (int i = 0; i < MAX_ICACHE; i++) {
        if (!s_icache[i].token_hash[0]) { slot = i; break; }
        if (s_icache[i].cached_at < oldest) { oldest = s_icache[i].cached_at; slot = i; }
    }
    strncpy(s_icache[slot].token_hash, hash, 64);
    s_icache[slot].active    = active;
    s_icache[slot].cached_at = time(NULL);
    pthread_mutex_unlock(&s_icache_mu);
}

static bool introspect_validate(const char *token) {
    char hash[65]; sha256_hex(token, hash);
    int cached = ic_lookup(hash);
    if (cached >= 0) return cached == 1;

    /* Build Basic auth header if credentials are configured */
    char extra[384] = "";
    if (s_introspect_cred_id[0]) {
        /* Base64-encode id:secret using OpenSSL */
        char cred[512];
        int clen = snprintf(cred, sizeof(cred), "%s:%s",
                            s_introspect_cred_id, s_introspect_cred_sec);
        unsigned char b64[768];
        EVP_EncodeBlock(b64, (unsigned char *)cred, clen);
        snprintf(extra, sizeof(extra), "Authorization: Basic %s\r\n", b64);
    }

    char post_body[512], resp[2048];
    snprintf(post_body, sizeof(post_body), "token=%s", token);
    bool ok2xx = http_post(s_introspect_url, post_body, extra[0] ? extra : NULL,
                           resp, sizeof(resp));

    bool active = false;
    if (ok2xx) {
        char active_str[16] = "";
        json_get(resp, "active", active_str, sizeof(active_str));
        active = strcmp(active_str, "true") == 0;
    }

    ic_store(hash, active);
    return active;
}

/* =========================================================================
 * Mode B2 — JWT: RFC 7519 Bearer (HS256 or RS256)
 * ========================================================================= */

typedef enum { JWT_HS256 = 0, JWT_RS256 } JwtAlg;

static JwtAlg   s_jwt_alg        = JWT_HS256;
static char     s_jwt_secret[512]  = "";     /* HS256 */
static EVP_PKEY *s_jwt_pubkey      = NULL;   /* RS256 */
static char     s_jwt_issuer[256]  = "";
static char     s_jwt_audience[256] = "";

/* Base64url → raw bytes.  Returns decoded length, or 0 on error.
 *
 * `src` is attacker-controlled (a JWT header/payload/signature segment
 * straight from an Authorization header), and EVP_DecodeBlock() writes
 * exactly (slen+pad)/4*3 bytes into `dst` regardless of `cap` — that size
 * grows without bound with the length of `src`, while every caller here
 * passes a small fixed-size stack buffer. The size check against `cap`
 * used to run only AFTER EVP_DecodeBlock() had already written into dst,
 * i.e. after any overflow already happened — a check-after-write pattern
 * that's a stack buffer overflow waiting for a long enough token. Fixed
 * by computing the exact output size EVP_DecodeBlock() will use and
 * rejecting up front, before it's ever called. */
static size_t b64url_decode(const char *src, unsigned char *dst, size_t cap) {
    size_t slen = strlen(src);
    size_t pad  = (4 - slen % 4) % 4;
    size_t decoded_cap = (slen + pad) / 4 * 3;
    if (decoded_cap > cap) return 0;

    /* Convert base64url to standard base64 */
    char *b64 = malloc(slen + pad + 1);
    if (!b64) return 0;
    for (size_t i = 0; i < slen; i++) {
        if      (src[i] == '-') b64[i] = '+';
        else if (src[i] == '_') b64[i] = '/';
        else                    b64[i] = src[i];
    }
    /* Add padding */
    for (size_t i = 0; i < pad; i++) b64[slen + i] = '=';
    b64[slen + pad] = '\0';

    int n = EVP_DecodeBlock(dst, (unsigned char *)b64, (int)(slen + pad));
    free(b64);
    if (n < 0 || (size_t)n < pad) return 0;
    /* EVP_DecodeBlock pads with 0 bytes for '=' — subtract them */
    return (size_t)n - pad;
}

static bool jwt_verify_hs256(const char *hdr_b64, const char *pay_b64,
                              const char *sig_b64) {
    char msg[4096];
    int mlen = snprintf(msg, sizeof(msg), "%s.%s", hdr_b64, pay_b64);
    unsigned int expected_len = 32;
    unsigned char expected[32];
    HMAC(EVP_sha256(),
         (unsigned char *)s_jwt_secret, (int)strlen(s_jwt_secret),
         (unsigned char *)msg, (size_t)mlen,
         expected, &expected_len);

    unsigned char got[256];
    size_t got_len = b64url_decode(sig_b64, got, sizeof(got));
    if (got_len != 32) return false;
    return CRYPTO_memcmp(expected, got, 32) == 0;
}

static bool jwt_verify_rs256(const char *hdr_b64, const char *pay_b64,
                              const char *sig_b64) {
    if (!s_jwt_pubkey) return false;
    char msg[4096];
    int mlen = snprintf(msg, sizeof(msg), "%s.%s", hdr_b64, pay_b64);

    unsigned char sig[512];
    size_t sig_len = b64url_decode(sig_b64, sig, sizeof(sig));
    if (sig_len == 0) return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    bool ok = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, s_jwt_pubkey) == 1 &&
              EVP_DigestVerifyUpdate(ctx, msg, (size_t)mlen) == 1 &&
              EVP_DigestVerifyFinal(ctx, sig, sig_len) == 1;
    EVP_MD_CTX_free(ctx);
    return ok;
}

static bool jwt_check_claims(const char *payload_json) {
    /* Expiry */
    char exp_str[32] = "";
    if (json_get(payload_json, "exp", exp_str, sizeof(exp_str))) {
        time_t exp = (time_t)atol(exp_str);
        if (exp < time(NULL)) return false;
    }
    /* Issuer (optional) */
    if (s_jwt_issuer[0]) {
        char iss[256] = "";
        json_get(payload_json, "iss", iss, sizeof(iss));
        if (strcmp(iss, s_jwt_issuer) != 0) return false;
    }
    /* Audience (optional) */
    if (s_jwt_audience[0]) {
        char aud[256] = "";
        json_get(payload_json, "aud", aud, sizeof(aud));
        if (strcmp(aud, s_jwt_audience) != 0) return false;
    }
    return true;
}

static bool jwt_validate(const char *token) {
    /* Split into three dot-separated parts */
    const char *dot1 = strchr(token, '.');
    if (!dot1) return false;
    const char *dot2 = strchr(dot1 + 1, '.');
    if (!dot2) return false;

    /* Copy each part as a NUL-terminated string */
    size_t hlen = (size_t)(dot1 - token);
    size_t plen = (size_t)(dot2 - dot1 - 1);
    char *hdr_b64 = malloc(hlen + 1);
    char *pay_b64 = malloc(plen + 1);
    if (!hdr_b64 || !pay_b64) { free(hdr_b64); free(pay_b64); return false; }
    memcpy(hdr_b64, token,       hlen); hdr_b64[hlen] = '\0';
    memcpy(pay_b64, dot1 + 1,   plen); pay_b64[plen] = '\0';
    const char *sig_b64 = dot2 + 1;

    /* Decode and parse the payload */
    unsigned char payload_raw[4096];
    size_t payload_len = b64url_decode(pay_b64, payload_raw, sizeof(payload_raw) - 1);
    if (payload_len == 0) { free(hdr_b64); free(pay_b64); return false; }
    payload_raw[payload_len] = '\0';

    /* Detect algorithm from header */
    unsigned char hdr_raw[256];
    size_t hdr_len = b64url_decode(hdr_b64, hdr_raw, sizeof(hdr_raw) - 1);
    bool alg_ok = false;
    if (hdr_len > 0) {
        hdr_raw[hdr_len] = '\0';
        char alg_str[16] = "";
        json_get((char *)hdr_raw, "alg", alg_str, sizeof(alg_str));
        if (s_jwt_alg == JWT_HS256 && strcmp(alg_str, "HS256") == 0) alg_ok = true;
        if (s_jwt_alg == JWT_RS256 && strcmp(alg_str, "RS256") == 0) alg_ok = true;
    }

    bool ok = false;
    if (alg_ok) {
        bool sig_ok = (s_jwt_alg == JWT_HS256)
            ? jwt_verify_hs256(hdr_b64, pay_b64, sig_b64)
            : jwt_verify_rs256(hdr_b64, pay_b64, sig_b64);
        ok = sig_ok && jwt_check_claims((char *)payload_raw);
    }

    free(hdr_b64); free(pay_b64);
    return ok;
}

/* =========================================================================
 * Public — mcp_auth_validate
 * ========================================================================= */

bool mcp_auth_validate(const char *bearer_token) {
    if (s_mode == MCP_AUTH_NONE)                   return true;
    if (!bearer_token || bearer_token[0] == '\0') return false;
    switch (s_mode) {
        case MCP_AUTH_NONE:            return true;   /* unreachable — handled above */
        case MCP_AUTH_SELF_CONTAINED:  return sc_validate(bearer_token);
        case MCP_AUTH_INTROSPECT:      return introspect_validate(bearer_token);
        case MCP_AUTH_JWT:             return jwt_validate(bearer_token);
        default:                       return false;
    }
}

/* =========================================================================
 * Public — mcp_auth_www_authenticate
 * ========================================================================= */

const char *mcp_auth_www_authenticate(const char *error, const char *description) {
    static _Thread_local char buf[512];
    if (!error) {
        snprintf(buf, sizeof(buf), "Bearer realm=\"mcp\"");
    } else if (!description) {
        snprintf(buf, sizeof(buf), "Bearer realm=\"mcp\", error=\"%s\"", error);
    } else {
        snprintf(buf, sizeof(buf),
            "Bearer realm=\"mcp\", error=\"%s\", error_description=\"%s\"",
            error, description);
    }
    return buf;
}

/* =========================================================================
 * Scheme primitives
 * ========================================================================= */

static curry_val fn_auth_mode(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_symbol(av[0])) curry_error("mcp-auth-mode!: expected symbol");
    const char *m = curry_symbol(av[0]);
    if      (strcmp(m, "none")           == 0) s_mode = MCP_AUTH_NONE;
    else if (strcmp(m, "self-contained") == 0) s_mode = MCP_AUTH_SELF_CONTAINED;
    else if (strcmp(m, "introspect")     == 0) s_mode = MCP_AUTH_INTROSPECT;
    else if (strcmp(m, "jwt")            == 0) s_mode = MCP_AUTH_JWT;
    else curry_error("mcp-auth-mode!: unknown mode '%s' (none|self-contained|introspect|jwt)", m);
    return curry_void();
}

/* --- Mode A --- */

static curry_val fn_register_client(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-register-client!: id must be a string");
    if (!curry_is_string(av[1])) curry_error("mcp-register-client!: secret must be a string");
    pthread_mutex_lock(&s_client_mu);
    if (s_nclient >= MAX_CLIENTS) {
        pthread_mutex_unlock(&s_client_mu);
        curry_error("mcp-register-client!: client registry full (max %d)", MAX_CLIENTS);
    }
    strncpy(s_clients[s_nclient].id,     curry_string(av[0]), 127);
    strncpy(s_clients[s_nclient].secret, curry_string(av[1]), 255);
    s_nclient++;
    pthread_mutex_unlock(&s_client_mu);
    return curry_void();
}

static curry_val fn_token_ttl(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_fixnum(av[0])) curry_error("mcp-token-ttl!: expected integer seconds");
    s_token_ttl = (int)curry_fixnum(av[0]);
    return curry_void();
}

/* --- Mode B1 --- */

static curry_val fn_introspection_endpoint(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0]))
        curry_error("mcp-introspection-endpoint!: expected URL string");
    strncpy(s_introspect_url, curry_string(av[0]), sizeof(s_introspect_url) - 1);
    return curry_void();
}

static curry_val fn_introspection_credentials(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0]) || !curry_is_string(av[1]))
        curry_error("mcp-introspection-credentials!: expected two strings");
    strncpy(s_introspect_cred_id,  curry_string(av[0]), sizeof(s_introspect_cred_id) - 1);
    strncpy(s_introspect_cred_sec, curry_string(av[1]), sizeof(s_introspect_cred_sec) - 1);
    return curry_void();
}

static curry_val fn_introspection_cache_ttl(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_fixnum(av[0]))
        curry_error("mcp-introspection-cache-ttl!: expected integer seconds");
    s_introspect_cache_ttl = (int)curry_fixnum(av[0]);
    return curry_void();
}

/* --- Mode B2 --- */

static curry_val fn_jwt_algorithm(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_symbol(av[0])) curry_error("mcp-jwt-algorithm!: expected symbol");
    const char *a = curry_symbol(av[0]);
    if      (strcmp(a, "hs256") == 0) s_jwt_alg = JWT_HS256;
    else if (strcmp(a, "rs256") == 0) s_jwt_alg = JWT_RS256;
    else curry_error("mcp-jwt-algorithm!: unknown algorithm '%s' (hs256|rs256)", a);
    return curry_void();
}

static curry_val fn_jwt_secret(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-jwt-secret!: expected string");
    strncpy(s_jwt_secret, curry_string(av[0]), sizeof(s_jwt_secret) - 1);
    return curry_void();
}

static curry_val fn_jwt_public_key(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-jwt-public-key!: expected file path string");
    FILE *f = fopen(curry_string(av[0]), "r");
    if (!f) curry_error("mcp-jwt-public-key!: cannot open '%s'", curry_string(av[0]));
    if (s_jwt_pubkey) { EVP_PKEY_free(s_jwt_pubkey); s_jwt_pubkey = NULL; }
    s_jwt_pubkey = PEM_read_PUBKEY(f, NULL, NULL, NULL);
    fclose(f);
    if (!s_jwt_pubkey)
        curry_error("mcp-jwt-public-key!: failed to parse public key from '%s'",
                    curry_string(av[0]));
    return curry_void();
}

static curry_val fn_jwt_public_key_pem(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-jwt-public-key-pem!: expected PEM string");
    const char *pem = curry_string(av[0]);
    BIO *bio = BIO_new_mem_buf(pem, (int)strlen(pem));
    if (s_jwt_pubkey) { EVP_PKEY_free(s_jwt_pubkey); s_jwt_pubkey = NULL; }
    s_jwt_pubkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!s_jwt_pubkey)
        curry_error("mcp-jwt-public-key-pem!: failed to parse PEM public key");
    return curry_void();
}

static curry_val fn_jwt_issuer(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-jwt-issuer!: expected string");
    strncpy(s_jwt_issuer, curry_string(av[0]), sizeof(s_jwt_issuer) - 1);
    return curry_void();
}

static curry_val fn_jwt_audience(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0])) curry_error("mcp-jwt-audience!: expected string");
    strncpy(s_jwt_audience, curry_string(av[0]), sizeof(s_jwt_audience) - 1);
    return curry_void();
}

/* =========================================================================
 * Public — mcp_auth_register
 * ========================================================================= */

void mcp_auth_register(CurryVM *vm) {
#define DEF(n, f, a, b) curry_define_fn(vm, n, f, a, b, NULL)
    DEF("mcp-auth-mode!",                  fn_auth_mode,                  1, 1);
    /* Mode A */
    DEF("mcp-register-client!",            fn_register_client,            2, 2);
    DEF("mcp-token-ttl!",                  fn_token_ttl,                  1, 1);
    /* Mode B1 */
    DEF("mcp-introspection-endpoint!",     fn_introspection_endpoint,     1, 1);
    DEF("mcp-introspection-credentials!",  fn_introspection_credentials,  2, 2);
    DEF("mcp-introspection-cache-ttl!",    fn_introspection_cache_ttl,    1, 1);
    /* Mode B2 */
    DEF("mcp-jwt-algorithm!",              fn_jwt_algorithm,              1, 1);
    DEF("mcp-jwt-secret!",                 fn_jwt_secret,                 1, 1);
    DEF("mcp-jwt-public-key!",             fn_jwt_public_key,             1, 1);
    DEF("mcp-jwt-public-key-pem!",         fn_jwt_public_key_pem,         1, 1);
    DEF("mcp-jwt-issuer!",                 fn_jwt_issuer,                 1, 1);
    DEF("mcp-jwt-audience!",               fn_jwt_audience,               1, 1);
#undef DEF
}
