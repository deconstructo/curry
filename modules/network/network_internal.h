/*
 * network_internal.h — shared "raw socket handle" representation for the
 * curry_network module target (network.c, srfi106.c; NOT tls.c, which
 * never touches raw handles directly).
 *
 * A raw socket handle (as opposed to a port -- see tcp-connect's own
 * comment in network.c) is `(socket . bytevector-packed-fd)`: the fd is
 * packed byte-for-byte into a bytevector rather than exposed as a
 * fixnum, so it can never be mistaken for -- or forged as -- an ordinary
 * integer at the Scheme level. Moved here (out of network.c, where these
 * were originally static/file-private) so srfi106.c can share the exact
 * same pack/unpack logic instead of duplicating it -- header-only
 * `static inline` rather than a shared .c translation unit, since these
 * are all one-liners and this avoids any new link-time surface for a
 * module that's otherwise a flat set of independent .c files compiled
 * into one target.
 */

#ifndef CURRY_NETWORK_INTERNAL_H
#define CURRY_NETWORK_INTERNAL_H

#include <curry.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
typedef SOCKET sock_t;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close closesocket
#else
#  include <unistd.h>
typedef int sock_t;
#  define SOCK_INVALID (-1)
#  define sock_close close
#endif

static inline curry_val net_sock_to_val(sock_t fd) {
    curry_val bv = curry_make_bytevector(sizeof(sock_t), 0);
    for (size_t i = 0; i < sizeof(sock_t); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&fd)[i]);
    return curry_make_pair(curry_make_symbol("socket"), bv);
}

static inline sock_t net_val_to_sock(curry_val v) {
    curry_val bv = curry_cdr(v);
    sock_t fd;
    for (size_t i = 0; i < sizeof(sock_t); i++)
        ((uint8_t *)&fd)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return fd;
}

static inline bool net_is_raw_socket_handle(curry_val v) {
    return curry_is_pair(v) && curry_is_symbol(curry_car(v)) &&
           strcmp(curry_symbol(curry_car(v)), "socket") == 0;
}

/* Accepts either a raw socket handle (tcp-listen's/udp-socket's/SRFI-106
 * make-client-socket's/make-server-socket's/socket-accept's return) or a
 * port (tcp-connect's/tcp-accept's in-port or out-port) -- extract_fd
 * dispatches on which it got. */
static inline int net_extract_fd(curry_val v, const char *who) {
    if (net_is_raw_socket_handle(v)) return (int)net_val_to_sock(v);
    int fd = curry_port_fd(v);
    if (fd < 0) curry_error("%s: not a socket handle or file-backed port", who);
    return fd;
}

#endif /* CURRY_NETWORK_INTERNAL_H */
