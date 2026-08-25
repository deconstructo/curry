/*
 * curry_network — TCP/UDP networking module for Curry Scheme.
 *
 * Scheme API:
 *   (tcp-connect host port)         -> port-pair (in-port . out-port)
 *   (tcp-listen port backlog)       -> server-socket
 *   (tcp-accept server)             -> port-pair (in-port . out-port)
 *   (tcp-close server)              -> void      (closes tcp-listen's raw socket;
 *                                                  port pairs use close-port)
 *   (udp-socket)                    -> socket
 *   (udp-bind sock port)            -> void
 *   (udp-send sock data host port)  -> void
 *   (udp-recv sock maxbytes)        -> (data host port)
 *
 * tcp-connect/tcp-accept return an actual (in-port . out-port) pair now
 * (see curry_make_port_from_fd in the public API) — close each end with
 * close-port. tcp-listen's own listening socket is not a stream and stays
 * a raw handle, closed with tcp-close.
 *
 * socket-set-nonblocking!/socket-ready? (below) cover single-threaded
 * multiplexing; see docs/reference/module-network.md.
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <sys/select.h>
#endif

/* sock_t/SOCK_INVALID/sock_close and the raw-socket-handle pack/unpack
 * helpers (net_sock_to_val/net_val_to_sock/net_is_raw_socket_handle/
 * net_extract_fd) now live in network_internal.h, shared with srfi106.c
 * (the SRFI-106 socket interface, added alongside it) -- see that
 * header's own comment. Local aliases kept here (rather than rewriting
 * every call site below) purely to minimize this diff; srfi106.c uses
 * the net_-prefixed names directly. */
#include "network_internal.h"
#define sock_to_val net_sock_to_val
#define val_to_sock net_val_to_sock

static curry_val fn_tcp_connect(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    const char *host = curry_string(av[0]);
    int port = (int)curry_fixnum(av[1]);
    char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        curry_error("tcp-connect: could not resolve %s", host);

    sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == SOCK_INVALID) { freeaddrinfo(res); curry_error("tcp-connect: socket failed"); }
    if (connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen) != 0) {
        sock_close(fd); freeaddrinfo(res);
        curry_error("tcp-connect: connect failed");
    }
    freeaddrinfo(res);

    /* Wrap as a port pair: (in-port . out-port). Two independent FILE*
     * streams over dup()'d fds (not one bidirectional stream) since a
     * Curry port is one-directional — matches the doc comment's
     * `-> port-pair (in-port . out-port)` contract. */
    int wfd = dup((int)fd);
    if (wfd < 0) { sock_close(fd); curry_error("tcp-connect: dup failed"); }
    /* curry_make_port_from_fd takes ownership of its fd ONLY on success;
     * on fdopen failure it returns a falsy value without taking
     * ownership at all (curry.h's own documented contract), so each of
     * these must close its own fd explicitly on failure rather than
     * leaking it -- found missing here by independent code review of
     * the identical pattern in srfi106.c's socket-input-port/-output-
     * port. Once in_port has been successfully created it owns `fd`
     * (its own finalizer/close-port will close it), so a later out_port
     * failure must only close `wfd`, never `fd` again. */
    curry_val in_port = curry_make_port_from_fd((int)fd, false, false);
    if (!curry_is_true(in_port)) {
        sock_close(fd);
        sock_close(wfd);
        curry_error("tcp-connect: fdopen failed (in-port)");
    }
    curry_val out_port = curry_make_port_from_fd(wfd, true, false);
    if (!curry_is_true(out_port)) {
        sock_close(wfd);
        curry_error("tcp-connect: fdopen failed (out-port)");
    }
    return curry_make_pair(in_port, out_port);
}

