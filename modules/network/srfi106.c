/*
 * srfi106.c — SRFI 106 (Basic Socket Interface) native primitives.
 *
 * Compiled into the same curry_network.so target as network.c/tls.c (see
 * network.c's own curry_srfi106_module_init comment) -- built on the
 * exact same raw-socket-handle representation network.c already
 * established (network_internal.h): `(socket . bytevector-packed-fd)`.
 *
 * Scheme API (registered directly under these SRFI-106-spec names, same
 * convention (curry posix)/SRFI-170 already uses -- the C module defines
 * the spec's own names directly; the (srfi s106 sockets)/(srfi 106)
 * library files just import-and-re-export, no renaming layer needed):
 *
 *   (make-client-socket node service [family [socktype [ai-flags [protocol]]]])
 *     -> socket
 *   (make-server-socket service [family [socktype [protocol]]]) -> socket
 *   (socket? obj) -> bool
 *   (socket-accept socket) -> socket
 *   (socket-send socket bv [flags]) -> bytes-sent (fixnum)
 *   (socket-recv socket size [flags]) -> bytevector
 *   (socket-shutdown socket how) -> void
 *   (socket-close socket) -> void
 *   (socket-input-port socket) -> port
 *   (socket-output-port socket) -> port
 *
 * `service` (make-client-socket/make-server-socket) accepts either a
 * string (a service name or numeric port string, passed straight to
 * getaddrinfo) or a fixnum (a port number, converted to its decimal
 * string form first) -- SRFI 106 leaves the representation
 * implementation-defined; supporting both matches what real portable
 * code in the wild actually passes (chibi/gauche both accept a fixnum
 * port too, not just a string).
 *
 * `how` (socket-shutdown) is NOT a raw platform SHUT_RD/SHUT_WR/
 * SHUT_RDWR value -- see the shutdown-method constants below for why.
 *
 * The (address-family ...)/(address-info ...)/(socket-domain ...)/
 * (ip-protocol ...)/(message-type ...)/(shutdown-method ...) macros
 * SRFI 106 itself specifies live in the Scheme shim (srfi/s106/sockets.scm),
 * not here -- they're pure compile-time name->constant lookups with no
 * runtime behavior of their own, syntax-rules is the natural fit, and
 * curry's C module API has no macro-registration story (DEF-registered
 * primitives are always ordinary procedures).
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
/* struct addrinfo/getaddrinfo/freeaddrinfo/AI_* /IPPROTO_* /etc. below are
 * declared in ws2tcpip.h, not winsock2.h (network_internal.h's own
 * _WIN32 branch only pulls in the latter, for sock_t/SOCK_INVALID/
 * sock_close) -- mirrors network.c's own identical include, which needs
 * the exact same symbols for its own getaddrinfo calls. */
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <arpa/inet.h>
#endif

#include "network_internal.h"

/* 20 digits (the largest magnitude a signed 64-bit long can print) + 1
 * sign + 1 NUL, rounded up. See service_to_cstr's own comment for why
 * this needs to cover more than just a 16-bit port number. */
#define SERVICE_BUF_SIZE 24

/* service: fixnum port number or a string (service name / numeric port
 * string) -- normalizes to the C string getaddrinfo wants. `buf` must be
 * at least SERVICE_BUF_SIZE bytes -- curry_fixnum is a signed ~61-bit
 * value (val_t's 2-bit tag leaves that much magnitude, see value.h), not
 * a 32-bit int, so its decimal form needs up to 20 digits plus a sign and
 * NUL; a 16-byte buffer (an earlier version of this function, found
 * undersized by independent code review) silently truncated the most
 * extreme fixnum values instead of overflowing -- snprintf itself never
 * writes past `buflen`, so this was never a memory-safety bug, but a
 * truncated port number is still a real correctness bug: getaddrinfo
 * would reject the truncated string as an unresolvable service, an
 * opaque failure with no hint that the real cause was a buffer that was
 * simply too small. Returns a pointer to either `buf` or the original
 * Scheme string's own GC-heap bytes (curry_string's usual "pointer into
 * the GC heap" contract -- valid as long as the underlying curry_val
 * stays reachable, which it does for the duration of this call, all on
 * the C stack). */
static const char *service_to_cstr(curry_val service, char *buf, size_t buflen) {
    if (curry_is_fixnum(service)) {
        snprintf(buf, buflen, "%ld", (long)curry_fixnum(service));
        return buf;
    }
    if (curry_is_string(service)) return curry_string(service);
    curry_error("service must be a string or an exact integer");
    return NULL; /* unreachable -- curry_error longjmps */
}

