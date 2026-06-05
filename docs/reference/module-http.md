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
```

## Notes

- Follows redirects automatically (`CURLOPT_FOLLOWLOCATION`).
- Sends `User-Agent: curry-http/1.0`.
- TLS is handled by the system libcurl (OpenSSL or SecureTransport on macOS).
- The curl global state is initialised once at first call.
