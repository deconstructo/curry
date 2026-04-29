# Module: (curry crypto)

Cryptographic primitives: base64, MD5, SHA-1, SHA-256, HMAC-SHA-256.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libssl-dev

# macOS (OpenSSL is keg-only; pkg-config will find it via Homebrew)
brew install openssl
```

Enabled by `-DBUILD_MODULE_CRYPTO=ON` (default). Skipped silently if OpenSSL is not found.

MD5 is implemented in pure C (no OpenSSL). SHA-1, SHA-256, and HMAC use the OpenSSL EVP API.

## Import

```scheme
(import (curry crypto))
```

## Procedures

### Base64

```scheme
(base64-encode bytevector)   ; → string
(base64-decode string)       ; → bytevector
```

Standard RFC 4648 base64 with `=` padding.

### MD5

```scheme
(md5     bytevector)         ; → bytevector (16 bytes)
(md5-hex bytevector)         ; → string (32 lowercase hex chars)
```

MD5 is cryptographically broken; use it only for checksums and legacy interoperability.

### SHA-1

```scheme
(sha1     bytevector)        ; → bytevector (20 bytes)
(sha1-hex bytevector)        ; → string (40 lowercase hex chars)
```

SHA-1 is deprecated for security purposes. Prefer SHA-256.

### SHA-256

```scheme
(sha256     bytevector)      ; → bytevector (32 bytes)
(sha256-hex bytevector)      ; → string (64 lowercase hex chars)
```

### HMAC-SHA-256

```scheme
(hmac-sha256 key-bytevector message-bytevector)  ; → bytevector (32 bytes)
```

Used for AWS Signature V4, webhook verification, JWT signatures, etc.

### String encoding

```scheme
(string->utf8 string)        ; → bytevector (UTF-8 encoded)
```

Converts a Scheme string to its UTF-8 byte representation. Use this before hashing strings.

## Examples

```scheme
(import (curry crypto))

; Hash a string
(sha256-hex (string->utf8 "hello world"))
; => "b94d27b9934d3e08a52e52d7da7dabfac484efe04294e576f4aee9d856e48616"

; HMAC for webhook verification
(define secret (string->utf8 "my-secret-key"))
(define payload (string->utf8 "{\"event\":\"push\"}"))
(define sig (hmac-sha256 secret payload))
(base64-encode sig)   ; → base64-encoded HMAC

; AWS-style signing chain
(define (hmac-sha256-key k m)
  (hmac-sha256 k (string->utf8 m)))

(define date-key    (hmac-sha256-key (string->utf8 "AWS4secret") "20240101"))
(define region-key  (hmac-sha256-key date-key   "us-east-1"))
(define service-key (hmac-sha256-key region-key "s3"))
(define signing-key (hmac-sha256-key service-key "aws4_request"))

; Base64 encoding
(base64-encode (string->utf8 "user:password"))
; => "dXNlcjpwYXNzd29yZA=="

(define decoded (base64-decode "dXNlcjpwYXNzd29yZA=="))
; bytevector — convert back: (utf8->string decoded)
```

## Notes

- All hash functions accept bytevectors, not strings. Use `string->utf8` to convert.
- `hmac-sha256` returns raw bytes (bytevector). Use `base64-encode` for HTTP headers or `sha256-hex` style — but note `hmac-sha256` has no `-hex` variant; use `(list->string (map (lambda (b) (string-ref "0123456789abcdef" (quotient b 16))) (bytevector->list sig)))` or write a simple hex converter.
- The storage module (`(curry storage)`) uses these internally for AWS and Azure authentication.
