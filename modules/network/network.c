/*
 * curry_network — TCP/UDP networking module for Curry Scheme.
 *
 * Scheme API:
 *   (tcp-connect host port)         -> port-pair (in-port . out-port)
 *   (tcp-listen port backlog)       -> server-socket
 *   (tcp-accept server)             -> port-pair
 *   (tcp-close server)              -> void
 *   (udp-socket)                    -> socket
 *   (udp-bind sock port)            -> void
 *   (udp-send sock data host port)  -> void
 *   (udp-recv sock maxbytes)        -> bytevector
 *
 * Non-blocking I/O and async support are planned via integration
 * with the actor system (each connection runs in its own actor).
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
typedef SOCKET sock_t;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close closesocket
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <arpa/inet.h>
typedef int sock_t;
#  define SOCK_INVALID (-1)
#  define sock_close close
#endif

static curry_val sock_to_val(sock_t fd) {
    /* Pack fd into a bytevector */
    curry_val bv = curry_make_bytevector(sizeof(sock_t), 0);
    for (size_t i = 0; i < sizeof(sock_t); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&fd)[i]);
    return curry_make_pair(curry_make_symbol("socket"), bv);
}

static sock_t val_to_sock(curry_val v) {
    curry_val bv = curry_cdr(v);
    sock_t fd;
    for (size_t i = 0; i < sizeof(sock_t); i++)
        ((uint8_t *)&fd)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return fd;
}

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

    /* Wrap as FILE* for use as Scheme ports */
    FILE *rfp = fdopen((int)fd, "r");
    FILE *wfp = fdopen((int)dup((int)fd), "w");
    (void)rfp; (void)wfp;
    /* Return sock handle; caller uses port wrappers */
    return sock_to_val(fd);
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
    return sock_to_val(client);
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
    uint8_t *data = malloc(dlen);
    for (uint32_t i = 0; i < dlen; i++) data[i] = curry_bytevector_ref(av[1], i);
    const char *host = curry_string(av[2]);
    int port = (int)curry_fixnum(av[3]);
    char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_DGRAM;
    getaddrinfo(host, port_str, &hints, &res);
    sendto((int)fd, data, dlen, 0, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res); free(data);
    return curry_void();
}

static curry_val fn_udp_recv(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    sock_t fd = val_to_sock(av[0]);
    int maxbytes = (int)curry_fixnum(av[1]);
    uint8_t *buf = malloc((size_t)maxbytes);
    ssize_t n = recv((int)fd, buf, (size_t)maxbytes, 0);
    if (n < 0) { free(buf); curry_error("udp-recv: recv failed"); }
    curry_val bv = curry_make_bytevector((uint32_t)n, 0);
    for (ssize_t i = 0; i < n; i++) curry_bytevector_set(bv, (uint32_t)i, buf[i]);
    free(buf);
    return bv;
}

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "tcp-connect", fn_tcp_connect, 2, 2, NULL);
    curry_define_fn(vm, "tcp-listen",  fn_tcp_listen,  1, 2, NULL);
    curry_define_fn(vm, "tcp-accept",  fn_tcp_accept,  1, 1, NULL);
    curry_define_fn(vm, "tcp-close",   fn_tcp_close,   1, 1, NULL);
    curry_define_fn(vm, "udp-socket",  fn_udp_socket,  0, 0, NULL);
    curry_define_fn(vm, "udp-bind",    fn_udp_bind,    2, 2, NULL);
    curry_define_fn(vm, "udp-send",    fn_udp_send,    4, 4, NULL);
    curry_define_fn(vm, "udp-recv",    fn_udp_recv,    2, 2, NULL);
}
