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
#include <stdbool.h>

#ifdef _WIN32
#  include <winsock2.h>
typedef SOCKET sock_t;
#  define SOCK_INVALID INVALID_SOCKET
#  define sock_close closesocket
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <errno.h>
#  include <time.h>
#  include <math.h>
typedef int sock_t;
#  define SOCK_INVALID (-1)
#  define sock_close close
#endif

/* Issue #160: process-wide registry of fds curry's own socket-opening
 * primitives actually created (every call site that calls
 * net_sock_to_val_registered below: tcp-listen, udp-socket,
 * make-client-socket, make-server-socket, socket-accept). A raw socket
 * handle's PAIR SHAPE is trivially forgeable from Scheme --
 * net_is_raw_socket_handle (below) only checks shape, not provenance (see
 * #158) -- so `(cons 'socket packed-fd-bytes)` names whatever fd number
 * happens to be encoded, including fds this process has open for
 * something entirely unrelated to a socket the curry script was ever
 * actually handed: another open file, stdin/stdout/stderr, a DIFFERENT
 * actor's own socket, or a fd number simply recycled by the OS after an
 * earlier close. net_checked_val_to_sock and net_extract_fd's handle
 * branch (below) both cross-check membership here before returning a fd
 * to any caller, so a forged/foreign fd number is rejected the same way a
 * malformed pair shape already is, instead of silently letting a curry
 * script operate on whatever that fd currently happens to be.
 *
 * Defined once in network.c (always compiled whenever this module is
 * built -- see CMakeLists.txt's BUILD_MODULE_NETWORK block), declared
 * here with ordinary extern linkage so srfi106.c shares the exact same
 * table instead of each translation unit getting its own (which a
 * `static` definition in this header would have silently produced, since
 * network.c and srfi106.c are separate TUs compiled into the same .so
 * target). Mutex-protected growable array rather than a fixed-size table
 * sized against sysconf(_SC_OPEN_MAX) (a soft limit a program can raise
 * at runtime) or a full hash set: the number of concurrently open sockets
 * in a real program is expected to stay small, so linear scan/insert/
 * remove is plenty fast for this table's actual size. */
bool net_fd_registry_add(sock_t fd);       /* false = registration failed (OOM) */
void net_fd_registry_remove(sock_t fd);
bool net_fd_registry_contains(sock_t fd);

static inline curry_val net_sock_to_val(sock_t fd) {
    curry_val bv = curry_make_bytevector(sizeof(sock_t), 0);
    for (size_t i = 0; i < sizeof(sock_t); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&fd)[i]);
    return curry_make_pair(curry_make_symbol("socket"), bv);
}

/* Every primitive that hands a freshly-created fd back to Scheme as a raw
 * socket handle must register it first -- this is the one choke point
 * that does both, so no call site can pack a handle without also
 * registering it. On registration failure (OOM in the registry's own
 * growable array), the fd is closed rather than handed to Scheme
 * unregistered, which would have made it permanently unusable anyway
 * (every future net_checked_val_to_sock/net_extract_fd call would reject
 * it as unregistered) while also silently leaking it. */
static inline curry_val net_sock_to_val_registered(sock_t fd, const char *who) {
    if (!net_fd_registry_add(fd)) {
        sock_close(fd);
        curry_error("%s: out of memory (socket registry)", who);
    }
    return net_sock_to_val(fd);
}

static inline sock_t net_val_to_sock(curry_val v) {
    curry_val bv = curry_cdr(v);
    sock_t fd;
    for (size_t i = 0; i < sizeof(sock_t); i++)
        ((uint8_t *)&fd)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return fd;
}

/* Issue #158: a curry script can construct ANY pair shaped like a raw
 * socket handle -- e.g. (cons 'socket 42) or (cons 'socket (make-bytevector 0)) --
 * since this check previously only looked at the car. net_val_to_sock
 * unconditionally reads sizeof(sock_t) bytes from the cdr with no bounds
 * check of its own (curry_bytevector_ref does no bounds check either), so
 * a too-short bytevector was a genuine out-of-bounds heap read whose
 * garbage result became an fd fed straight into a real syscall. A cdr
 * that isn't a bytevector at all was worse: curry_bytevector_length/
 * curry_bytevector_ref assume their argument already IS one (as_bytes
 * does an unchecked cast), so misinterpreting an arbitrary heap object's
 * header as a Bytevector's is its own type-confusion bug, not just an
 * out-of-bounds length read. Now verifies both: the cdr must actually be
 * a bytevector, and it must be EXACTLY sizeof(sock_t) bytes -- not just
 * "at least", since a too-long bytevector silently accepted here would
 * only ever have its first sizeof(sock_t) bytes read anyway, so exact
 * match is the only value that unambiguously round-trips through
 * net_sock_to_val's own construction. */
