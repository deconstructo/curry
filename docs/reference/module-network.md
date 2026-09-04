# Module: (curry network)

*v1.2.2 — 2026-06-07; non-blocking mode + readiness check added later.*

TCP and UDP socket primitives, plus non-blocking mode and readiness
polling. Uses POSIX sockets on Linux/macOS and Winsock2 on Windows.
*v1.2.2 — 2026-06-07; TLS client support added later.*

TCP, TLS, and UDP socket primitives. Uses POSIX sockets on Linux/macOS and Winsock2 on Windows; TLS via OpenSSL.

## Installation

No extra packages required. Enabled by default (`-DBUILD_MODULE_NETWORK=ON`).

## Import

```scheme
(import (curry network))
```

## TCP

### Client

```scheme
(tcp-connect host port)     ; connect to host:port, return (in-port . out-port)
```

`tcp-connect` returns a **pair of ports** — `(in-port . out-port)` — not a
single socket handle. A Curry port is one-directional, and a TCP connection
needs both read and write, so you get two independent ports over the same
underlying connection. Close each with `close-port` when done.

### Server

```scheme
(tcp-listen port)           ; listen on port (all interfaces), return a raw socket handle
(tcp-listen port backlog)   ; listen with explicit backlog
(tcp-accept listen-sock)    ; block until a client connects, return (in-port . out-port)
(tcp-close listen-sock)     ; close the *listening* socket (not a port pair)
```

`tcp-listen`'s own return value is a raw listening-socket handle (not a
stream, so not a port) — close it with `tcp-close`. Each accepted
connection from `tcp-accept` is a port pair like `tcp-connect`'s, closed
with `close-port` on each end.

```scheme
(socket-local-port sock)   ; the actual port a socket is bound to
```

Pass `0` as `tcp-listen`'s (or SRFI-106 `make-server-socket`'s) `port` to
ask the OS for an arbitrary free ephemeral port instead of a hardcoded
one — the standard way to avoid port-collision flakiness when a test or
short-lived server doesn't care which specific port it gets, just that it
gets *a* free one. Since the OS picks the port, you don't know which one
until after `bind()` has already happened; `socket-local-port` reads it
back via `getsockname`. Works on any socket handle from `tcp-listen`,
`udp-socket`, or SRFI-106's `make-client-socket`/`make-server-socket`/
`socket-accept` (they all share the same underlying representation).
`(curry websocket)`'s `ws-listener-port` is the equivalent for a
`ws-listen` listener.

### TLS client

```scheme
(tcp-connect-tls host port)   ; connect + TLS handshake, return (in-port . out-port)
```

Same port-pair contract as `tcp-connect`, but the connection is TLS-wrapped
via OpenSSL. Certificate verification is **on** by default (system trust
store, `SSL_VERIFY_PEER`) — connecting to a host with an invalid or
self-signed certificate raises rather than silently connecting insecurely.
SNI (`SSL_set_tlsext_host_name`) is sent automatically using the `host`
argument, needed by most modern HTTPS servers that multiplex by hostname.

