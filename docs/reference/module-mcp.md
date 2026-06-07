# Module: `(curry mcp)`

*v1.2.2 — 2026-06-07*

MCP (Model Context Protocol) server for Curry Scheme. Exposes Scheme functions
as MCP tools and resources callable by Claude Code and other MCP clients.
Two transports: **stdio** (one client, spawned per session) and **SSE**
(HTTP + Server-Sent Events, many concurrent clients).

Authentication is optional and supports three OAuth 2.0 modes — see
[`docs/guides/mcp-authentication.md`](../guides/mcp-authentication.md) for
worked examples.

## Import

```scheme
(import (curry mcp))
```

## Build requirement

The MCP module requires OpenSSL (used for auth and UUID generation).
Enable it with `-DBUILD_MODULE_MCP=ON` when configuring CMake.

---

## Tool and resource registration

### `(mcp-tool name description schema handler)`

Register a callable tool. Must be called before `mcp-serve` / `mcp-serve-sse`.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Unique tool name shown to the LLM |
| `description` | string | Human-readable description of what the tool does |
| `schema` | alist | Parameter schema (see below) |
| `handler` | procedure | Called with an alist of `(symbol . value)` pairs |

**Schema format** — alist mapping parameter names (symbols or strings) to
property alists:

```scheme
'((param-name . ((type . "string")
                 (description . "What this param does")
                 (default . "optional default value"))))
```

Parameters without a `(default . ...)` entry are marked required in the
JSON Schema sent to the LLM.

**Handler** receives one argument: an alist of `(symbol . value)` pairs.
Values are decoded from JSON: string → string, integer → fixnum,
number → flonum, boolean → `#t`/`#f`, array → list, null → `'()`.

The handler must return either an `(mcp-text ...)` or `(mcp-json ...)` value.

```scheme
(mcp-tool "add" "Add two numbers"
  '((a . ((type . "number") (description . "First operand")))
    (b . ((type . "number") (description . "Second operand"))))
  (lambda (args)
    (let ((a (cdr (assq 'a args)))
          (b (cdr (assq 'b args))))
      (mcp-text (number->string (+ a b))))))
```

---

### `(mcp-resource uri description handler)`

Register a browseable resource. `handler` receives the URI string and must
return `(mcp-text ...)` or a plain string.

```scheme
(mcp-resource "file:///config.json" "Server configuration"
  (lambda (uri) (mcp-text (read-file "config.json"))))
```

---

## Result constructors

### `(mcp-text string)` → result value

Wraps a string as a plain-text tool/resource result.

### `(mcp-json value)` → result value

Wraps any Scheme value as a JSON tool result. The value is serialised to JSON
using the same rules as `mcp-serve` output.

### `(mcp-notify-progress current total message)`

Emit an MCP `notifications/progress` event to the client during a long-running
tool call. `current` and `total` are numbers; `message` is an optional status
string. Has no effect when called outside a tool handler.

```scheme
(mcp-tool "slow-task" "A task that takes a while" '()
  (lambda (_)
    (mcp-notify-progress 0 3 "starting")
    (do-step-1)
    (mcp-notify-progress 1 3 "step 1 done")
    (do-step-2)
    (mcp-notify-progress 2 3 "step 2 done")
    (do-step-3)
    (mcp-text "done")))
```

---

## Transport — stdio

### `(mcp-serve)` · `(mcp-serve name version)`

Start the stdio JSON-RPC transport. Blocks reading from stdin until EOF.
Use this when Claude Code spawns the server as a subprocess.

```scheme
(mcp-serve)                    ; server name defaults to "curry-mcp"
(mcp-serve "my-server" "1.0")  ; custom name and version
```

