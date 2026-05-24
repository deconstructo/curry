/*
 * mcp_auth.h — OAuth 2.0 authentication / authorisation for curry_mcp.
 *
 * Three selectable modes (plus the default "none"):
 *
 *   self-contained  — the MCP server itself is the authorisation server.
 *                     Registers clients, issues opaque access tokens via
 *                     POST /token (Client Credentials grant, RFC 6749 §4.4).
 *
 *   introspect      — tokens are issued by an external IdP.  Each incoming
 *                     token is validated by calling an RFC 7662 introspection
 *                     endpoint; results are cached in-process to reduce
 *                     round-trip latency.
 *
 *   jwt             — tokens are JWTs issued by an external IdP and validated
 *                     locally.  Supports HS256 (HMAC-SHA-256 with a shared
 *                     secret) and RS256 (RSA-SHA-256 with a PEM public key).
 *                     Validates signature, expiry, and optionally iss / aud.
 *
 * Requires OpenSSL (used by all three modes for SHA-256, HMAC, RSA, and
 * the HTTPS client used by the introspection mode).
 */

#pragma once

#include <curry.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MCP_AUTH_NONE = 0,         /* no auth — any client may connect (default) */
    MCP_AUTH_SELF_CONTAINED,   /* RFC 6749 §4.4 — server is its own auth server */
    MCP_AUTH_INTROSPECT,       /* RFC 7662  — external IdP, token introspection */
    MCP_AUTH_JWT,              /* RFC 7519  — external IdP, local JWT validation */
} McpAuthMode;

/*
 * Validate an incoming Bearer token against the active mode.
 * bearer_token is the raw token value with the "Bearer " prefix already
 * stripped.  Returns true iff the request is authorised.
 */
bool mcp_auth_validate(const char *bearer_token);

/*
 * Returns true when the self-contained mode is active, so mcp.c knows to
 * route POST /token to mcp_auth_handle_token_endpoint() before the auth check.
 */
bool mcp_auth_has_token_endpoint(void);

/*
 * Handle a POST /token request (self-contained mode only).
 * body is the raw application/x-www-form-urlencoded request body.
 * Writes a complete HTTP/1.1 response — headers and body — to fd.
 */
void mcp_auth_handle_token_endpoint(int fd, const char *body, size_t body_len);

/*
 * Return the value to use in the WWW-Authenticate response header on 401.
 * error and description are RFC 6750 §3 error codes / descriptions; pass
 * NULL for a bare challenge (no active token present yet).
 * The returned pointer is into a thread-local static buffer — copy if needed.
 */
const char *mcp_auth_www_authenticate(const char *error, const char *description);

/*
 * Register all mcp-auth-* Scheme primitives with the VM.
 * Call this from mcp.c's curry_module_init().
 */
void mcp_auth_register(CurryVM *vm);
