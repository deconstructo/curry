# Module: (curry websocket)

*unreleased*

A plain (no TLS) RFC 6455 WebSocket client, pure Scheme — built entirely on curry's existing SRFI-106 sockets and `(curry crypto)`'s `sha1`/`base64-encode` for the handshake. No new C code, no external library.

`ws://` only. `wss://` would need a TLS byte stream underneath the handshake and framing implemented here; SRFI-106 doesn't provide one. `(curry network)` has its own `tls-connect`, but it isn't wired into this module — a `wss://` client would mean layering this module's protocol logic over that instead of a plain socket.

## Installation

No extra packages required. Always available — pure Scheme, no CMake flag.

## Import

```scheme
(import (curry websocket))
```

## Procedures

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
