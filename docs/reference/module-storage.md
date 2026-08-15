# Module: (curry storage)

*v1.2.2 — 2026-06-07 · S3 moved out to `(curry s3)` — see below*

Object storage: OpenStack Swift and Azure Blob Storage.

**S3 (and every S3-compatible endpoint — Cloudflare R2, MinIO, Ceph, GCS via S3 interoperability) moved to the pure-Scheme [`(curry s3)`](module-s3.md) module.** This C module no longer implements S3 at all; `s3-client`/`s3-put!`/`s3-get`/`s3-delete!` are not bound here. If you're looking for those, see [`module-s3.md`](module-s3.md) — the replacement has a larger API surface (bucket listing, HEAD, copy, presigned URLs, multipart upload) built without any new C code.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libcurl4-openssl-dev libssl-dev

# macOS
brew install curl openssl
```

Enable: `-DBUILD_MODULE_STORAGE=ON` (off by default).

## Import

```scheme
(import (curry storage))
```

## OpenStack Swift

```scheme
(swift-client auth-url tenant username password)
(swift-client auth-url tenant username password endpoint-override)
```

Authenticates with Keystone v3 (token-based). Returns a client handle.

```scheme
(swift-put! client container object data)
(swift-put! client container object data content-type)
(swift-get  client container object)    ; → string or #f
```

### Swift example

```scheme
(import (curry storage))

(define swift (swift-client "https://keystone.example.com/v3"
                            "my-project"
                            "alice"
                            "password"))

(swift-put! swift "backups" "db-2024-01-01.sql.gz" backup-data "application/gzip")
(define data (swift-get swift "backups" "db-2024-01-01.sql.gz"))
```

## Azure Blob Storage

```scheme
(azure-client account-name account-key)
```

Authenticates using Shared Key (HMAC-SHA256 of the canonicalized request). Returns a client handle.

```scheme
(azure-put! client container blob data)
(azure-put! client container blob data content-type)
(azure-get     client container blob)   ; → string or #f
(azure-delete! client container blob)  ; → #t
```

### Azure example

```scheme
(import (curry storage))

(define az (azure-client "mystorageaccount"
                         "base64-encoded-account-key=="))

(azure-put! az "simulation-results" "run-001.json"
            (json-stringify results) "application/json")
```

## Notes

- All transfers are synchronous (blocking). Use actors for concurrent uploads.
- Data can be a string (treated as UTF-8 bytes) or a bytevector.
- Error handling: on HTTP errors the procedure returns `#f`. For detailed errors, check stderr output from libcurl (set `CURLOPT_VERBOSE` by patching the C source if needed).
- For S3, Cloudflare R2, MinIO, Ceph, or GCS-via-S3-interop, see [`(curry s3)`](module-s3.md) instead.
