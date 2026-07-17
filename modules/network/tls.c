/*
 * tls.c — TLS client support for (curry network), via OpenSSL.
 *
 * Scheme API:
 *   (tcp-connect-tls host port)  -> port-pair (in-port . out-port)
 *
 * Compiles into the same curry_network.so target as network.c (see
 * CMakeLists.txt) but lives in its own file to keep OpenSSL's includes
 * and link isolated from the plain-socket code.
 *
 * Design: an SSL* session is wrapped as two Curry ports (input/output)
 * by making it masquerade as two FILE* streams via funopen (macOS/BSD)
 * or fopencookie (Linux/glibc) — SSL_read/SSL_write become the stream's
 * read/write callbacks. This reuses 100% of curry's existing FILE*-based
 * Port machinery (src/port.c) with zero changes there; the only public
 * API addition needed was curry_make_port_from_file (added alongside
 * curry_make_port_from_fd for the plain-socket port-pair fix), since a
 * funopen/fopencookie stream isn't backed by a plain fd curry_make_port_
 * from_fd could fdopen.
 *
 * Both directions share one SSL* (SSL_read/SSL_write on the same object,
 * per OpenSSL's own design — a single session isn't actually split into
 * two half-duplex objects), so the two FILE*s share one ref-counted
 * cookie; the underlying SSL/fd are only torn down once both ports have
 * been closed.
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

typedef struct {
    SSL *ssl;
    int  fd;
    int  refcount;   /* two FILE*s (in/out) share one session; tear down
                      * SSL_shutdown/SSL_free/close(fd) only once both
                      * have been closed, not on the first close-port. */
} TlsCookie;

static SSL_CTX *g_tls_ctx = NULL;

static SSL_CTX *tls_ctx(void) {
    if (g_tls_ctx) return g_tls_ctx;
    g_tls_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_tls_ctx) curry_error("tcp-connect-tls: SSL_CTX_new failed");
    if (!SSL_CTX_set_default_verify_paths(g_tls_ctx))
        curry_error("tcp-connect-tls: could not load system trust store");
    /* Verification on by default -- no silent downgrade to unverified TLS. */
    SSL_CTX_set_verify(g_tls_ctx, SSL_VERIFY_PEER, NULL);
    return g_tls_ctx;
}

/* ---- funopen (macOS/BSD) / fopencookie (Linux/glibc) callbacks ---- */

#ifdef __APPLE__

static int tls_read_cb(void *cookie, char *buf, int n) {
    TlsCookie *c = (TlsCookie *)cookie;
    int got = SSL_read(c->ssl, buf, n);
    if (got > 0) return got;
    int err = SSL_get_error(c->ssl, got);
    if (err == SSL_ERROR_ZERO_RETURN) return 0;   /* clean EOF */
    errno = EIO;
    return -1;
}

static int tls_write_cb(void *cookie, const char *buf, int n) {
    TlsCookie *c = (TlsCookie *)cookie;
    int written = SSL_write(c->ssl, buf, n);
    if (written > 0) return written;
    errno = EIO;
    return -1;
}

static int tls_close_cb(void *cookie) {
    TlsCookie *c = (TlsCookie *)cookie;
    if (--c->refcount == 0) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        close(c->fd);
        free(c);
    }
    return 0;
}

static FILE *tls_fopen(TlsCookie *c, bool for_write) {
    return funopen(c, for_write ? NULL : tls_read_cb,
                      for_write ? tls_write_cb : NULL,
                      NULL, tls_close_cb);
}

#else /* Linux / glibc */

static ssize_t tls_read_cb(void *cookie, char *buf, size_t n) {
    TlsCookie *c = (TlsCookie *)cookie;
    int got = SSL_read(c->ssl, buf, (int)n);
    if (got > 0) return (ssize_t)got;
    int err = SSL_get_error(c->ssl, got);
    if (err == SSL_ERROR_ZERO_RETURN) return 0;
    errno = EIO;
    return -1;
}

static ssize_t tls_write_cb(void *cookie, const char *buf, size_t n) {
    TlsCookie *c = (TlsCookie *)cookie;
    int written = SSL_write(c->ssl, buf, (int)n);
    if (written > 0) return (ssize_t)written;
    errno = EIO;
    return -1;
}

static int tls_close_cb(void *cookie) {
    TlsCookie *c = (TlsCookie *)cookie;
    if (--c->refcount == 0) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        close(c->fd);
        free(c);
    }
    return 0;
}

static FILE *tls_fopen(TlsCookie *c, bool for_write) {
    cookie_io_functions_t io = {0};
    if (for_write) io.write = tls_write_cb;
    else           io.read  = tls_read_cb;
    io.close = tls_close_cb;
    return fopencookie(c, for_write ? "w" : "r", io);
}

#endif

/* ---- (tcp-connect-tls host port) ---- */

static curry_val fn_tcp_connect_tls(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *host = curry_string(av[0]);
    int port = (int)curry_fixnum(av[1]);
    char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        curry_error("tcp-connect-tls: could not resolve %s", host);

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); curry_error("tcp-connect-tls: socket failed"); }
    if (connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res);
        curry_error("tcp-connect-tls: connect failed");
    }
    freeaddrinfo(res);

    SSL *ssl = SSL_new(tls_ctx());
    if (!ssl) { close(fd); curry_error("tcp-connect-tls: SSL_new failed"); }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);   /* SNI */

    /* SSL_get_verify_result alone only checks that the certificate chain
     * is trusted -- it says nothing about whether the certificate is
     * actually FOR this host. Without hostname verification, any host
     * holding a valid cert for any domain could impersonate `host` via
     * a MITM'd connection. SSL_set1_host makes the handshake itself fail
     * on a mismatch, matching what SSL_get_verify_result's callers below
     * assume it already covers. */
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (!SSL_set1_host(ssl, host)) {
        SSL_free(ssl); close(fd);
        curry_error("tcp-connect-tls: SSL_set1_host failed");
    }

    if (SSL_connect(ssl) != 1) {
        unsigned long e = ERR_get_error();
        char errbuf[256];
        ERR_error_string_n(e, errbuf, sizeof(errbuf));
        SSL_free(ssl); close(fd);
        curry_error("tcp-connect-tls: handshake with %s failed: %s", host, errbuf);
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        const char *reason = X509_verify_cert_error_string(SSL_get_verify_result(ssl));
        SSL_shutdown(ssl); SSL_free(ssl); close(fd);
        curry_error("tcp-connect-tls: certificate verification failed for %s: %s",
                    host, reason);
    }

    TlsCookie *cookie = malloc(sizeof(TlsCookie));
    cookie->ssl = ssl;
    cookie->fd  = fd;
    cookie->refcount = 2;   /* one per direction */

    FILE *rfp = tls_fopen(cookie, false);
    FILE *wfp = tls_fopen(cookie, true);
    if (!rfp || !wfp) {
        if (rfp) fclose(rfp); else if (wfp) fclose(wfp);
        else { SSL_shutdown(ssl); SSL_free(ssl); close(fd); free(cookie); }
        curry_error("tcp-connect-tls: could not create stream wrapper");
    }

    curry_val in_port  = curry_make_port_from_file(rfp, false, false);
    curry_val out_port = curry_make_port_from_file(wfp, true, false);
    return curry_make_pair(in_port, out_port);
}

void curry_tls_module_init(CurryVM *vm) {
    curry_define_fn(vm, "tcp-connect-tls", fn_tcp_connect_tls, 2, 2, NULL);
}
