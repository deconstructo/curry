# Module: (curry storage)

Object storage: AWS S3, OpenStack Swift, and Azure Blob Storage. GCS is supported via S3 interoperability mode.

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

## AWS S3

Supports AWS S3, Cloudflare R2, MinIO, Ceph, and any S3-compatible endpoint. Authentication uses AWS Signature Version 4 (HMAC-SHA256 signing chain).

```scheme
(s3-client access-key secret-key region)
(s3-client access-key secret-key region endpoint)   ; custom endpoint
```

- `endpoint` defaults to `https://s3.amazonaws.com`. Override for non-AWS services.
- Returns an opaque client handle.

```scheme
(s3-put! client bucket key data)
(s3-put! client bucket key data content-type)   ; optional content-type string
```

Upload `data` (string or bytevector) to `bucket/key`. Returns `#t` on success.

```scheme
(s3-get client bucket key)     ; → string (body) or #f on error
(s3-delete! client bucket key) ; → #t on success
```

### AWS examples

```scheme
(import (curry storage))

(define s3 (s3-client "AKIAIOSFODNN7EXAMPLE"
                      "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"
                      "us-east-1"))

(s3-put! s3 "my-bucket" "data/hello.txt" "Hello, world!" "text/plain")

(display (s3-get s3 "my-bucket" "data/hello.txt"))
; => "Hello, world!"

(s3-delete! s3 "my-bucket" "data/hello.txt")
```

### Cloudflare R2

```scheme
(define r2 (s3-client "r2-access-key" "r2-secret-key" "auto"
                      "https://<account-id>.r2.cloudflarestorage.com"))
```

### MinIO / Ceph

```scheme
(define minio (s3-client "minioadmin" "minioadmin" "us-east-1"
                         "http://localhost:9000"))
```

### Google Cloud Storage (S3 interoperability)

Enable HMAC keys in GCS, then:

```scheme
(define gcs (s3-client "GOOGHMAC..." "secret..." "auto"
                       "https://storage.googleapis.com"))
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
- Multipart upload for large objects (>5 GB for S3) is not implemented; split large data manually.
- The signing logic for S3 (AWS Sig V4) is implemented in pure C without depending on any AWS SDK.