static curry_val fn_tcp_listen(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int port = (int)curry_fixnum(av[0]);
    int backlog = ac > 1 ? (int)curry_fixnum(av[1]) : 10;

    sock_t fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) curry_error("tcp-listen: socket failed");

    int optval = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    /* Also try IPv4+IPv6 dual stack */
    int off = 0;
    setsockopt((int)fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_port   = htons((uint16_t)port);
    addr.sin6_addr   = in6addr_any;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        sock_close(fd); curry_error("tcp-listen: bind failed on port %d", port);
    }
    if (listen(fd, backlog) != 0) {
        sock_close(fd); curry_error("tcp-listen: listen failed");
    }
    return sock_to_val(fd);
}

static curry_val fn_tcp_accept(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    sock_t server = val_to_sock(av[0]);
    struct sockaddr_storage addr; socklen_t addrlen = sizeof(addr);
    sock_t client = accept((int)server, (struct sockaddr *)&addr, &addrlen);
    if (client == SOCK_INVALID) curry_error("tcp-accept: accept failed");

    /* Port pair, same rationale (and same fd-leak-on-fdopen-failure fix)
     * as fn_tcp_connect above. */
    int wfd = dup((int)client);
    if (wfd < 0) { sock_close(client); curry_error("tcp-accept: dup failed"); }
    curry_val in_port = curry_make_port_from_fd((int)client, false, false);
    if (!curry_is_true(in_port)) {
        sock_close(client);
        sock_close(wfd);
        curry_error("tcp-accept: fdopen failed (in-port)");
    }
    curry_val out_port = curry_make_port_from_fd(wfd, true, false);
    if (!curry_is_true(out_port)) {
        sock_close(wfd);
        curry_error("tcp-accept: fdopen failed (out-port)");
    }
    return curry_make_pair(in_port, out_port);
}

static curry_val fn_tcp_close(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    sock_close(val_to_sock(av[0]));
    return curry_void();
}

static curry_val fn_udp_socket(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; (void)av;
    sock_t fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd == SOCK_INVALID) curry_error("udp-socket: failed");
    /* Dual-stack: udp-send resolves IPv4 destinations as v4-mapped IPv6
     * addresses, which a V6ONLY socket rejects on BSD/macOS (the default
     * there, unlike Linux) even before any udp-bind call. */
    int off = 0;
    setsockopt((int)fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    return sock_to_val(fd);
}

static curry_val fn_udp_bind(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    sock_t fd = val_to_sock(av[0]);
    int port = (int)curry_fixnum(av[1]);
    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_port   = htons((uint16_t)port);
    addr.sin6_addr   = in6addr_any;
    if (bind((int)fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        curry_error("udp-bind: bind failed");
    return curry_void();
}

static curry_val fn_udp_send(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    sock_t fd = val_to_sock(av[0]);
    uint32_t dlen = curry_bytevector_length(av[1]);
    uint8_t *data = malloc(dlen > 0 ? dlen : 1);
    if (!data) curry_error("udp-send: out of memory");
    for (uint32_t i = 0; i < dlen; i++) data[i] = curry_bytevector_ref(av[1], i);
    const char *host = curry_string(av[2]);
    int port = (int)curry_fixnum(av[3]);
    char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_V4MAPPED | AI_ALL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        free(data);
        curry_error("udp-send: could not resolve %s", host);
    }
    ssize_t sent = sendto((int)fd, data, dlen, 0, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res); free(data);
    if (sent < 0) curry_error("udp-send: sendto failed");
    return curry_void();
}

static curry_val fn_udp_recv(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    sock_t fd = val_to_sock(av[0]);
    int maxbytes = (int)curry_fixnum(av[1]);
    if (maxbytes <= 0) curry_error("udp-recv: maxbytes must be positive");
    uint8_t *buf = malloc((size_t)maxbytes);
    if (!buf) curry_error("udp-recv: out of memory");
    struct sockaddr_storage addr; socklen_t addrlen = sizeof(addr);
    ssize_t n = recvfrom((int)fd, buf, (size_t)maxbytes, 0,
                          (struct sockaddr *)&addr, &addrlen);
    if (n < 0) { free(buf); curry_error("udp-recv: recvfrom failed"); }
    curry_val bv = curry_make_bytevector((uint32_t)n, 0);
    for (ssize_t i = 0; i < n; i++) curry_bytevector_set(bv, (uint32_t)i, buf[i]);
    free(buf);

    char hostbuf[NI_MAXHOST], portbuf[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *)&addr, addrlen, hostbuf, sizeof(hostbuf),
                     portbuf, sizeof(portbuf), NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        curry_error("udp-recv: getnameinfo failed");
    }

    /* Our sockets are dual-stack (IPV6_V6ONLY off), so an IPv4 sender shows up
     * as an IPv4-mapped IPv6 address ("::ffff:1.2.3.4"). Report the plain
     * dotted-quad form instead, since that's what the caller actually dialed. */
    const char *host = hostbuf;
    if (strncmp(hostbuf, "::ffff:", 7) == 0) host = hostbuf + 7;

    return curry_list(3, bv, curry_make_string(host), curry_make_fixnum(atoi(portbuf)));
}

