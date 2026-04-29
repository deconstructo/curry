# Module: (curry ldap)

LDAP and LDAPS directory access.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libldap-dev

# macOS
brew install openldap
```

Enabled by `-DBUILD_MODULE_LDAP=ON` (default).

## Import

```scheme
(import (curry ldap))
```

## Procedures

### Connection

```scheme
(ldap-connect host port use-tls?)
```

Opens a connection to an LDAP server. `use-tls?` (`#t`/`#f`) selects LDAPS (port 636) or plain LDAP (port 389). Returns an opaque connection handle.

```scheme
(ldap-bind! conn bind-dn password)
```

Authenticates the connection using simple bind. `bind-dn` is the distinguished name of the account (e.g., `"cn=admin,dc=example,dc=com"`). Returns `#t` on success.

```scheme
(ldap-close! conn)
```

Closes and frees the connection.

### Options

```scheme
(ldap-set-option! conn option value)
```

Set a protocol option before binding. Common options:

| Symbol | LDAP constant | Typical value |
|--------|---------------|---------------|
| `'protocol-version` | `LDAP_OPT_PROTOCOL_VERSION` | `3` |
| `'timelimit` | `LDAP_OPT_TIMELIMIT` | seconds |
| `'sizelimit` | `LDAP_OPT_SIZELIMIT` | max entries |

### Search

```scheme
(ldap-search conn base-dn scope filter attrs)
```

Parameters:
- `base-dn` — search base (string)
- `scope` — `'base` (single entry), `'one` (one level), `'sub` (subtree)
- `filter` — LDAP filter string (e.g., `"(objectClass=person)"`)
- `attrs` — list of attribute name strings to retrieve, or `'()` for all

Returns a list of entries, each of the form:

```
(dn . ((attr-name . value) ...))
```

where `dn` is the distinguished name string and each attribute is a pair of name→value strings.

## Examples

```scheme
(import (curry ldap))

; Connect to a local OpenLDAP server
(define conn (ldap-connect "ldap.example.com" 389 #f))
(ldap-set-option! conn 'protocol-version 3)
(ldap-bind! conn "cn=admin,dc=example,dc=com" "secret")

; Find all users
(define results
  (ldap-search conn
    "ou=users,dc=example,dc=com"
    'sub
    "(objectClass=inetOrgPerson)"
    '("cn" "mail" "uid")))

(for-each
  (lambda (entry)
    (display (car entry)) (newline)    ; DN
    (for-each
      (lambda (attr)
        (display "  ") (display (car attr))
        (display ": ") (display (cdr attr)) (newline))
      (cdr entry)))
  results)

(ldap-close! conn)
```

### LDAPS (TLS)

```scheme
(define conn (ldap-connect "ldap.example.com" 636 #t))
```

### Authenticate a user (check password)

```scheme
; Try to bind as the user — success means credentials are valid
(define (authenticate? conn user-dn password)
  (guard (exn (#t #f))
    (ldap-bind! conn user-dn password)))
```

## Notes

- Multiple simultaneous connections are supported — create one handle per connection.
- `ldap-search` returns results synchronously (blocking).
- Multi-valued attributes: the current implementation returns only the first value of each attribute. To retrieve all values, use `ldap-search` with the raw attribute list and parse using `ldap-get-values` — expose this if needed via a C extension patch.
- Certificate verification for LDAPS follows the system's OpenSSL trust store. To disable verification (for development): `(ldap-set-option! conn 'tls-reqcert 0)` — not recommended in production.