**Claude Code config** (`~/.config/claude/claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "my-server": {
      "command": "/path/to/build/curry",
      "args":    ["/path/to/my_server.scm"]
    }
  }
}
```

---

## Transport — SSE

### `(mcp-serve-sse port)` · `(mcp-serve-sse port name)` · `(mcp-serve-sse port name version)`

Start the HTTP + SSE transport. Blocks forever; spawns one thread per
incoming connection.

**Endpoints:**

| Method | Path | Purpose |
|--------|------|---------|
| `GET`  | `/sse` | Open SSE event stream; emits `event: endpoint` with the POST URL |
| `POST` | `/message?sessionId=<id>` | Submit a JSON-RPC request; response arrives via SSE |
| `POST` | `/token` | Issue an access token (self-contained auth mode only) |
| `OPTIONS` | any | CORS preflight — always allowed without auth |

```scheme
(mcp-serve-sse 8080)
(mcp-serve-sse 8080 "my-server")
(mcp-serve-sse 8080 "my-server" "2.1.0")
```

Prints `[mcp] SSE server listening on port N` to stderr when ready.

**Claude Code config** (persistent SSE server):

```json
{
  "mcpServers": {
    "my-server": {
      "url": "http://localhost:8080/sse"
    }
  }
}
```

---

## Authentication

All auth procedures must be called **before** `mcp-serve-sse`. Auth is not
applied to the stdio transport.

### `(mcp-auth-mode! mode)`

Select the authentication mode. `mode` is one of:

| Symbol | Behaviour |
|--------|-----------|
| `'none` | No auth — any client may connect (default) |
| `'self-contained` | Server issues opaque tokens via `POST /token` (RFC 6749 §4.4) |
| `'introspect` | Validate tokens via an external RFC 7662 introspection endpoint |
| `'jwt` | Validate JWT Bearer tokens locally with OpenSSL (RFC 7519) |

---

### Mode `self-contained` — Client Credentials

The server acts as its own authorisation server.

#### `(mcp-register-client! id secret)`

Register a client. Clients exchange their `id` + `secret` for an access token
via `POST /token`. Call multiple times to register multiple clients.

#### `(mcp-token-ttl! seconds)`

Set the access token lifetime in seconds. Default: `3600`.

**Token endpoint:**

```
POST /token
Content-Type: application/x-www-form-urlencoded

grant_type=client_credentials&client_id=<id>&client_secret=<secret>
```

**Success response** (`200 OK`):

```json
{ "access_token": "<uuid>", "token_type": "Bearer", "expires_in": 3600 }
```

**Error responses** follow RFC 6749 §5.2:

| HTTP | `error` field | Cause |
|------|---------------|-------|
| 400 | `unsupported_grant_type` | `grant_type` ≠ `client_credentials` |
| 401 | `invalid_client` | Unknown client ID or wrong secret |
| 503 | `server_error` | Token store full |

---

### Mode `introspect` — RFC 7662

#### `(mcp-introspection-endpoint! url)`

URL of the identity provider's RFC 7662 introspection endpoint.
Both `http://` and `https://` are supported.

#### `(mcp-introspection-credentials! id secret)`

Optional. Send HTTP Basic authentication to the introspection endpoint using
these resource-server credentials.

#### `(mcp-introspection-cache-ttl! seconds)`

How long to cache introspection results in process memory. Default: `60`.
Increase for performance; decrease for tighter revocation detection.

**Introspection request** made by the server for each uncached token:

```
POST <introspection-url>
Authorization: Basic <base64(id:secret)>   ; if credentials configured
Content-Type: application/x-www-form-urlencoded

token=<bearer-token>
```

The server checks that the response contains `"active": true`.

---

### Mode `jwt` — RFC 7519

#### `(mcp-jwt-algorithm! alg)`

Select the signature algorithm. `alg` is one of:

| Symbol | Algorithm | Key type |
|--------|-----------|----------|
| `'hs256` | HMAC-SHA-256 | Shared secret string |
| `'rs256` | RSA-SHA-256 | RSA public key (PEM) |

Default: `'hs256`.

#### `(mcp-jwt-secret! secret-string)`

Set the HMAC secret for HS256. Should be at least 32 random bytes.

#### `(mcp-jwt-public-key! path)`

Load an RS256 public key from a PEM file at `path`.

#### `(mcp-jwt-public-key-pem! pem-string)`

Load an RS256 public key from an inline PEM string.

#### `(mcp-jwt-issuer! iss)`

Optional. If set, the JWT `iss` claim must equal `iss`.

#### `(mcp-jwt-audience! aud)`

Optional. If set, the JWT `aud` claim must equal `aud`.

**JWT validation steps performed:**

1. Split token into `header.payload.signature`
2. Verify the signature matches the algorithm and key
3. Check `exp` is in the future
4. Check `iss` if `mcp-jwt-issuer!` was called
5. Check `aud` if `mcp-jwt-audience!` was called

Tokens failing any check are rejected with HTTP 401.

---

## Error responses

All auth failures return:

```
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Bearer realm="mcp"[, error="...", error_description="..."]
Content-Length: 0
```

The `error` and `error_description` fields follow RFC 6750 §3.1:

| `error` | When |
|---------|------|
| (none) | No token was sent at all |
| `invalid_token` | Token present but invalid, expired, or not recognised |

---

## Schema quick reference

```scheme
; Full parameter schema example
'((query  . ((type . "string")
             (description . "Search terms")))
  (limit  . ((type . "integer")
             (description . "Max results (optional)")
             (default . 10)))
  (debug  . ((type . "boolean")
             (description . "Enable verbose output")
             (default . #f))))
```

Supported `type` values: `"string"`, `"number"`, `"integer"`, `"boolean"`,
`"array"`, `"object"`.

---

## Handler argument helpers

```scheme
; Get a required parameter (raises if missing)
(define (arg args name) (cdr (assq name args)))

; Get an optional parameter with a default
(define (arg? args name default)
  (let ((p (assq name args))) (if p (cdr p) default)))

; Usage in a handler:
(lambda (args)
  (let ((q     (arg  args 'query))
        (limit (arg? args 'limit 10)))
    (mcp-text (search q limit))))
```

---

## Protocol notes

- **stdio** — newline-delimited JSON-RPC 2.0; one client only; synchronous dispatch.
- **SSE** — one thread per HTTP connection; up to 32 concurrent sessions; tool calls
  serialised by a mutex (Scheme evaluator is single-threaded); keepalive comments
  sent every 15 s; up to 1 MB per JSON-RPC message.
- Scheme global state persists across tool calls for the lifetime of the server process.
- `(self)` is not available from tool handlers (main thread is not an actor).
- Tool exceptions are caught and returned as JSON-RPC error responses
  (`code: -32603`); the server continues running normally.
- Auth tokens in `self-contained` mode are stored in process memory only.
  They are lost on server restart.

---

## See also

- [Authentication guide](../guides/mcp-authentication.md) — step-by-step OAuth 2.0 setup
- [Example servers](../../examples/mcp_server.scm) — `mcp_server.scm`, `mcp_math.scm`, `mcp_nbody.scm`
