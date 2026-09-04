# Module: (curry websocket)

*unreleased*

A plain (no TLS) RFC 6455 WebSocket client **and server**, pure Scheme — built entirely on curry's existing SRFI-106 sockets and `(curry crypto)`'s `sha1`/`base64-encode` for the handshake. No new C code, no external library.

`ws://` only. `wss://` would need a TLS byte stream underneath the handshake and framing implemented here; SRFI-106 doesn't provide one. `(curry network)` has its own `tls-connect`, but it isn't wired into this module — a `wss://` client/server would mean layering this module's protocol logic over that instead of a plain socket.

Client and server share every piece of frame-level machinery (masking, length encoding, fragmentation reassembly, ping/pong) — the only place the role actually matters is which direction masks: RFC 6455 §5.1 requires every client-to-server frame to be masked and forbids masking server-to-client frames, and requires a peer that receives a frame violating this to fail the connection (this is a real security requirement — defense against cache/protocol-confusion attacks via intermediaries that don't understand WebSocket framing — not just wire-format tidiness). `ws-recv!` enforces both directions: a server rejects an unmasked incoming frame, a client rejects a masked one, closing the connection and raising a clear error either way rather than silently tolerating the violation.

## Installation

No extra packages required. Always available — pure Scheme, no CMake flag.

## Import

```scheme
(import (curry websocket))
```

## Procedures — client

### `(ws-connect url)` → ws

Connects to `url` (must start with `ws://`) and performs the RFC 6455 opening handshake: sends the HTTP `Upgrade: websocket` request with a fresh random `Sec-WebSocket-Key`, reads the server's response, and verifies `Sec-WebSocket-Accept` against the expected value. Raises if the server doesn't upgrade or the accept hash doesn't match.

### `(ws-send! ws string)`

Sends `string` as a single text frame (UTF-8 encoded, masked, as required for every client-to-server frame).

### `(ws-send-binary! ws bytevector)`

Sends `bytevector` as a single binary frame.

### `(ws-recv! ws)` → string | bytevector | eof-object

Blocks for the next complete message. Transparently handles protocol mechanics the caller shouldn't need to think about:

- **Fragmentation** — a message split across multiple continuation frames is reassembled before returning.
- **Ping** — answered with a pong automatically; `ws-recv!` keeps waiting for real data.
- **Pong** — ignored.
- **Close** — the connection is marked closed and `(eof-object)` is returned.

Returns a string for a text message, a bytevector for a binary message, or `(eof-object)` once the peer has closed the connection.

### `(ws-close! ws)`

Sends a close frame (if not already closed) and closes the underlying socket.

### `(ws-closed? ws)` → boolean

### `(ws? obj)` → boolean

### `(ws-path ws)` → string | `#f`

The request path a server-side connection was opened against (e.g. `"/live-data"`), for a server that wants to route by path. `#f` for a client-side connection — `ws-connect`'s own caller already knows the path it asked for, it doesn't need it echoed back.

Returned verbatim from the request line — a query string, if the client sent one, stays attached (`GET /live-data?token=abc HTTP/1.1` gives `"/live-data?token=abc"`, not `"/live-data"`). Split it yourself if you're routing on the path alone.

## Procedures — server

### `(ws-listen port)` → listener

Binds and listens on `port`. Doesn't block, doesn't accept anything yet — mirrors `(curry network)`'s own `tcp-listen`. Pass `0` to ask the OS for an arbitrary free ephemeral port instead of a hardcoded one; use `ws-listener-port` to find out which one it picked.

### `(ws-listener-port listener)` → fixnum

The actual port `listener` is bound to — needed after `(ws-listen 0)`, since you don't know which ephemeral port the OS assigned until after the underlying `bind()` has already happened. Equivalent to `(curry network)`'s `socket-local-port` applied to the listener's own socket.

### `(ws-accept listener)` → ws

Blocks for the next incoming TCP connection and completes the RFC 6455 opening handshake on it (reads the client's `GET` request and headers, extracts `Sec-WebSocket-Key`, computes and sends `Sec-WebSocket-Accept`) before returning. By the time `ws-accept` returns, the connection is a fully negotiated WebSocket — `ws-send!`/`ws-recv!`/`ws-close!` all work on it exactly as they would on a `ws-connect` result. Raises if the request has no `Sec-WebSocket-Key` or a malformed request line, if the connection closes before the handshake completes, or if a single header line exceeds 8KB or the request has more than 100 header lines — a bounded-cost rejection of a connection that's misbehaving or attacking, rather than unbounded memory growth (an incoming connection is untrusted by construction, unlike `ws-connect`'s server, which the caller already chose to trust).

### `(ws-listener? obj)` → boolean

### `(ws-listener-close! listener)`

Closes the listening socket. Doesn't affect any connections already accepted from it.

## Example: a minimal echo server

```scheme
(import (curry websocket) (curry sync))

(define listener (ws-listen 8080))

(spawn (lambda ()
         (let loop ()
           (let ((conn (ws-accept listener)))
             (spawn (lambda ()
                      (let handle ()
                        (let ((msg (ws-recv! conn)))
                          (if (not (eof-object? msg))
                              (begin (ws-send! conn msg) (handle)))))))
             (loop)))))
```

One actor accepts connections in a loop; each accepted connection gets its own handler actor so slow or long-lived clients don't block new connections from being accepted. A client that disconnects abruptly mid-message raises inside `ws-recv!` (e.g. "connection closed mid-frame") — uncaught here, which only takes down that one handler actor (actors are isolated; the accept loop and every other connection are unaffected), but production code that wants to log or otherwise handle that gracefully should wrap the handler loop's body in `guard`.

## Thread safety

Multiple actors may call `ws-send!`/`ws-send-binary!`/`ws-close!` on the same connection concurrently — each connection has its own internal mutex serializing the (multi-part) write of a single frame, so concurrent senders can't interleave their bytes on the wire. `ws-recv!` is meant to be called from a single reader (this is exactly the pattern `(curry ros)` uses: one background reader actor calling `ws-recv!` in a loop, plus arbitrarily many other actors calling `ws-send!`).

## Example

```scheme
(import (curry websocket))

(define ws (ws-connect "ws://localhost:9090/"))
(ws-send! ws "hello")
(display (ws-recv! ws))   ; => whatever the server echoes back
(ws-close! ws)
```

## See also

- [`(curry ros)`](module-ros.md) — a rosbridge client built on top of this module
- [Simulating cell biochemistry with Gillespie](../guides/gillespie-cells.md) — unrelated, but another example of a small pure-Scheme module built directly on curry's core primitives
