# Module: `(curry s3)`

*unreleased*

Amazon S3 client, pure Scheme — no new C code. Also works against any S3-compatible endpoint: Cloudflare R2, MinIO, Ceph, or Google Cloud Storage via its S3 interoperability mode. Authentication uses AWS Signature Version 4 (HMAC-SHA256 signing chain), implemented directly against `(curry crypto)`'s `hmac-sha256`/`sha256-hex`.

This replaces `(curry storage)`'s old C-level `s3-client`/`s3-put!`/`s3-get`/`s3-delete!` (removed — see [`module-storage.md`](module-storage.md)) with a larger API surface: bucket listing, object metadata (HEAD), server-side copy, bucket create/delete, presigned URLs, and multipart upload for large objects. `(curry storage)` keeps its OpenStack Swift and Azure Blob Storage support unchanged.

Built on `(curry crypto)`, `(curry http)`, `(curry xml)`, `(srfi 19)`, and `(srfi 132)` — all either always-on or default-on modules; no extra `BUILD_MODULE_*` flag is needed.

## Import

```scheme
(import (curry s3))
```

## Client

```scheme
(s3-client access-key secret-key region)            ; -> <s3-client>
(s3-client access-key secret-key region endpoint)    ; custom endpoint
(s3-client? obj)                                     ; -> boolean
```

`endpoint` defaults to `https://s3.<region>.amazonaws.com` (path-style: bucket and key both go in the URL path, not a virtual-hosted subdomain — this is what makes the same client code work unmodified against MinIO/Ceph, which usually don't support virtual-hosted style). Pass a full `https://` or `http://` URL to override; the scheme is remembered and reused for every request from that client.

```scheme
(define s3  (s3-client "AKIAIOSFODNN7EXAMPLE" "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY" "us-east-1"))
(define r2  (s3-client "r2-key" "r2-secret" "auto" "https://<account-id>.r2.cloudflarestorage.com"))
(define gcs (s3-client "GOOGHMAC..." "secret..." "auto" "https://storage.googleapis.com"))
(define minio (s3-client "minioadmin" "minioadmin" "us-east-1" "http://localhost:9000"))
(define wasabi (s3-client "wasabi-key" "wasabi-secret" "us-east-1" "https://s3.wasabisys.com"))
```

Wasabi is fully S3-API-compatible (path-style addressing, SigV4, `ListObjectsV2`, multipart) — no special-casing needed, just the right region/endpoint pair. `region` must match whatever Wasabi expects in its signing scope for the endpoint you use: either `us-east-1` with the default `s3.wasabisys.com` endpoint, or the matching region (e.g. `eu-central-1`) with that region's own endpoint (`s3.eu-central-1.wasabisys.com`) — check Wasabi's own current region/endpoint list, since a mismatched pair fails signature verification even though both values look individually valid.

## Objects

```scheme
(s3-put! client bucket key data)                  ; -> #t
(s3-put! client bucket key data content-type)     ; content-type defaults to "application/octet-stream"
(s3-get client bucket key)                        ; -> bytevector, or #f on 404
(s3-delete! client bucket key)                    ; -> #t (also #t on 404 -- already gone)
(s3-head client bucket key)                       ; -> alist, or #f on 404
(s3-copy! client bucket key src-bucket src-key)   ; -> #t (server-side copy)
```

`data` may be a string or a bytevector. Passed straight through to `(curry http)` as whichever type it already is — binary payloads don't get forced through a UTF-8-validating string conversion (see `modules/http/http.c`'s `resolve_body`).

