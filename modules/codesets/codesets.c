/* codesets.c — SRFI-238 codesets: unified numeric-code <-> symbol <->
 * message lookup for errno, POSIX signals, and HTTP status codes.
 *
 * errno/signal symbol names and numeric values come straight from
 * <errno.h>/<signal.h> macros, so the numbers are always whatever this
 * platform actually uses (they differ between macOS and Linux for several
 * codes) — only the *set* of codes and their C macro names is fixed at
 * compile time, exactly as the SRFI expects ("as many as are known").
 * Platform-specific codes not defined everywhere are guarded with #ifdef
 * so the table only ever lists what the build's libc actually provides.
 *
 * http-status is a plain static table (RFC 7231/6585/etc reason phrases,
 * no platform dependency at all) since HTTP status codes aren't a libc
 * concept.
 */

#include <curry.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

typedef struct {
    const char *name;    /* symbol name, e.g. "ENOENT" or "not-found" */
    int         number;
    const char *message; /* NULL = compute dynamically (strerror/strsignal) */
} CodesetEntry;

#define ERRNO_ENTRY(e) { #e, e, NULL }

/* Rather than one #ifdef per possibly-missing macro (most POSIX.1-2008
 * base codes are universal on macOS/Linux; a handful of STREAMS-related
 * ones are glibc/Linux-only), list the universal ones directly and guard
 * only the ones known to vary. */
static const CodesetEntry errno_entries[] = {
    ERRNO_ENTRY(E2BIG), ERRNO_ENTRY(EACCES), ERRNO_ENTRY(EADDRINUSE),
    ERRNO_ENTRY(EADDRNOTAVAIL), ERRNO_ENTRY(EAFNOSUPPORT), ERRNO_ENTRY(EAGAIN),
    ERRNO_ENTRY(EALREADY), ERRNO_ENTRY(EBADF), ERRNO_ENTRY(EBADMSG),
    ERRNO_ENTRY(EBUSY), ERRNO_ENTRY(ECANCELED), ERRNO_ENTRY(ECHILD),
    ERRNO_ENTRY(ECONNABORTED), ERRNO_ENTRY(ECONNREFUSED), ERRNO_ENTRY(ECONNRESET),
    ERRNO_ENTRY(EDEADLK), ERRNO_ENTRY(EDESTADDRREQ), ERRNO_ENTRY(EDOM),
    ERRNO_ENTRY(EDQUOT), ERRNO_ENTRY(EEXIST), ERRNO_ENTRY(EFAULT),
    ERRNO_ENTRY(EFBIG), ERRNO_ENTRY(EHOSTUNREACH), ERRNO_ENTRY(EIDRM),
    ERRNO_ENTRY(EILSEQ), ERRNO_ENTRY(EINPROGRESS), ERRNO_ENTRY(EINTR),
    ERRNO_ENTRY(EINVAL), ERRNO_ENTRY(EIO), ERRNO_ENTRY(EISCONN),
    ERRNO_ENTRY(EISDIR), ERRNO_ENTRY(ELOOP), ERRNO_ENTRY(EMFILE),
    ERRNO_ENTRY(EMLINK), ERRNO_ENTRY(EMSGSIZE), ERRNO_ENTRY(EMULTIHOP),
    ERRNO_ENTRY(ENAMETOOLONG), ERRNO_ENTRY(ENETDOWN), ERRNO_ENTRY(ENETRESET),
    ERRNO_ENTRY(ENETUNREACH), ERRNO_ENTRY(ENFILE), ERRNO_ENTRY(ENOBUFS),
    ERRNO_ENTRY(ENODEV), ERRNO_ENTRY(ENOENT), ERRNO_ENTRY(ENOEXEC),
    ERRNO_ENTRY(ENOLCK), ERRNO_ENTRY(ENOLINK), ERRNO_ENTRY(ENOMEM),
    ERRNO_ENTRY(ENOMSG), ERRNO_ENTRY(ENOPROTOOPT), ERRNO_ENTRY(ENOSPC),
    ERRNO_ENTRY(ENOSYS), ERRNO_ENTRY(ENOTCONN), ERRNO_ENTRY(ENOTDIR),
    ERRNO_ENTRY(ENOTEMPTY), ERRNO_ENTRY(ENOTRECOVERABLE), ERRNO_ENTRY(ENOTSOCK),
    ERRNO_ENTRY(ENOTSUP), ERRNO_ENTRY(ENOTTY), ERRNO_ENTRY(ENXIO),
    ERRNO_ENTRY(EOVERFLOW), ERRNO_ENTRY(EOWNERDEAD), ERRNO_ENTRY(EPERM),
    ERRNO_ENTRY(EPIPE), ERRNO_ENTRY(EPROTO), ERRNO_ENTRY(EPROTONOSUPPORT),
    ERRNO_ENTRY(EPROTOTYPE), ERRNO_ENTRY(ERANGE), ERRNO_ENTRY(EROFS),
    ERRNO_ENTRY(ESPIPE), ERRNO_ENTRY(ESRCH), ERRNO_ENTRY(ESTALE),
    ERRNO_ENTRY(ETIMEDOUT), ERRNO_ENTRY(ETXTBSY), ERRNO_ENTRY(EXDEV),
#ifdef ENODATA
    ERRNO_ENTRY(ENODATA),
#endif
#ifdef ENOSR
    ERRNO_ENTRY(ENOSR),
#endif
#ifdef ENOSTR
    ERRNO_ENTRY(ENOSTR),
#endif
#ifdef ETIME
    ERRNO_ENTRY(ETIME),
#endif
#ifdef EOPNOTSUPP
    ERRNO_ENTRY(EOPNOTSUPP),
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EWOULDBLOCK != EAGAIN)
    ERRNO_ENTRY(EWOULDBLOCK),
