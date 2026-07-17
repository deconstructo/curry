# Module: (curry network)

*v1.2.2 — 2026-06-07*

TCP and UDP socket primitives. Uses POSIX sockets on Linux/macOS and Winsock2 on Windows.

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

## UDP

```scheme
(udp-socket)                    ; create a UDP socket
(udp-bind sock port)            ; bind to a local port
(udp-send sock data host port)  ; send bytevector data to host:port
(udp-recv sock max-bytes)       ; receive up to max-bytes; returns (data host port)
```

UDP is datagram-oriented, not stream-oriented, so `udp-socket` stays a raw
handle — there's no port wrapping for it.

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
- For TLS, wrap a `tcp-connect` socket with the crypto module (OpenSSL BIO)
  — not yet exposed in the Scheme API; use `system` to call
  `openssl s_client` as a workaround. See
  [issue #14](https://github.com/deconstructo/curry/issues/14).
