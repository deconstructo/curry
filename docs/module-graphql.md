# Module: (curry graphql)

GraphQL client over HTTP/HTTPS. Queries and mutations share the same wire protocol (JSON POST); subscriptions are not supported.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libcurl4-openssl-dev

# macOS
brew install curl
```

Enable: `-DBUILD_MODULE_GRAPHQL=ON` (off by default).

## Import

```scheme
(import (curry graphql))
```

## Procedures

```scheme
(graphql-client url)
(graphql-client url headers-alist)     ; optional extra HTTP headers
```

Create a client pointing at a GraphQL endpoint. `headers-alist` is a list of `("Header-Name" . "value")` pairs — useful for `Authorization`, `X-API-Key`, etc.

```scheme
(graphql-query  client query)
(graphql-query  client query variables)

(graphql-mutate client mutation)
(graphql-mutate client mutation variables)
```

`query` / `mutation` is a GraphQL document string. `variables` (optional) is an association list that maps variable names (strings) to their values. Returns the parsed response data as a Scheme alist, or raises an error if the server returns errors.

The JSON ↔ Scheme mapping follows the same rules as `(curry json)`:

| JSON | Scheme |
|------|--------|
| object | association list |
| array | list |
| string | string |
| number | fixnum or flonum |
| `true`/`false` | `#t`/`#f` |
| `null` | `#f` |

## Examples

### Public API (no auth)

```scheme
(import (curry graphql))

(define client (graphql-client "https://countries.trevorblades.com/"))

(define result
  (graphql-query client
    "{ countries { code name } }"))

; result is (("countries" . (("code" . "AD") ("name" . "Andorra")) ...))
(define countries (cdr (assoc "countries" result)))
(for-each
  (lambda (c)
    (display (cdr (assoc "code" c)))
    (display " ")
    (display (cdr (assoc "name" c)))
    (newline))
  countries)
```

### Authenticated query

```scheme
(import (curry graphql))

(define client
  (graphql-client "https://api.github.com/graphql"
    '(("Authorization" . "Bearer ghp_your_token_here")
      ("User-Agent"    . "curry-scheme"))))

(define result
  (graphql-query client
    "query ($login: String!) {
       user(login: $login) {
         name
         bio
         repositories(first: 5) {
           nodes { name stargazerCount }
         }
       }
     }"
    '(("login" . "octocat"))))

(define user (cdr (assoc "user" result)))
(display (cdr (assoc "name" user))) (newline)
```

### Mutation

```scheme
(graphql-mutate client
  "mutation ($input: CreateIssueInput!) {
     createIssue(input: $input) {
       issue { number url }
     }
   }"
  '(("input" . (("repositoryId" . "R_kgDO...")
                ("title"        . "Bug: crash on startup")
                ("body"         . "Steps to reproduce...")))))
```

### Introspection

```scheme
(graphql-query client
  "{ __schema { types { name kind } } }")
```

## Notes

- Variables must be an alist. Nested objects are alists of alists; arrays are lists. The module serialises these to JSON automatically.
- Errors returned in the GraphQL `errors` array cause `graphql-query` to raise an error with the first error message.
- HTTPS works automatically via libcurl's built-in TLS. Certificate verification uses the system trust store.
- Subscriptions (WebSocket-based) are not implemented. Use a dedicated WebSocket library or actor + raw socket for streaming APIs.
- The response `data` field is unwrapped automatically — you receive the value of `data` directly, not `{"data": {...}}`.