#endif
    { NULL, 0, NULL }
};

#define SIGNAL_ENTRY(s) { #s, s, NULL }
static const CodesetEntry signal_entries[] = {
    SIGNAL_ENTRY(SIGHUP), SIGNAL_ENTRY(SIGINT), SIGNAL_ENTRY(SIGQUIT),
    SIGNAL_ENTRY(SIGILL), SIGNAL_ENTRY(SIGTRAP), SIGNAL_ENTRY(SIGABRT),
    SIGNAL_ENTRY(SIGFPE), SIGNAL_ENTRY(SIGKILL), SIGNAL_ENTRY(SIGBUS),
    SIGNAL_ENTRY(SIGSEGV), SIGNAL_ENTRY(SIGSYS), SIGNAL_ENTRY(SIGPIPE),
    SIGNAL_ENTRY(SIGALRM), SIGNAL_ENTRY(SIGTERM), SIGNAL_ENTRY(SIGURG),
    SIGNAL_ENTRY(SIGSTOP), SIGNAL_ENTRY(SIGTSTP), SIGNAL_ENTRY(SIGCONT),
    SIGNAL_ENTRY(SIGCHLD), SIGNAL_ENTRY(SIGTTIN), SIGNAL_ENTRY(SIGTTOU),
    SIGNAL_ENTRY(SIGXCPU), SIGNAL_ENTRY(SIGXFSZ), SIGNAL_ENTRY(SIGVTALRM),
    SIGNAL_ENTRY(SIGPROF), SIGNAL_ENTRY(SIGWINCH), SIGNAL_ENTRY(SIGUSR1),
    SIGNAL_ENTRY(SIGUSR2),
#ifdef SIGPOLL
    SIGNAL_ENTRY(SIGPOLL),
#endif
#ifdef SIGIO
    SIGNAL_ENTRY(SIGIO),
#endif
    { NULL, 0, NULL }
};