static curry_val fn_make_client_socket(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *node = curry_string(av[0]);
    char service_buf[SERVICE_BUF_SIZE];
    const char *service = service_to_cstr(av[1], service_buf, sizeof(service_buf));

    struct addrinfo hints = {0}, *res;
    hints.ai_family   = ac > 2 ? (int)curry_fixnum(av[2]) : AF_UNSPEC;
    hints.ai_socktype = ac > 3 ? (int)curry_fixnum(av[3]) : SOCK_STREAM;
    hints.ai_flags    = ac > 4 ? (int)curry_fixnum(av[4]) : 0;
    hints.ai_protocol = ac > 5 ? (int)curry_fixnum(av[5]) : 0;

    if (getaddrinfo(node, service, &hints, &res) != 0)
        curry_error("make-client-socket: could not resolve %s", node);

    sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == SOCK_INVALID) {
        freeaddrinfo(res);
        curry_error("make-client-socket: socket failed");
    }
    if (connect((int)fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        sock_close(fd);
        freeaddrinfo(res);
        curry_error("make-client-socket: connect failed");
    }
    freeaddrinfo(res);
    return net_sock_to_val(fd);
}

static curry_val fn_make_server_socket(int ac, curry_val *av, void *ud) {
    (void)ud;
    char service_buf[SERVICE_BUF_SIZE];
    const char *service = service_to_cstr(av[0], service_buf, sizeof(service_buf));

    struct addrinfo hints = {0}, *res;
    hints.ai_family   = ac > 1 ? (int)curry_fixnum(av[1]) : AF_UNSPEC;
    hints.ai_socktype = ac > 2 ? (int)curry_fixnum(av[2]) : SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_protocol = ac > 3 ? (int)curry_fixnum(av[3]) : 0;

    if (getaddrinfo(NULL, service, &hints, &res) != 0)
        curry_error("make-server-socket: could not resolve service %s", service);

    sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == SOCK_INVALID) {
        freeaddrinfo(res);
        curry_error("make-server-socket: socket failed");
    }
    int optval = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind((int)fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        sock_close(fd);
        freeaddrinfo(res);
        curry_error("make-server-socket: bind failed");
    }
    /* Only a stream socket is listen()able -- a datagram server socket is
     * ready to recv immediately after bind. */
    if (hints.ai_socktype == SOCK_STREAM && listen((int)fd, 128) != 0) {
        sock_close(fd);
        freeaddrinfo(res);
        curry_error("make-server-socket: listen failed");
    }
    freeaddrinfo(res);
    return net_sock_to_val(fd);
}

static curry_val fn_socket_p(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_bool(net_is_raw_socket_handle(av[0]));
}

static curry_val fn_socket_accept(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int server = net_extract_fd(av[0], "socket-accept");
    struct sockaddr_storage addr; socklen_t addrlen = sizeof(addr);
    sock_t client = accept(server, (struct sockaddr *)&addr, &addrlen);
    if (client == SOCK_INVALID) curry_error("socket-accept: accept failed");
    return net_sock_to_val(client);
}

/* (socket-local-port socket) -> fixnum
 *
 * The actual port a socket is bound to, read back via getsockname --
 * needed for a server socket created with a "0" service/port (the
 * standard way to ask the OS for an arbitrary free ephemeral port
 * instead of a hardcoded one): the caller doesn't learn what port that
 * actually is until after bind() has already happened, since the OS
 * picks it. Not part of SRFI 106 itself (that spec offers no way to
 * query a bound socket's local address at all), but registered under
 * the same "socket-*" naming convention as the rest of this file's
 * primitives -- see docs/reference for how (curry websocket)'s ws-listen
 * and the ros_tests.scm/websocket_tests.scm/websocket_server_tests.scm
 * suites use this to bind an ephemeral port instead of a fixed one,
 * closing out issue #110's CI port-contention flakiness at the root
 * rather than only papering over it with a ctest-level timeout. */
static curry_val fn_socket_local_port(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int fd = net_extract_fd(av[0], "socket-local-port");
    struct sockaddr_storage addr; socklen_t addrlen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) != 0)
        curry_error("socket-local-port: getsockname failed: %s", strerror(errno));
    uint16_t port;
    if (addr.ss_family == AF_INET6)
        port = ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    else if (addr.ss_family == AF_INET)
        port = ntohs(((struct sockaddr_in *)&addr)->sin_port);
    else
        curry_error("socket-local-port: not an AF_INET/AF_INET6 socket");
    return curry_make_fixnum(port);
}