/* socket-ready?/socket-set-nonblocking! accept either a raw socket handle
 * (tcp-listen's/udp-socket's/SRFI-106 socket's return) or a port
 * (tcp-connect's/tcp-accept's in-port or out-port) -- net_extract_fd
 * (network_internal.h) dispatches on which it got. */
#define extract_fd net_extract_fd

static curry_val fn_socket_set_nonblocking(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    int fd = extract_fd(av[0], "socket-set-nonblocking!");
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        curry_error("socket-set-nonblocking!: fcntl failed");
    return curry_void();
}

static curry_val fn_socket_ready_p(int ac, curry_val *av, void *ud) {
    (void)ud;
    int fd = extract_fd(av[0], "socket-ready?");
    struct timeval tv = {0, 0};
    struct timeval *tvp = &tv;
    if (ac > 1) {
        double ms = curry_float(av[1]);
        tv.tv_sec  = (long)(ms / 1000.0);
        tv.tv_usec = (long)(((long long)ms % 1000) * 1000);
    }
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    int r = select(fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0) curry_error("socket-ready?: select failed");
    return curry_make_bool(r > 0 && FD_ISSET(fd, &rfds));
}

/* Defined in tls.c, compiled into this same module target -- registers
 * tcp-connect-tls. Kept as a separate init function (not curry_module_init
 * itself, since only one symbol by that name can exist per .so) so the
 * TLS code's OpenSSL includes/link stay isolated in their own file. */
void curry_tls_module_init(CurryVM *vm);

/* Defined in srfi106.c, compiled into this same module target -- SRFI 106
 * (Basic Socket Interface) native primitives, built on the exact same
 * raw-socket-handle representation this file uses (network_internal.h).
 * Same "separate file, own init function" pattern as curry_tls_module_init
 * above, for the same reason: keeps the larger, more self-contained
 * SRFI-106 surface out of this file. */
void curry_srfi106_module_init(CurryVM *vm);

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "tcp-connect", fn_tcp_connect, 2, 2, NULL);
    curry_define_fn(vm, "tcp-listen",  fn_tcp_listen,  1, 2, NULL);
    curry_define_fn(vm, "tcp-accept",  fn_tcp_accept,  1, 1, NULL);
    curry_define_fn(vm, "tcp-close",   fn_tcp_close,   1, 1, NULL);
    curry_define_fn(vm, "udp-socket",  fn_udp_socket,  0, 0, NULL);
    curry_define_fn(vm, "udp-bind",    fn_udp_bind,    2, 2, NULL);
    curry_define_fn(vm, "udp-send",    fn_udp_send,    4, 4, NULL);
    curry_define_fn(vm, "udp-recv",    fn_udp_recv,    2, 2, NULL);
    curry_define_fn(vm, "socket-set-nonblocking!", fn_socket_set_nonblocking, 1, 1, NULL);
    curry_define_fn(vm, "socket-ready?", fn_socket_ready_p, 1, 2, NULL);
    curry_tls_module_init(vm);
    curry_srfi106_module_init(vm);
}
