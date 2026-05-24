# Guide: Authenticating your MCP server

This guide walks through each of the three OAuth 2.0 authentication modes
supported by `(curry mcp)`, with copy-paste examples you can adapt directly.

---

## Quick orientation

By default the MCP server accepts connections from anyone. You add authentication
by calling `(mcp-auth-mode! ...)` and its companion configuration calls **before**
`(mcp-serve-sse ...)`. The mode is then enforced on every `GET /sse` and
`POST /message` request; unauthenticated clients receive HTTP 401.

There are three modes:

| Mode | Best for |
|------|----------|
| `self-contained` | You control all clients (e.g., your own scripts, Claude Code) |
| `introspect` | You already run a shared identity provider (Keycloak, Auth0, …) |
| `jwt` | Same as above, but you want local validation with no round-trip |

Authentication only applies to the SSE transport. The stdio transport runs as a
subprocess of the MCP client and is secured by the OS process boundary.

---

## Mode A — self-contained (Client Credentials)

The MCP server issues its own Bearer tokens. Clients call `POST /token` with
their credentials and then use the returned token on every subsequent request.
No external identity provider needed.

### 1. Register clients and start the server

```scheme
(import (curry mcp))

; Register one client with an id and secret.
; You can call this multiple times for multiple clients.
(mcp-auth-mode! 'self-contained)
(mcp-register-client! "claude-code" "super-secret-passphrase")

; Optional: shorten or lengthen token lifetime (default 3600 seconds = 1 hour).
(mcp-token-ttl! 1800)

; Register your tools ...
(mcp-tool "hello" "Say hello"
  '((name . ((type . "string") (description . "Name to greet"))))
  (lambda (args) (mcp-text (string-append "Hello, " (cdr (assq 'name args))))))

(mcp-serve-sse 8080)
```

### 2. Get a token

The client POSTs to `/token` using `application/x-www-form-urlencoded`:

```bash
curl -X POST http://localhost:8080/token \
     -d "grant_type=client_credentials&client_id=claude-code&client_secret=super-secret-passphrase"
```

The server responds with:

```json
{
  "access_token": "a3f82c19-...",
  "token_type":   "Bearer",
  "expires_in":   1800
}
```

### 3. Use the token

Pass the access token as a Bearer header on every request to `/sse` and `/message`:

```bash
# Open the SSE stream
curl -N http://localhost:8080/sse \
     -H "Authorization: Bearer a3f82c19-..."

# POST a tool call
curl -X POST "http://localhost:8080/message?sessionId=<id>" \
     -H "Content-Type: application/json" \
     -H "Authorization: Bearer a3f82c19-..." \
     -d '{"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"hello","arguments":{"name":"world"}}}'
```

### Connecting from Claude Code

In your `claude_desktop_config.json` or Claude Code settings, use the `url` form
with the auth header set through your HTTP proxy or shell wrapper, **or** handle
token exchange in a thin wrapper script:

```bash
#!/usr/bin/env bash
# mcp-client-wrapper.sh — fetch token once, then forward stdio to SSE via mcp-proxy
TOKEN=$(curl -sX POST http://localhost:8080/token \
  -d "grant_type=client_credentials&client_id=claude-code&client_secret=super-secret-passphrase" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])")
exec mcp-proxy --header "Authorization: Bearer $TOKEN" http://localhost:8080/sse
```

---

## Mode B1 — token introspection (RFC 7662)

Your identity provider issues tokens; the MCP server validates each token by
calling the introspection endpoint. A short in-memory cache (default 60 s)
prevents a round-trip on every request.

### When to use this

- You already run Keycloak, Auth0, Okta, or any other OAuth 2.0 server
- Tokens may be revoked mid-session (introspection always reflects current state)
- You don't want to manage public keys on the MCP server

### Setup

```scheme
(import (curry mcp))

(mcp-auth-mode! 'introspect)

; URL of your IdP's RFC 7662 introspection endpoint.
(mcp-introspection-endpoint! "https://auth.example.com/oauth2/introspect")

; If your introspection endpoint requires client authentication, provide the
; resource-server credentials.  This sends an HTTP Basic header.
(mcp-introspection-credentials! "my-mcp-server" "resource-server-secret")

; Optional: how long to cache introspection results (seconds).
; Longer = faster, but revoked tokens stay valid until the cache expires.
(mcp-introspection-cache-ttl! 60)

(mcp-tool "echo" "Echo a message"
  '((msg . ((type . "string") (description . "Message to echo"))))
  (lambda (args) (mcp-text (cdr (assq 'msg args)))))

(mcp-serve-sse 8080)
```

### Getting a token from your IdP

This is IdP-specific; here is a generic Client Credentials example against Keycloak:

```bash
TOKEN=$(curl -sX POST https://auth.example.com/realms/myrealm/protocol/openid-connect/token \
  -d "grant_type=client_credentials&client_id=my-client&client_secret=my-secret" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])")
```

Then use `Authorization: Bearer $TOKEN` on SSE/message requests as shown in
Mode A §3 above.

### Security note

The MCP server makes an outbound HTTPS POST to the introspection endpoint for
every uncached token. Ensure your introspection endpoint is reachable from where
the MCP server runs, and protect the resource-server secret like any other
credential.

---

## Mode B2 — local JWT validation (RFC 7519)

Tokens are JWTs issued by your identity provider and validated entirely
in-process using OpenSSL. No network call on the hot path.

Two signature algorithms are supported:

| Algorithm | Key type | Use when |
|-----------|----------|----------|
| `hs256` | Shared HMAC secret | You control both client and server; simpler setup |
| `rs256` | RSA public/private key pair | Industry standard; IdP keeps the private key |