No TLS server side (`tls-listen`/`tls-accept`) yet — client-side was the
actually-blocking gap (talking to HTTPS-only APIs over raw sockets); see
[issue #14](https://github.com/deconstructo/curry/issues/14) if you need
server-side TLS.

## UDP

```scheme
(udp-socket)                    ; create a UDP socket
(udp-bind sock port)            ; bind to a local port
(udp-send sock data host port)  ; send bytevector data to host:port
(udp-recv sock max-bytes)       ; receive up to max-bytes; returns (data host port)
```

UDP is datagram-oriented, not stream-oriented, so `udp-socket` stays a raw
handle — there's no port wrapping for it.

## Non-blocking mode and readiness

```scheme
(socket-set-nonblocking! sock)     ; fcntl O_NONBLOCK
(socket-ready? sock)               ; immediate poll, #t if data/connection pending
(socket-ready? sock timeout-ms)    ; block up to timeout-ms waiting for readiness
```

Both accept either a raw socket handle (`tcp-listen`'s/`udp-socket`'s
return value) or a port (`tcp-connect`'s/`tcp-accept`'s in-port or
out-port) — whichever you have on hand. `socket-ready?` on a listening
socket means "`tcp-accept` would not block"; on a connected port it means
"there's data to read without blocking."

This is deliberately **not** a full epoll/kqueue event-loop reactor.
curry's actors are real OS threads (not green threads/coroutines — those
are a separate, unimplemented roadmap item), so a full reactor integration
wouldn't buy much over the current thread-per-actor model yet. What's here
covers a real, immediately useful case: multiplexing a handful of sockets
on one thread without needing an actor per connection. See
[issue #15](https://github.com/deconstructo/curry/issues/15) for the
fuller reactor, if/when green threads make it worthwhile.

## Examples

### HTTP GET (manual)

```scheme
(import (curry network))

(define conn (tcp-connect "example.com" 80))
(define in  (car conn))
(define out (cdr conn))

(write-string "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n" out)
(flush-output-port out)

(let loop ((line (read-line in)))
  (unless (eof-object? line)
    (display line) (newline)
    (loop (read-line in))))

(close-port in)
(close-port out)
```

### HTTPS GET (manual, TLS)

```scheme
(import (curry network))

(define conn (tcp-connect-tls "example.com" 443))
(define in  (car conn))
(define out (cdr conn))

(write-string "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n" out)
(flush-output-port out)

(let loop ((line (read-line in)))
  (unless (eof-object? line)
    (display line) (newline)
    (loop (read-line in))))

(close-port in)
(close-port out)
```

### Echo server

```scheme
(import (curry network))
(import (scheme base))

(define listener (tcp-listen 7777))
(display "Listening on port 7777...\n")

(let loop ()
  (define conn (tcp-accept listener))
  (define in  (car conn))
  (define out (cdr conn))
  (spawn (lambda ()
    (let echo ()
      (define line (read-line in))
      (unless (eof-object? line)
        (write-string line out)
        (write-string "\n" out)
        (flush-output-port out)
        (echo)))
    (close-port in)
    (close-port out)))
  (loop))
```

### Single-threaded multiplexed server (no actor per connection)

```scheme
(import (curry network))
(import (scheme base))

(define listener (tcp-listen 7778))
(define conns '())   ; list of (in . out) pairs currently connected

(let loop ()
  ;; Accept a new connection if one is pending, without blocking.
  (when (socket-ready? listener)
    (set! conns (cons (tcp-accept listener) conns)))
  ;; Service whichever existing connections have data ready.
  (set! conns
    (filter
      (lambda (conn)
        (if (socket-ready? (car conn))
            (let ((line (read-line (car conn))))
              (if (eof-object? line)
                  (begin (close-port (car conn)) (close-port (cdr conn)) #f)
                  (begin (write-string line (cdr conn))
                         (write-string "\n" (cdr conn))
                         (flush-output-port (cdr conn))
                         #t)))
            #t))
      conns))
  (loop))
```

### UDP echo

> **Currently broken** — `udp-recv` uses `recv()`, not `recvfrom()`, so it
> cannot actually report the sender's address; it returns only the raw
> bytevector today, not `(data host port)` as shown below. This example
> documents the intended contract. See
> [issue #16](https://github.com/deconstructo/curry/issues/16).

```scheme
(import (curry network))

(define sock (udp-socket))
(udp-bind sock 9999)

(let loop ()
  (define-values (data host port) (apply values (udp-recv sock 1024)))
  (udp-send sock data host port)
  (loop))
```

## Notes

- `tcp-connect`/`tcp-accept` return `(in-port . out-port)` pairs — real
  Curry ports usable with `read-line`, `write-string`, `read-char`, etc.
  Remember to `flush-output-port` after writing before expecting a reply
  (output ports are buffered by default, same as any other Curry port).
- `tcp-listen` binds to `0.0.0.0`/`::` (all interfaces, dual-stack). To
  bind to a specific interface, use the raw C extension API.
- `tcp-connect-tls` requires `BUILD_MODULE_NETWORK=ON` (default) and links
  OpenSSL (already a dependency via `(curry crypto)`) — no extra CMake flag.