`s3-head` returns an alist: `(content-length content-type etag last-modified)`, each a `(key . value)` pair (value `#f` if the response didn't include that header).

```scheme
(import (curry s3))
(define s3 (s3-client "AKIAIOSFODNN7EXAMPLE" "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY" "us-east-1"))

(s3-put! s3 "my-bucket" "data/hello.txt" "Hello, world!" "text/plain")
(display (utf8->string (s3-get s3 "my-bucket" "data/hello.txt")))
; => "Hello, world!"

(s3-head s3 "my-bucket" "data/hello.txt")
; => ((content-length . 13) (content-type . "text/plain") (etag . "\"...\"") (last-modified . "..."))

(s3-copy! s3 "my-bucket" "data/hello-copy.txt" "my-bucket" "data/hello.txt")
(s3-delete! s3 "my-bucket" "data/hello.txt")
```

## Listing

```scheme
(s3-list client bucket)                                              ; -> <s3-list-result>
(s3-list client bucket prefix)
(s3-list client bucket prefix delimiter)
(s3-list client bucket prefix delimiter continuation-token)
(s3-list client bucket prefix delimiter continuation-token max-keys)
```

Positional trailing optionals — R7RS has no keyword arguments. Pass `#f` to skip an earlier one and still set a later one: `(s3-list s3 "my-bucket" #f "/" token)`. Backed by `ListObjectsV2`.

```scheme
(s3-list-objects result)         ; -> list of <s3-object>
(s3-list-common-prefixes result) ; -> list of strings
(s3-list-truncated? result)      ; -> boolean
(s3-list-next-token result)      ; -> string or #f -- pass to the next call's continuation-token
```

`<s3-object>` accessors: `s3-object-key`, `s3-object-last-modified`, `s3-object-etag`, `s3-object-size`, `s3-object-storage-class`.

```scheme
(define result (s3-list s3 "my-bucket" "photos/" "/"))
(for-each (lambda (o) (display (s3-object-key o)) (newline))
          (s3-list-objects result))
(when (s3-list-truncated? result)
  (s3-list s3 "my-bucket" "photos/" "/" (s3-list-next-token result)))
```

## Buckets

```scheme
(s3-bucket-create! client bucket)   ; -> #t
(s3-bucket-delete! client bucket)   ; -> #t
```

## Presigned URLs

```scheme
(s3-presign client bucket key seconds)          ; -> url string, method defaults to "GET"
(s3-presign client bucket key seconds method)
```

Query-string SigV4: the signature is embedded in the URL itself rather than an `Authorization` header, so the URL alone grants temporary access — hand it to something that doesn't have your credentials (a browser, a curl command, another service) and it expires after `seconds`. Per AWS's own spec for this flavor, the payload is always treated as `UNSIGNED-PAYLOAD` — the eventual requester supplies the body, which the signer here never sees.

```scheme
(define url (s3-presign s3 "my-bucket" "reports/q3.pdf" 3600))
; => "https://s3.us-east-1.amazonaws.com/my-bucket/reports/q3.pdf?X-Amz-Algorithm=...&X-Amz-Signature=..."
```

## Multipart upload

```scheme
(s3-put-large! client bucket key data)                          ; -> #t
(s3-put-large! client bucket key data content-type)
(s3-put-large! client bucket key data content-type part-size)   ; part-size in bytes, default 8 MiB
```

Automatic: if `data` fits in one part it's just an `s3-put!`; otherwise it creates a multipart upload, splits `data` into `part-size` chunks (S3 requires every part but the last to be ≥ 5 MiB — the default comfortably clears that), uploads each part, and completes the upload. If any part fails, the in-progress upload is aborted (`s3-multipart-abort!`) before the error propagates, rather than leaving an incomplete upload accumulating storage charges on the bucket.

```scheme
(s3-multipart-create! client bucket key)                     ; -> upload-id
(s3-multipart-create! client bucket key content-type)
(s3-multipart-upload-part! client bucket key upload-id part-number data)  ; -> etag string
(s3-multipart-complete! client bucket key upload-id parts)   ; parts: list of (part-number . etag)
(s3-multipart-abort! client bucket key upload-id)            ; -> #t
```

The lower-level primitives are exported directly for callers that want to stream parts from somewhere `s3-put-large!`'s "whole `data` already in memory" model doesn't fit — e.g. reading a large file in chunks rather than loading it whole first.

```scheme
(define upload-id (s3-multipart-create! s3 "my-bucket" "big.tar.gz" "application/gzip"))
(define etag1 (s3-multipart-upload-part! s3 "my-bucket" "big.tar.gz" upload-id 1 chunk1))
(define etag2 (s3-multipart-upload-part! s3 "my-bucket" "big.tar.gz" upload-id 2 chunk2))
(s3-multipart-complete! s3 "my-bucket" "big.tar.gz" upload-id
  (list (cons 1 etag1) (cons 2 etag2)))
```

## Errors

Any non-2xx response raises via `(error message code status s3-message)`. `(error-object-irritants e)` gives `(list s3-error-code http-status s3-message-text)`, so callers that need to distinguish e.g. `"NoSuchKey"` from `"AccessDenied"` can pattern-match on `(car (error-object-irritants e))` rather than parsing the message string.

`s3-get` is the one exception: it returns `#f` (not an error) specifically for a 404/`NoSuchKey`, matching R7RS association-style "not found" conventions elsewhere in curry (e.g. `hash-table-ref/default`). `s3-delete!` also treats a 404 as success (`#t`) — the object is gone either way. Every other operation raises on any non-2xx.

```scheme
(guard (e (#t (display (car (error-object-irritants e))) (newline)))
  (s3-put! s3 "does-not-exist-bucket" "key" "data"))
; => NoSuchBucket
```

## Notes

- Path-style addressing throughout (bucket and key in the URL path) — not virtual-hosted style (`bucket.s3.amazonaws.com`). Works against AWS as well as any S3-compatible store; there was no reason to special-case AWS onto a different addressing style than everything else this module supports.
- URI encoding follows AWS's own SigV4 canonicalization rules exactly: unreserved characters are strictly ASCII `A-Za-z0-9-_.~` (not the Unicode-aware `char-alphabetic?`/`char-numeric?` — those would leave e.g. accented filenames incorrectly unescaped), percent-encoding uses uppercase hex digits, and the SigV4 signature itself uses lowercase hex — two different, non-interchangeable hex conventions in the same request.
- All transfers are synchronous (blocking) and unbuffered — a `s3-get` or `s3-put!` call fully materializes the object in memory (via `(curry http)`), same as `(curry storage)`. There's no streaming request/response body support at either the HTTP or S3 layer. Use actors for concurrent transfers.
- The SigV4 signing algorithm (canonical request construction, string-to-sign, HMAC signing-key derivation chain) was cross-checked during development against an independent reference implementation for both the `Authorization`-header and query-string (presign) signing flavors, with byte-for-byte matching signatures.

## See also

- [`module-storage.md`](module-storage.md) — OpenStack Swift and Azure Blob Storage (S3 used to live here too)
- [`module-http.md`](module-http.md) — the HTTP client this module is built on
- [`module-crypto.md`](module-crypto.md) — `hmac-sha256`/`sha256-hex`, including its own worked SigV4 signing-key example
- [`(curry xml)`](module-xml.md) — the XML reader/writer this module's list/error/multipart parsing is built on