#define HTTP_ENTRY(code, sym, msg) { sym, code, msg }
static const CodesetEntry http_status_entries[] = {
    HTTP_ENTRY(100, "continue", "Continue"),
    HTTP_ENTRY(101, "switching-protocols", "Switching Protocols"),
    HTTP_ENTRY(200, "ok", "OK"),
    HTTP_ENTRY(201, "created", "Created"),
    HTTP_ENTRY(202, "accepted", "Accepted"),
    HTTP_ENTRY(204, "no-content", "No Content"),
    HTTP_ENTRY(206, "partial-content", "Partial Content"),
    HTTP_ENTRY(301, "moved-permanently", "Moved Permanently"),
    HTTP_ENTRY(302, "found", "Found"),
    HTTP_ENTRY(303, "see-other", "See Other"),
    HTTP_ENTRY(304, "not-modified", "Not Modified"),
    HTTP_ENTRY(307, "temporary-redirect", "Temporary Redirect"),
    HTTP_ENTRY(308, "permanent-redirect", "Permanent Redirect"),
    HTTP_ENTRY(400, "bad-request", "Bad Request"),
    HTTP_ENTRY(401, "unauthorized", "Unauthorized"),
    HTTP_ENTRY(403, "forbidden", "Forbidden"),
    HTTP_ENTRY(404, "not-found", "Not Found"),
    HTTP_ENTRY(405, "method-not-allowed", "Method Not Allowed"),
    HTTP_ENTRY(406, "not-acceptable", "Not Acceptable"),
    HTTP_ENTRY(408, "request-timeout", "Request Timeout"),
    HTTP_ENTRY(409, "conflict", "Conflict"),
    HTTP_ENTRY(410, "gone", "Gone"),
    HTTP_ENTRY(411, "length-required", "Length Required"),
    HTTP_ENTRY(412, "precondition-failed", "Precondition Failed"),
    HTTP_ENTRY(413, "payload-too-large", "Payload Too Large"),
    HTTP_ENTRY(414, "uri-too-long", "URI Too Long"),
    HTTP_ENTRY(415, "unsupported-media-type", "Unsupported Media Type"),
    HTTP_ENTRY(416, "range-not-satisfiable", "Range Not Satisfiable"),
    HTTP_ENTRY(417, "expectation-failed", "Expectation Failed"),
    HTTP_ENTRY(422, "unprocessable-entity", "Unprocessable Entity"),
    HTTP_ENTRY(425, "too-early", "Too Early"),
    HTTP_ENTRY(426, "upgrade-required", "Upgrade Required"),
    HTTP_ENTRY(428, "precondition-required", "Precondition Required"),
    HTTP_ENTRY(429, "too-many-requests", "Too Many Requests"),
    HTTP_ENTRY(431, "request-header-fields-too-large", "Request Header Fields Too Large"),
    HTTP_ENTRY(451, "unavailable-for-legal-reasons", "Unavailable For Legal Reasons"),
    HTTP_ENTRY(500, "internal-server-error", "Internal Server Error"),
    HTTP_ENTRY(501, "not-implemented", "Not Implemented"),
    HTTP_ENTRY(502, "bad-gateway", "Bad Gateway"),
    HTTP_ENTRY(503, "service-unavailable", "Service Unavailable"),
    HTTP_ENTRY(504, "gateway-timeout", "Gateway Timeout"),
    HTTP_ENTRY(505, "http-version-not-supported", "HTTP Version Not Supported"),
    HTTP_ENTRY(507, "insufficient-storage", "Insufficient Storage"),
    HTTP_ENTRY(508, "loop-detected", "Loop Detected"),
    HTTP_ENTRY(510, "not-extended", "Not Extended"),
    HTTP_ENTRY(511, "network-authentication-required", "Network Authentication Required"),
    { NULL, 0, NULL }
};

typedef enum { CS_ERRNO, CS_SIGNAL, CS_HTTP_STATUS, CS_UNKNOWN } CodesetId;

