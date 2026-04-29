# Module: (curry network)

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
(tcp-connect host port)     ; connect to host:port, return socket handle
(tcp-close sock)            ; close the socket
```

Once connected, use `read`/`write` on the socket as a port, or use the raw procedures below.

### Server

```scheme
(tcp-listen port)           ; listen on port (all interfaces)
(tcp-listen port backlog)   ; listen with explicit backlog
(tcp-accept listen-sock)    ; block until a client connects, return socket handle
(tcp-close sock)
```

## UDP

```scheme
(udp-socket)                    ; create a UDP socket
(udp-bind sock port)            ; bind to a local port
(udp-send sock host port data)  ; send bytevector data to host:port
(udp-recv sock max-bytes)       ; receive up to max-bytes; returns (data host port)
```

## Examples

### HTTP GET (manual)

```scheme
(import (curry network))

(define sock (tcp-connect "example.com" 80))
; send request
(write-string "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n" sock)
; read response
(let loop ((line (read-line sock)))
  (unless (eof-object? line)
    (display line) (newline)
    (loop (read-line sock))))
(tcp-close sock)
```

### Echo server

```scheme
(import (curry network))
(import (scheme base))

(define listener (tcp-listen 7777))
(display "Listening on port 7777...\n")

(let loop ()
  (define client (tcp-accept listener))
  (spawn (lambda ()
    (let echo ()
      (define line (read-line client))
      (unless (eof-object? line)
        (write-string line client)
        (write-string "\n" client)
        (echo)))
    (tcp-close client)))
  (loop))
```

### UDP echo

```scheme
(import (curry network))

(define sock (udp-socket))
(udp-bind sock 9999)

(let loop ()
  (define-values (data host port) (apply values (udp-recv sock 1024)))
  (udp-send sock host port data)
  (loop))
```

## Notes

- Socket handles are opaque values. They can be passed to port-based procedures (`read-line`, `write-string`, `read-char`, etc.) since they are implemented as Curry ports.
- `tcp-listen` binds to `0.0.0.0` (all interfaces). To bind to a specific interface, use the raw C extension API.
- For TLS, wrap a `tcp-connect` socket with the crypto module (OpenSSL BIO) — this is not yet exposed in the Scheme API; use `system` to call `openssl s_client` as a workaround.
