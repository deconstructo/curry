# module: `(curry http)`

General-purpose HTTP client module built on libcurl.

## Build

Requires libcurl. Enabled by default (`BUILD_MODULE_HTTP=ON`).

```bash
# Debian/Ubuntu
sudo apt install libcurl4-openssl-dev

# macOS
brew install curl  # already bundled
```

## API

### `(http-request method url)` → `(status . body)`
### `(http-request method url headers)` → `(status . body)`
### `(http-request method url headers body)` → `(status . body)`

Performs an HTTP request and returns a pair `(status-code . body-string)`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `method`  | string | HTTP method: `"GET"`, `"POST"`, `"PUT"`, `"PATCH"`, `"DELETE"`, etc. |
| `url`     | string | Full URL |
| `headers` | alist | `(("Header-Name" . "value") ...)` pairs, or `'()` |
| `body`    | string | Optional request body (e.g. JSON) |

Returns a pair where `car` is the integer HTTP status code and `cdr` is the
response body as a string. Non-2xx responses are **not** automatically errors —
inspect the status code yourself.

Raises a Scheme error on network failure (DNS, TLS, timeout, etc.).

Header injection is rejected: headers whose name or value contain `CR` or `LF`
raise an error immediately.

### `(http-request/headers method url)` → `(status headers body)`
### `(http-request/headers method url headers)` → `(status headers body)`
### `(http-request/headers method url headers body)` → `(status headers body)`

Same arguments as `http-request`, but returns a 3-element list `(status
headers-alist body)` instead of a `(status . body)` pair. `headers-alist`
holds the *response* headers as `("name" . "value")` pairs with names
lowercased (HTTP header names are case-insensitive), so look one up with
`(assoc "etag" headers)`. Only headers from the final response are kept —
if the request follows a redirect, headers from the intermediate hops are
discarded.

Useful for conditional requests / cache validation: send `If-None-Match` or
`If-Modified-Since` via the `headers` request parameter (as with
`http-request`), then read the `ETag` / `Last-Modified` response headers
back out to store alongside a local cache.

## Examples

```scheme
(import (curry http))

; Simple GET
(define res (http-request "GET" "https://httpbin.org/get"))
(display (car res))    ; status code, e.g. 200
(display (cdr res))    ; raw response body

; POST with JSON body
(define res
  (http-request "POST" "https://httpbin.org/post"
    '(("Content-Type" . "application/json"))
    "{\"hello\":\"world\"}"))

; DELETE with auth header
(define res
  (http-request "DELETE" "https://api.example.com/items/42"
    (list (cons "Authorization" (string-append "Bearer " my-token)))))
(when (not (= (car res) 204))
  (error "delete failed" (car res)))

; Conditional GET using cached validators
(define res
  (http-request/headers "GET" "https://example.com/data.csv"
    (list (cons "If-None-Match" cached-etag))))
(define status  (car res))
(define headers (cadr res))
(define body    (caddr res))
(when (= status 200)
  (set! cached-etag (cdr (assoc "etag" headers))))
```

## Notes

- Follows redirects automatically (`CURLOPT_FOLLOWLOCATION`).
- Sends `User-Agent: curry-http/1.0`.
- TLS is handled by the system libcurl (OpenSSL or SecureTransport on macOS).
- The curl global state is initialised once at first call.