static CodesetId codeset_id(curry_val v) {
    if (!curry_is_symbol(v)) return CS_UNKNOWN;
    const char *n = curry_symbol(v);
    if (!strcmp(n, "errno")) return CS_ERRNO;
    if (!strcmp(n, "signal")) return CS_SIGNAL;
    if (!strcmp(n, "http-status")) return CS_HTTP_STATUS;
    return CS_UNKNOWN;
}

static const CodesetEntry *codeset_table(CodesetId id) {
    switch (id) {
        case CS_ERRNO: return errno_entries;
        case CS_SIGNAL: return signal_entries;
        case CS_HTTP_STATUS: return http_status_entries;
        default: return NULL;
    }
}

static const CodesetEntry *find_by_number(const CodesetEntry *t, int n) {
    for (; t->name; t++) if (t->number == n) return t;
    return NULL;
}

static const CodesetEntry *find_by_name(const CodesetEntry *t, const char *n) {
    for (; t->name; t++) if (!strcmp(t->name, n)) return t;
    return NULL;
}

static const CodesetEntry *resolve(const CodesetEntry *t, curry_val code) {
    if (curry_is_fixnum(code)) return find_by_number(t, (int)curry_fixnum(code));
    if (curry_is_symbol(code)) return find_by_name(t, curry_symbol(code));
    return NULL;
}

static curry_val fn_codeset_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(codeset_id(av[0]) != CS_UNKNOWN);
}

static curry_val fn_codeset_symbols(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const CodesetEntry *t = codeset_table(codeset_id(av[0]));
    if (!t) curry_error("codeset-symbols: not a known codeset");
    /* Build in reverse then... no cdr-setter is available, so prepend and
     * reverse once at the end (same idiom as (curry posix)'s directory-files). */
    curry_val acc = curry_nil();
    for (const CodesetEntry *e = t; e->name; e++)
        acc = curry_make_pair(curry_make_symbol(e->name), acc);
    curry_val rev = curry_nil();
    while (!curry_is_nil(acc)) { rev = curry_make_pair(curry_car(acc), rev); acc = curry_cdr(acc); }
    return rev;
}

static curry_val fn_codeset_symbol(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const CodesetEntry *t = codeset_table(codeset_id(av[0]));
    if (!t) curry_error("codeset-symbol: not a known codeset");
    const CodesetEntry *e = resolve(t, av[1]);
    return e ? curry_make_symbol(e->name) : curry_make_bool(false);
}

static curry_val fn_codeset_number(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    const CodesetEntry *t = codeset_table(codeset_id(av[0]));
    if (!t) curry_error("codeset-number: not a known codeset");
    const CodesetEntry *e = resolve(t, av[1]);
    return e ? curry_make_fixnum(e->number) : curry_make_bool(false);
}

static curry_val fn_codeset_message(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    CodesetId id = codeset_id(av[0]);
    const CodesetEntry *t = codeset_table(id);
    if (!t) curry_error("codeset-message: not a known codeset");
    const CodesetEntry *e = resolve(t, av[1]);
    if (!e) return curry_make_bool(false);
    if (e->message) return curry_make_string(e->message);
    if (id == CS_ERRNO) return curry_make_string(strerror(e->number));
    if (id == CS_SIGNAL) {
        const char *m = strsignal(e->number);
        return m ? curry_make_string(m) : curry_make_bool(false);
    }
    return curry_make_bool(false);
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "codeset?",        fn_codeset_p,       1, 1, NULL);
    curry_define_fn(vm, "codeset-symbols", fn_codeset_symbols, 1, 1, NULL);
    curry_define_fn(vm, "codeset-symbol",  fn_codeset_symbol,  2, 2, NULL);
    curry_define_fn(vm, "codeset-number",  fn_codeset_number,  2, 2, NULL);
    curry_define_fn(vm, "codeset-message", fn_codeset_message, 2, 2, NULL);
}