static inline bool net_is_raw_socket_handle(curry_val v) {
    if (!curry_is_pair(v) || !curry_is_symbol(curry_car(v))) return false;
    if (strcmp(curry_symbol(curry_car(v)), "socket") != 0) return false;
    curry_val bv = curry_cdr(v);
    return curry_is_bytevector(bv) && curry_bytevector_length(bv) == sizeof(sock_t);
}

/* Accepts either a raw socket handle (tcp-listen's/udp-socket's/SRFI-106
 * make-client-socket's/make-server-socket's/socket-accept's return) or a
 * port (tcp-connect's/tcp-accept's in-port or out-port) -- extract_fd
 * dispatches on which it got. */
static inline int net_extract_fd(curry_val v, const char *who) {
    if (net_is_raw_socket_handle(v)) {
        sock_t fd = net_val_to_sock(v);
        /* Issue #160: shape alone doesn't prove this fd is one curry's
         * own socket primitives ever actually opened -- see the registry
         * comment above. */
        if (!net_fd_registry_contains(fd)) curry_error("%s: not a socket handle", who);
        return (int)fd;
    }
    int fd = curry_port_fd(v);
    if (fd < 0) curry_error("%s: not a socket handle or file-backed port", who);
    return fd;
}

/* Issue #158 follow-up (found by independent code review of the fix
 * above): net_val_to_sock itself is still unconditionally called
 * directly -- bypassing net_is_raw_socket_handle's validation entirely
 * -- by every network.c primitive that only ever accepts a raw handle,
 * never a port (tcp-accept, tcp-close, udp-bind, udp-send, udp-recv):
 * they call the raw #define val_to_sock (== net_val_to_sock) on av[0]
 * with no check at all, reachable directly from Scheme since these are
 * ordinary user-callable builtins. Reproduced as an actual SIGSEGV, not
 * just a benign clean error, for a malformed handle like
 * (cons 'socket 42) passed to tcp-close/udp-bind. Those primitives
 * can't use net_extract_fd (which also silently accepts a port, wrong
 * for functions that make no sense on anything but a raw handle -- see
 * srfi106.c's fn_socket_close, which already gets this right with its
 * own inline net_is_raw_socket_handle check for the identical reason).
 * This is that same check, factored out so five call sites don't each
 * duplicate it. */
static inline sock_t net_checked_val_to_sock(curry_val v, const char *who) {
    if (!net_is_raw_socket_handle(v)) curry_error("%s: not a socket handle", who);
    sock_t fd = net_val_to_sock(v);
    /* Issue #160: see the registry comment above -- shape alone doesn't
     * prove provenance. */
    if (!net_fd_registry_contains(fd)) curry_error("%s: not a socket handle", who);
    return fd;
}

#ifndef _WIN32
/* On at least macOS/BSD (confirmed empirically -- not just a
 * theoretical concern), a socket returned by accept() can inherit the
 * LISTENING socket's O_NONBLOCK flag rather than starting fresh in
 * blocking mode (Linux does not do this, but this codebase can't
 * assume Linux-only). Every caller of accept() in this file hands the
 * returned fd to curry's ordinary blocking-I/O port machinery, which
 * would otherwise see spurious EWOULDBLOCK/EAGAIN on the very first
 * read/write -- reproduced exactly this way during #238's development:
 * a real client connecting and completing its side of a handshake,
 * while the server side raised immediately instead of blocking to read
 * the client's request, because net_accept_with_timeout (below)
 * necessarily sets the listener non-blocking for its retry loop. The
 * same inheritance can also happen via any ordinary accept() call on a
 * listener a script separately made non-blocking with
 * socket-set-nonblocking! -- so every caller of this function calls it
 * unconditionally on every successful accept(), not just the timeout
 * path, closing the whole bug class rather than one entry point into
 * it. A hard error (not a silent no-op) if F_GETFL itself fails on the
 * freshly-accepted fd -- independent review flagged the earlier
 * silent-fail-open version as reproducing this exact bug quietly in
 * that corner case instead of surfacing it. */
