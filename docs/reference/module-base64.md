# Module: `(curry base64)`

*unreleased*

RFC 4648 base64 encode/decode. Pure Scheme, no build step, no external dependency.

## Import

```scheme
(import (curry base64))
```

## Design

Port-based streaming transcoders are the core (`base64-encode-port`/`base64-decode-port`); the string and bytevector procedures are thin wrappers around them built on curry's own string/bytevector ports (`open-input-string`/`open-input-bytevector`/`open-output-string`/`open-output-bytevector`) — the same "port is the primitive, string/file forms are convenience layers on top" shape `(curry fits)`/`(curry netcdf)` already use for binary formats (their own shared `(curry private binary-io)` helper covers big-endian/IEEE-754 numeric decoding, unrelated to base64's alphabet/bit-accumulator logic, so this module doesn't import it — it's fully self-contained). `read-bytevector` on curry's string/bytevector/file ports only ever returns fewer than requested at genuine end-of-input (these aren't partial-read network sockets), so a short 3-byte read during encoding always means "this is the final, possibly-short group" — no separate end-of-stream signal is needed.

`(curry crypto)` already provides `base64-encode`/`base64-decode`, backed by OpenSSL. This module exists as the dependency-free alternative for anything that wants base64 without pulling in the optional crypto module's build requirement (`libssl-dev`/`-DBUILD_MODULE_CRYPTO=ON`) — `(curry naips)` is the first consumer, decoding the base64-wrapped report text NAIPS's SOAP responses carry.

Standard alphabet only (RFC 4648 §4: `A`-`Z`, `a`-`z`, `0`-`9`, `+`, `/`, `=` padding) — not the URL-safe variant (§5, `-`/`_`).

## API

### `(base64-encode-port in out)`

Reads bytes from binary input port `in`, writes base64 text to textual output port `out`. No trailing newline or line wrapping.

### `(base64-decode-port in out)`

Reads base64 text from textual input port `in`, writes decoded bytes to binary output port `out`. Whitespace (space/tab/newline/CR) between groups — e.g. line-wrapped base64 — is skipped. `=` padding characters are recognized and skipped (they carry no bits by the time they're reached). Any other character outside the base64 alphabet raises immediately, rather than silently dropping it and producing silently-truncated or silently-wrong output.

### `(base64-encode bv)` → *string*

Encodes a bytevector.

### `(base64-decode s)` → *bytevector*

Decodes a base64 string to a bytevector.

### `(base64-encode-string s)` → *string*

Encodes a Scheme string's UTF-8 bytes.

### `(base64-decode-string s)` → *string*

Decodes a base64 string, then interprets the resulting bytes as UTF-8 text. For 7-bit-ASCII payloads (the common case for embedded report/config text) this is byte-for-byte identical to the source; it's also correct for genuine multi-byte UTF-8, unlike a naive per-byte `integer->char` mapping.

```scheme
(import (curry base64))

(base64-encode-string "foobar")             ; => "Zm9vYmFy"
(base64-decode-string "Zm9vYmFy")           ; => "foobar"

(base64-encode (bytevector 1 2 3))          ; => "AQID"
(base64-decode "AQID")                      ; => #u8(1 2 3)

;; Port form — e.g. streaming a large file without holding it all as one string:
(call-with-input-file "image.png"
  (lambda (in)
    (call-with-output-file "image.png.b64"
      (lambda (out) (base64-encode-port in out)))))
```

## Errors

`base64-decode-port`/`base64-decode`/`base64-decode-string` raise a Scheme error on any character that isn't in the base64 alphabet, whitespace, or `=`. There is no lenient/best-effort decode mode.