### HS256 — shared secret

```scheme
(import (curry mcp))

(mcp-auth-mode! 'jwt)
(mcp-jwt-algorithm! 'hs256)
(mcp-jwt-secret! "at-least-32-bytes-of-random-secret-here")

; Optional claim validation
(mcp-jwt-issuer!   "https://auth.example.com")  ; validates the "iss" claim
(mcp-jwt-audience! "my-mcp-server")             ; validates the "aud" claim

(mcp-serve-sse 8080)
```

JWTs must be signed with HMAC-SHA-256 using the same secret, and must carry an
`exp` claim in the future. Generate a test token with the `pyjwt` CLI:

```bash
python3 -m pip install pyjwt
python3 -c "
import jwt, time
print(jwt.encode(
    {'sub':'test','iss':'https://auth.example.com','aud':'my-mcp-server',
     'exp':int(time.time())+3600},
    'at-least-32-bytes-of-random-secret-here',
    algorithm='HS256'))"
```

### RS256 — RSA public key

This is the mode used by Keycloak, Auth0, Azure AD, and most commercial IdPs.
The MCP server only needs the public key — the private key never leaves the IdP.

**From a PEM file:**

```scheme
(mcp-auth-mode! 'jwt)
(mcp-jwt-algorithm! 'rs256)
(mcp-jwt-public-key! "/etc/mcp/idp-public.pem")  ; path to the PEM public key
(mcp-jwt-issuer!     "https://auth.example.com")
(mcp-serve-sse 8080)
```

**Or inline as a PEM string** (useful when the key is stored in a secret manager):

```scheme
(mcp-auth-mode! 'jwt)
(mcp-jwt-algorithm! 'rs256)
(mcp-jwt-public-key-pem!
  "-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...
-----END PUBLIC KEY-----")
(mcp-serve-sse 8080)
```

**Fetching the public key from an IdP JWKS endpoint** (example with `curl` and
`openssl`):

```bash
# 1. Download the JWKS and extract the RSA modulus + exponent
curl -s https://auth.example.com/.well-known/jwks.json \
  | python3 -c "
import sys, json, base64, struct
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend

jwks = json.load(sys.stdin)
key_data = jwks['keys'][0]  # use the key with the matching 'kid' if there are multiple
n = int.from_bytes(base64.urlsafe_b64decode(key_data['n'] + '=='), 'big')
e = int.from_bytes(base64.urlsafe_b64decode(key_data['e'] + '=='), 'big')
pub = rsa.RSAPublicNumbers(e, n).public_key(default_backend())
print(pub.public_bytes(serialization.Encoding.PEM,
      serialization.PublicFormat.SubjectPublicKeyInfo).decode())
" > /etc/mcp/idp-public.pem
```

---

## Adding auth tests to an existing test script

Here is a self-contained shell snippet that tests Mode A end-to-end:

```bash
#!/usr/bin/env bash
set -euo pipefail
PORT=18765

# Start server
cat > /tmp/auth_test.scm <<'EOF'
(import (curry mcp))
(mcp-auth-mode! 'self-contained)
(mcp-register-client! "test-client" "test-secret")
(mcp-tool "ping" "Ping" '() (lambda (_) (mcp-text "pong")))
(mcp-serve-sse 18765)
EOF
CURRY_MODULE_PATH=/path/to/mods curry /tmp/auth_test.scm &
SRV=$!
sleep 0.4

# Unauthenticated request must return 401
STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:$PORT/sse)
[ "$STATUS" = "401" ] && echo "PASS: no token → 401" || echo "FAIL: expected 401, got $STATUS"

# Get a token
TOKEN=$(curl -sX POST http://localhost:$PORT/token \
  -d "grant_type=client_credentials&client_id=test-client&client_secret=test-secret" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['access_token'])")

# Authenticated SSE request must succeed
curl -s -N http://localhost:$PORT/sse -H "Authorization: Bearer $TOKEN" > /tmp/sse.txt &
CURL=$!
sleep 0.3
grep -q "sessionId=" /tmp/sse.txt && echo "PASS: valid token → SSE stream" || echo "FAIL: no endpoint event"

kill $CURL $SRV 2>/dev/null; wait $CURL $SRV 2>/dev/null || true
rm -f /tmp/auth_test.scm /tmp/sse.txt
```

---

## Choosing a mode

```
                      ┌──────────────────────────────────────────────┐
                      │  Do you have an existing identity provider?   │
                      └──────────────┬───────────────────────────────┘
                                     │
                    No ◄─────────────┴─────────────► Yes
                     │                                │
                     ▼                                ▼
           ┌─────────────────┐         ┌─────────────────────────────────┐
           │  self-contained  │         │  Do you need revocation support? │
           │  (Mode A)        │         └────────────┬────────────────────┘
           └─────────────────┘                       │
                                    Yes ◄────────────┴──────────► No
                                     │                              │
                                     ▼                              ▼
                             ┌──────────────┐              ┌───────────────┐
                             │  introspect   │              │  jwt (rs256)  │
                             │  (Mode B1)    │              │  (Mode B2)    │
                             └──────────────┘              └───────────────┘
```

**Revocation** means a token can be invalidated before its `exp` time. JWT
mode cannot detect revocation (the token is validated locally). If a stolen
JWT needs to be revoked immediately, use introspect mode.

---

## See also

- [MCP module reference](../reference/module-mcp.md) — full procedure signatures
- [MCP server examples](../../examples/) — `mcp_server.scm`, `mcp_math.scm`, `mcp_nbody.scm`