static inline void net_clear_client_nonblock(sock_t client, const char *who) {
    int flags = fcntl(client, F_GETFL, 0);
    if (flags < 0) curry_error("%s: fcntl(F_GETFL) failed on accepted connection", who);
    if (flags & O_NONBLOCK) fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
}

/* Issue #244: shared deadline helpers for every poll()-with-timeout
 * retry loop in this file (net_accept_with_timeout below and
 * fn_socket_ready_p in network.c). This exact logic had to be fixed in
 * lockstep, by hand, in two independently-duplicated copies twice in
 * one sitting (issue #241: first the "never attempts a real poll() once
 * the deadline has technically already passed" bug, then the "EINTR
 * isn't gated on that same expired check" follow-up) before this
 * refactor landed -- one shared implementation instead of two hand-kept-
 * in-sync ones, so a third fix to this logic only has to happen once.
 *
 * isfinite() rejects both NaN and +/-Infinity -- ms < 0 alone does NOT
 * reject NaN (every NaN comparison is false), so +nan.0 would otherwise
 * sail straight through into the deadline arithmetic and hit undefined
 * behavior on the double -> integer casts (C11 6.3.1.4). The upper
 * bound similarly guards against a huge finite value (e.g. 1e300)
 * doing the same -- 1e9 ms is ~11.5 days, already an absurd timeout for
 * either an accept or a readiness check, so anything past it is a
 * caller bug to report cleanly rather than silently truncate into
 * garbage. */
static inline struct timespec net_deadline_from_ms(double ms, const char *who) {
    if (!isfinite(ms) || ms < 0 || ms > 1.0e9)
        curry_error("%s: timeout-ms must be a non-negative finite number (max 1e9)", who);
    /* Single conversion from ms to total nanoseconds, not a separate
     * tv_sec/tv_nsec split computed independently from ms/1000.0 and
     * ms%1000 -- avoids those two derived values ever disagreeing with
     * each other by a rounding hair. */
    long long total_ns = (long long)(ms * 1.0e6);
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec  += (time_t)(total_ns / 1000000000LL);
    deadline.tv_nsec += (long)(total_ns % 1000000000LL);
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    return deadline;
}

/* Remaining time until `deadline`, clamped to a nonnegative poll()-ready
 * millisecond timeout. Sets *expired once the deadline has already
 * passed -- the CALLER must still attempt one real poll() with the
 * returned (0) timeout rather than skip straight to "not ready"/"timed
 * out" (issue #241: the naive "check the deadline, bail out before ever
 * polling" version silently never checked the fd's real state at all
 * for a timeout-ms of 0, or any sufficiently small value, since this
 * function's own clock_gettime() necessarily reads a moment later than
 * the deadline's own "now" baseline). Only stop retrying once *expired
 * comes back true on an attempt that itself still found nothing. */
static inline int net_remaining_poll_ms(struct timespec deadline, bool *expired) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double remaining_ms = (double)(deadline.tv_sec  - now.tv_sec)  * 1000.0
                         + (double)(deadline.tv_nsec - now.tv_nsec) / 1.0e6;
    if (remaining_ms <= 0) { *expired = true; return 0; }
    *expired = false;
    return (int)ceil(remaining_ms);
}

