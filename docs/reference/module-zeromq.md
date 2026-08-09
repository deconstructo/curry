# Module: (curry zeromq)

*unreleased*

[ZeroMQ](https://zeromq.org) messaging sockets, via `(curry ffi)`. `libzmq` is dlopen'd lazily at runtime (not linked at build time — no CMake flag beyond the general `BUILD_FFI=ON`), the same pattern `(curry hdf5)`/`(curry ncurses)`/`(curry graphviz)` use.

## Import

```scheme
(import (curry zeromq))
```

Importing this module never requires libzmq to be installed — only the first actual call (`zmq-context`, etc.) does; it raises with install instructions if the library can't be found:

```
macOS:           brew install zeromq
Debian/Ubuntu:   apt install libzmq3-dev
Fedora/RHEL:     dnf install zeromq-devel
```

## Scope

Covers the core messaging primitives: context/socket lifecycle, bind/connect, send/receive (blocking and non-blocking), the common socket options (`SUBSCRIBE`/`UNSUBSCRIBE`/`LINGER`/`RCVTIMEO`/`SNDTIMEO`/`IDENTITY`), and multipart messages (`SNDMORE`/`RCVMORE`). Messages are always plain bytevectors at the primitive level; `zmq-send-string!`/`zmq-recv-string` are thin string↔bytevector convenience wrappers around that (via `string->utf8`/`utf8->string`), not a separate code path.

Deliberately **not** supported:

- **`zmq_poll`** — multiplexing many sockets in one call needs an array of `zmq_pollitem_t` structs, real added FFI complexity this module's core send/recv/sockopt surface didn't otherwise need. A curry program wanting to wait on several sockets today should give each its own actor and a blocking `zmq-recv` in a loop instead.
- **CURVE/PLAIN security mechanisms, `zmq_proxy`, `ZMQ_RADIO`/`ZMQ_DISH`** and other draft-API socket types — outside this module's scope of the stable, widely-used core API.

## Contexts and sockets

### `(zmq-context)` → context

### `(zmq-context? x)` → boolean

### `(zmq-context-destroy! ctx)`

### `(zmq-socket ctx type)` → socket

`type` is one of the symbols `'pair` `'pub` `'sub` `'req` `'rep` `'dealer` `'router` `'pull` `'push` `'xpub` `'xsub` `'stream` — ZeroMQ's own socket types.

### `(zmq-socket? x)` → boolean

### `(zmq-bind! sock endpoint)`

`endpoint` is a ZeroMQ endpoint string, e.g. `"tcp://*:5555"`, `"ipc:///tmp/feed"`, `"inproc://name"`.

### `(zmq-connect! sock endpoint)`

### `(zmq-close! sock)`

```scheme
(import (curry zeromq))
(define ctx (zmq-context))
(define push (zmq-socket ctx 'push))
(define pull (zmq-socket ctx 'pull))
(zmq-bind! pull "inproc://example")
(zmq-connect! push "inproc://example")
```

## Sending and receiving

### `(zmq-send! sock bv . flags)`

Sends `bv` (a bytevector). `flags` is zero or more of the symbols `'dontwait` (non-blocking — still raises rather than silently dropping the message) and `'sndmore` (more frames of this multipart message will follow).

### `(zmq-send-string! sock str . flags)`

`(apply zmq-send! sock (string->utf8 str) flags)`.

### `(zmq-recv sock . flags)` → bytevector or `#f`

Receives a message. With `'dontwait`, returns `#f` if no message is available right now rather than raising — the one case in this module where a "failure" (`EAGAIN`) is a normal, expected outcome rather than an error.

### `(zmq-recv-string sock . flags)` → string or `#f`

`(let ((bv (apply zmq-recv sock flags))) (and bv (utf8->string bv)))`.

### `(zmq-more? sock)` → boolean

True iff more parts of the current multipart message remain to be received (`ZMQ_RCVMORE`).

```scheme
(import (curry zeromq))
(zmq-send-string! push "hello")
(zmq-recv-string pull)   ; => "hello"

;; Multipart:
(zmq-send-string! push "part1" 'sndmore)
(zmq-send-string! push "part2")
(zmq-recv-string pull)   ; => "part1"
(zmq-more? pull)         ; => #t
(zmq-recv-string pull)   ; => "part2"
(zmq-more? pull)         ; => #f
```

## Socket options

### `(zmq-set-linger! sock ms)`
### `(zmq-set-rcvtimeo! sock ms)`
### `(zmq-set-sndtimeo! sock ms)`
### `(zmq-set-identity! sock id)`

`id` is a string or bytevector.

### `(zmq-subscribe! sock topic)`
### `(zmq-unsubscribe! sock topic)`

`topic` is a string or bytevector; an empty string/bytevector subscribes to everything. Only meaningful on `'sub`/`'xsub` sockets.

```scheme
(import (curry zeromq))
(define sub (zmq-socket ctx 'sub))
(zmq-connect! sub "tcp://localhost:5556")
(zmq-subscribe! sub "weather.")
```

## Misc

### `(zmq-version)` → `(list major minor patch)`

## See also

- [`module-hdf5.md`](module-hdf5.md), [`module-ncurses.md`](module-ncurses.md), [`module-graphviz.md`](module-graphviz.md) — the other modules using this same runtime-dlopen FFI pattern
- [`module-ffi.md`](module-ffi.md) — the general C FFI this module is built on