static curry_val fn_socket_send(int ac, curry_val *av, void *ud) {
    (void)ud;
    int fd = net_extract_fd(av[0], "socket-send");
    /* Issue #161: the socket HANDLE (av[0], just above) is validated via
     * net_extract_fd, but this data argument never was -- curry_bytevector_
     * length/data (src/api.c) do an unchecked as_bytes() cast assuming
     * their argument already IS a bytevector, the identical hazard #158
     * closed for the handle. Confirmed reproducible SIGSEGV via
     * (socket-send some-socket 42). */
    if (!curry_is_bytevector(av[1])) curry_error("socket-send: data must be a bytevector");
    uint32_t len = curry_bytevector_length(av[1]);
    const uint8_t *data = curry_bytevector_data(av[1]);
    int flags = ac > 2 ? (int)curry_fixnum(av[2]) : 0;
    ssize_t sent = send(fd, data, len, flags);
    if (sent < 0) curry_error("socket-send: send failed: %s", strerror(errno));
    return curry_make_fixnum(sent);
}

static curry_val fn_socket_recv(int ac, curry_val *av, void *ud) {
    (void)ud;
    int fd = net_extract_fd(av[0], "socket-recv");
    intptr_t size = curry_fixnum(av[1]);
    if (size <= 0) curry_error("socket-recv: size must be a positive exact integer");
    int flags = ac > 2 ? (int)curry_fixnum(av[2]) : 0;

    uint8_t *buf = malloc((size_t)size);
    if (!buf) curry_error("socket-recv: out of memory");
    ssize_t n = recv(fd, buf, (size_t)size, flags);
    if (n < 0) { free(buf); curry_error("socket-recv: recv failed: %s", strerror(errno)); }

    curry_val bv = curry_make_bytevector((uint32_t)n, 0);
    for (ssize_t i = 0; i < n; i++) curry_bytevector_set(bv, (uint32_t)i, buf[i]);
    free(buf);
    return bv;
}

/* `how` is a synthetic bitmask (1 = read, 2 = write, 3 = both -- see the
 * *shut-rd* / *shut-wr* / *shut-rdwr* constants below), NOT a raw platform
 * SHUT_RD/SHUT_WR/SHUT_RDWR value. Real POSIX SHUT_* values (0, 1, 2 on
 * every platform checked) are NOT independent bits -- SHUT_RD | SHUT_WR
 * (0 | 1 = 1) would silently collide with plain SHUT_WR instead of
 * producing SHUT_RDWR (2), which is exactly the shape SRFI 106's own
 * `(shutdown-method read write)` needs to combine cleanly the same way
 * `(message-type peek oob)` already does for genuinely independent MSG_*
 * bits. Translating a clean, always-combinable 2-bit encoding into the
 * real platform constant here (rather than exposing raw SHUT_* values to
 * Scheme and hoping the merge macro's plain bitwise-or never gets used
 * against them) keeps the combining logic uniform across all of SRFI
 * 106's flag categories instead of carving out a silent special case for
 * this one. */
static curry_val fn_socket_shutdown(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int fd = net_extract_fd(av[0], "socket-shutdown");
    intptr_t how = curry_fixnum(av[1]);
    int real_how;
    switch (how) {
        case 1: real_how = SHUT_RD;   break;
        case 2: real_how = SHUT_WR;   break;
        case 3: real_how = SHUT_RDWR; break;
        default:
            curry_error("socket-shutdown: how must be *shut-rd*, *shut-wr*, or *shut-rdwr*");
            return curry_void(); /* unreachable */
    }
    if (shutdown(fd, real_how) != 0)
        curry_error("socket-shutdown: shutdown failed: %s", strerror(errno));
    return curry_void();
}

static curry_val fn_socket_close(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    /* Only meaningful for a raw handle -- a socket obtained via
     * socket-input-port/-output-port is an ordinary port and should be
     * closed with close-port instead, same as tcp-connect's own port
     * pairs (see network.c's header comment). Mirrors extract_fd's own
     * "accepts either shape" flexibility elsewhere in this module, but
     * closing a port's underlying fd out from under it here (rather than
     * through close-port) would leave that port's own internal
     * open/closed bookkeeping wrong -- so this one deliberately does NOT
     * accept a port, only a raw handle. */
    if (!net_is_raw_socket_handle(av[0]))
        curry_error("socket-close: not a socket (did you mean close-port, for a socket-input-port/socket-output-port result?)");
    sock_close(net_val_to_sock(av[0]));
    return curry_void();
}

/* Duplicates the fd for each port (same rationale as tcp-connect's own
 * in-port/out-port pair, network.c: two independent FILE* streams over
 * dup()'d fds, not one bidirectional stream, since a Curry port is
 * one-directional) -- so socket-input-port and socket-output-port can
 * both be called on the same socket and used independently, and the
 * original socket handle is left untouched (still valid for socket-send/
 * socket-recv/socket-close) rather than consumed by either call. */