/* Issue #238: a race-free accept-with-timeout, shared by network.c's
 * tcp-accept and srfi106.c's socket-accept (both otherwise duplicating
 * the identical logic). The naive approach -- poll with socket-ready?
 * (a poll() call as of issue #239; a select() call originally), then a
 * separate ordinary blocking accept() -- has a genuine TOCTOU gap: a
 * connection that completes the handshake (making the poll report the
 * listener readable) can be reset by the client before the *separate*
 * accept() call dequeues it, at which point accept() blocks again
 * waiting for the next one, with no further timeout check at all --
 * silently defeating the very bound the caller asked for. That was
 * issue #237's first cut at this (see fn_socket_ready_p in network.c,
 * still used standalone elsewhere).
 *
 * This closes the gap properly: temporarily sets the listening socket
 * non-blocking, then loops poll()-then-accept() against a single
 * absolute deadline (recomputing the *remaining* budget each
 * iteration, never resetting to the full timeout) until a connection
 * is actually accepted or the deadline passes. A connection that
 * vanishes between poll() and accept() (EWOULDBLOCK/EAGAIN/EINTR/
 * ECONNABORTED) just falls through to the next loop iteration with
 * whatever time budget is left, rather than returning a stale
 * "success" or blocking unboundedly.
 *
 * The listening socket's original blocking-mode flag is always
 * restored before this returns (success, timeout, or error) -- it's a
 * borrowed fd the caller still owns and will keep using afterward
 * (accepting more connections, closing it, etc.), so leaving it
 * permanently non-blocking would be a surprising side effect on
 * unrelated future calls. */

static inline sock_t net_accept_with_timeout(sock_t server, double ms, const char *who) {
    struct timespec deadline = net_deadline_from_ms(ms, who);

    int orig_flags = fcntl(server, F_GETFL, 0);
    if (orig_flags < 0) curry_error("%s: fcntl(F_GETFL) failed", who);
    if (fcntl(server, F_SETFL, orig_flags | O_NONBLOCK) < 0)
        curry_error("%s: fcntl(F_SETFL) failed", who);

    for (;;) {
        bool expired;
        int timeout_ms = net_remaining_poll_ms(deadline, &expired);

        /* poll(), not select()/FD_SET: a fd_set is a fixed-size bitmap
         * (1024 bits on Linux/macOS/BSD) and FD_SET does an unchecked
         * write into it -- for a listening socket fd number >=
         * FD_SETSIZE (realistic on a long-lived process with many
         * concurrent sockets/actors, exactly the shape of process that
         * would use an accept timeout in the first place), that's an
         * out-of-bounds stack write, not just "select() misbehaves".
         * poll()'s pollfd array has no such fixed limit. */
        struct pollfd pfd;
        pfd.fd = server;
        pfd.events = POLLIN;
        pfd.revents = 0;
        curry_gc_thread_park();
        int r = poll(&pfd, 1, timeout_ms);
        curry_gc_thread_unpark();
        if (r < 0) {
            bool was_eintr = (errno == EINTR);
            if (!was_eintr) {
                fcntl(server, F_SETFL, orig_flags);
                curry_error("%s: poll failed", who);
            }
            /* Issue #241 follow-up review: gate EINTR on `expired` too,
             * matching every other "attempt came back empty" branch
             * below -- a signal that keeps interrupting poll() right at
             * or after the deadline must not be able to retry forever
             * past the caller's requested bound. */
            if (!expired) continue;
            fcntl(server, F_SETFL, orig_flags);
            curry_error("%s: timed out after %g ms waiting for a client connection", who, ms);
        }
        if (r == 0 || !(pfd.revents & POLLIN)) {
            /* r == 0: nothing ready within timeout_ms. Nonzero r but no
             * POLLIN: POLLERR/POLLHUP/POLLNVAL instead -- nothing to
             * accept yet either. Either way, only stop retrying once
             * this attempt was already known to be past the deadline. */
            if (!expired) continue;
            fcntl(server, F_SETFL, orig_flags);
            curry_error("%s: timed out after %g ms waiting for a client connection", who, ms);
        }

        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        sock_t client = accept(server, (struct sockaddr *)&addr, &addrlen);
        if (client != SOCK_INVALID) {
            fcntl(server, F_SETFL, orig_flags);
            net_clear_client_nonblock(client, who);
            return client;
        }
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR || errno == ECONNABORTED) {
            /* connection vanished between poll() and accept() -- retry
             * within remaining budget, unless there isn't any left. */
            if (!expired) continue;
            fcntl(server, F_SETFL, orig_flags);
            curry_error("%s: timed out after %g ms waiting for a client connection", who, ms);
        }
        fcntl(server, F_SETFL, orig_flags);
        curry_error("%s: accept failed", who);
    }
}
#endif /* !_WIN32 */

#endif /* CURRY_NETWORK_INTERNAL_H */