static curry_val fn_socket_input_port(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int fd = net_extract_fd(av[0], "socket-input-port");
    int dfd = dup(fd);
    if (dfd < 0) curry_error("socket-input-port: dup failed");
    curry_val port = curry_make_port_from_fd(dfd, false, false);
    /* curry_make_port_from_fd takes ownership of dfd ONLY on success
     * (closing it via fclose when the port is later closed/finalized);
     * on fdopen failure it returns a falsy value without having taken
     * ownership at all, per its own documented contract (curry.h) --
     * found missing here by independent code review: without this
     * explicit close, dfd (a real, just-dup()'d fd) leaked on every
     * fdopen failure, worsening the exact fd-exhaustion condition most
     * likely to CAUSE that failure in the first place. */
    if (!curry_is_true(port)) {
        sock_close((sock_t)dfd);
        curry_error("socket-input-port: fdopen failed");
    }
    return port;
}

static curry_val fn_socket_output_port(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int fd = net_extract_fd(av[0], "socket-output-port");
    int dfd = dup(fd);
    if (dfd < 0) curry_error("socket-output-port: dup failed");
    curry_val port = curry_make_port_from_fd(dfd, true, false);
    /* See fn_socket_input_port's own comment on this exact check. */
    if (!curry_is_true(port)) {
        sock_close((sock_t)dfd);
        curry_error("socket-output-port: fdopen failed");
    }
    return port;
}

void curry_srfi106_module_init(CurryVM *vm) {
    curry_define_fn(vm, "make-client-socket", fn_make_client_socket, 2, 6, NULL);
    curry_define_fn(vm, "make-server-socket", fn_make_server_socket, 1, 4, NULL);
    curry_define_fn(vm, "socket?",             fn_socket_p,          1, 1, NULL);
    curry_define_fn(vm, "socket-accept",       fn_socket_accept,     1, 1, NULL);
    curry_define_fn(vm, "socket-local-port",   fn_socket_local_port, 1, 1, NULL);
    curry_define_fn(vm, "socket-send",         fn_socket_send,       2, 3, NULL);
    curry_define_fn(vm, "socket-recv",         fn_socket_recv,       2, 3, NULL);
    curry_define_fn(vm, "socket-shutdown",     fn_socket_shutdown,   2, 2, NULL);
    curry_define_fn(vm, "socket-close",        fn_socket_close,      1, 1, NULL);
    curry_define_fn(vm, "socket-input-port",   fn_socket_input_port, 1, 1, NULL);
    curry_define_fn(vm, "socket-output-port",  fn_socket_output_port,1, 1, NULL);

    /* Named constants (SRFI 106's own *earmuffs* naming). Plain fixnums
     * wrapping the real platform values, EXCEPT *shut-rd* / *shut-wr* /
     * *shut-rdwr* -- see fn_socket_shutdown's own comment for why those
     * three are a synthetic, always-combinable encoding instead. */
    curry_define_val(vm, "*af-unspec*", curry_make_fixnum(AF_UNSPEC));
    curry_define_val(vm, "*af-inet*",   curry_make_fixnum(AF_INET));
    curry_define_val(vm, "*af-inet6*",  curry_make_fixnum(AF_INET6));

    curry_define_val(vm, "*sock-stream*", curry_make_fixnum(SOCK_STREAM));
    curry_define_val(vm, "*sock-dgram*",  curry_make_fixnum(SOCK_DGRAM));

    curry_define_val(vm, "*ai-canonname*",  curry_make_fixnum(AI_CANONNAME));
    curry_define_val(vm, "*ai-numerichost*", curry_make_fixnum(AI_NUMERICHOST));
    curry_define_val(vm, "*ai-v4mapped*",   curry_make_fixnum(AI_V4MAPPED));
    curry_define_val(vm, "*ai-all*",        curry_make_fixnum(AI_ALL));
    curry_define_val(vm, "*ai-addrconfig*", curry_make_fixnum(AI_ADDRCONFIG));

    curry_define_val(vm, "*ipproto-ip*",  curry_make_fixnum(IPPROTO_IP));
    curry_define_val(vm, "*ipproto-tcp*", curry_make_fixnum(IPPROTO_TCP));
    curry_define_val(vm, "*ipproto-udp*", curry_make_fixnum(IPPROTO_UDP));

    curry_define_val(vm, "*msg-peek*",    curry_make_fixnum(MSG_PEEK));
    curry_define_val(vm, "*msg-oob*",     curry_make_fixnum(MSG_OOB));
    curry_define_val(vm, "*msg-waitall*", curry_make_fixnum(MSG_WAITALL));

    curry_define_val(vm, "*shut-rd*",   curry_make_fixnum(1));
    curry_define_val(vm, "*shut-wr*",   curry_make_fixnum(2));
    curry_define_val(vm, "*shut-rdwr*", curry_make_fixnum(3));
}
