;;; S3 module tests — (curry s3)
;;;
;;; No live network access by default: SigV4 signing is validated
;;; offline (structure/format of presigned URLs, which is fully
;;; deterministic apart from the timestamp/signature values
;;; themselves), and the XML shapes (curry s3)'s list/error parsing
;;; depends on are checked directly against (curry xml) using
;;; representative ListBucketResult/Error fixtures -- the same
;;; xml-find/xml-find-all/xml-element-text calls s3.scm itself makes.
;;;
;;; The actual SigV4 algorithm (canonical request, string-to-sign,
;;; HMAC signing-key chain) was cross-checked during development
;;; against an independent Python reference implementation for both
;;; the Authorization-header and query-string (presign) signing
;;; flavors, with byte-for-byte matching signatures -- not repeated
;;; here since it requires patching %sign's private %now-strings to a
;;; fixed date, which isn't something a public API test should depend
;;; on. See git history for that verification.
;;;
;;; An optional live round-trip runs only if CURRY_S3_TEST_ENDPOINT,
;;; CURRY_S3_TEST_BUCKET, CURRY_S3_TEST_ACCESS_KEY, and
;;; CURRY_S3_TEST_SECRET_KEY are all set in the environment (e.g.
;;; against a local MinIO) -- skipped cleanly otherwise, same
;;; "skip if the real service isn't there" convention as
;;; mariadb_tests.scm/postgres_tests.scm/redis's own test script.

(import (curry s3))
(import (curry xml))
(import (scheme base) (scheme write) (scheme char))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-true label result)
  (check label (if result #t #f) #t))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

;;; ── client construction ─────────────────────────────────────────────

(define c-default (s3-client "AKID" "secret" "us-east-1"))
(check "s3-client? true for a real client" (s3-client? c-default) #t)
(check "s3-client? false for a non-client" (s3-client? "not a client") #f)

;;; ── presigned URL structure (deterministic parts only) ──────────────

;; core curry has no string-contains?/string-prefix? -- tiny local ones
(define (string-prefix-check s prefix)
  (and (>= (string-length s) (string-length prefix))
       (string=? (substring s 0 (string-length prefix)) prefix)))

(define (string-contains-check s needle)
  (let ((slen (string-length s)) (nlen (string-length needle)))
    (let loop ((i 0))
      (cond
        ((> (+ i nlen) slen) #f)
        ((string=? (substring s i (+ i nlen)) needle) #t)
        (else (loop (+ i 1)))))))

(define url (s3-presign c-default "my-bucket" "my-key.txt" 3600))
(check-true "presign URL uses default AWS host for the region"
  (string-prefix-check url "https://s3.us-east-1.amazonaws.com/my-bucket/my-key.txt?"))
(check-true "presign URL includes X-Amz-Algorithm" (string-contains-check url "X-Amz-Algorithm=AWS4-HMAC-SHA256"))
(check-true "presign URL includes X-Amz-Credential with access key" (string-contains-check url "X-Amz-Credential=AKID%2F"))
(check-true "presign URL includes X-Amz-Expires=3600" (string-contains-check url "X-Amz-Expires=3600"))
(check-true "presign URL includes X-Amz-SignedHeaders=host" (string-contains-check url "X-Amz-SignedHeaders=host"))
(check-true "presign URL includes X-Amz-Signature" (string-contains-check url "X-Amz-Signature="))

;; Custom (non-AWS) endpoint: scheme stripped, http:// remembered
(define c-minio (s3-client "k" "s" "us-east-1" "http://localhost:9000"))
(define minio-url (s3-presign c-minio "b" "k" 60))
(check-true "presign against a custom http:// endpoint keeps plain http"
  (string-prefix-check minio-url "http://localhost:9000/b/k?"))

;; https:// custom endpoint (e.g. Cloudflare R2)
(define c-r2 (s3-client "k" "s" "auto" "https://abc123.r2.cloudflarestorage.com"))
(define r2-url (s3-presign c-r2 "b" "k" 60))
(check-true "presign against a custom https:// endpoint keeps https"
  (string-prefix-check r2-url "https://abc123.r2.cloudflarestorage.com/b/k?"))

;; Key/bucket URI encoding: space -> %20, slash preserved as path separator,
;; non-ASCII -> percent-encoded UTF-8 bytes with uppercase hex digits
(define enc-url (s3-presign c-default "my-bucket" "a folder/caf\xE9;.txt" 60))
(check-true "space in key percent-encoded as %20" (string-contains-check enc-url "a%20folder/"))
(check-true "slash in key preserved as path separator, not encoded"
  (string-contains-check enc-url "/my-bucket/a%20folder/"))
(check-true "non-ASCII byte percent-encoded with uppercase hex"
  (string-contains-check enc-url "%C3%A9"))

;; Signature is 64 lowercase hex characters -- SigV4 requires lowercase;
;; percent-encoding (checked above) requires uppercase, so these two
;; requirements only look consistent if the code keeps them separate.
(define (extract-signature url)
  (let* ((slen (string-length url))
         (marker "X-Amz-Signature=")
         (mlen (string-length marker)))
    (let loop ((i 0))
      (cond
        ((> (+ i mlen) slen) #f)
        ((string=? (substring url i (+ i mlen)) marker)
         (substring url (+ i mlen) slen))
        (else (loop (+ i 1)))))))

(define sig (extract-signature url))
(check "extracted a signature" (and sig (> (string-length sig) 0)) #t)
(check "signature is exactly 64 hex characters" (and sig (string-length sig)) 64)
(check-true "signature contains only lowercase hex digits"
  (and sig (let loop ((i 0))
             (or (= i (string-length sig))
                 (and (or (char-numeric? (string-ref sig i))
                          (memv (string-ref sig i) '(#\a #\b #\c #\d #\e #\f)))
                      (loop (+ i 1)))))))

;;; ── XML fixtures: same shapes (curry s3)'s parsing relies on ────────

(define list-bucket-xml
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">
  <Name>my-bucket</Name>
  <Prefix>photos/</Prefix>
  <KeyCount>2</KeyCount>
  <MaxKeys>1000</MaxKeys>
  <IsTruncated>true</IsTruncated>
  <NextContinuationToken>abc123token</NextContinuationToken>
  <Contents>
    <Key>photos/1.jpg</Key>
    <LastModified>2024-01-01T00:00:00.000Z</LastModified>
    <ETag>&quot;9bb58f26192e4ba00f01e2e7b136bbd8&quot;</ETag>
    <Size>12345</Size>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
  <Contents>
    <Key>photos/2.jpg</Key>
    <LastModified>2024-01-02T00:00:00.000Z</LastModified>
    <ETag>&quot;abcdef0123456789abcdef0123456789&quot;</ETag>
    <Size>67890</Size>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
  <CommonPrefixes>
    <Prefix>photos/2024/</Prefix>
  </CommonPrefixes>
</ListBucketResult>")

(define lb-root (xml-parse list-bucket-xml))
(check "ListBucketResult root tag" (xml-tag lb-root) 'ListBucketResult)
(check "IsTruncated text" (xml-element-text (xml-find lb-root 'IsTruncated)) "true")
(check "NextContinuationToken text" (xml-element-text (xml-find lb-root 'NextContinuationToken)) "abc123token")

(define contents (xml-find-all lb-root 'Contents))
(check "two Contents entries found" (length contents) 2)
(check "first Contents Key" (xml-element-text (xml-find (car contents) 'Key)) "photos/1.jpg")
(check "first Contents Size" (xml-element-text (xml-find (car contents) 'Size)) "12345")
(check "first Contents ETag (entity-decoded quotes)"
  (xml-element-text (xml-find (car contents) 'ETag)) "\"9bb58f26192e4ba00f01e2e7b136bbd8\"")

(define common-prefixes (xml-find-all lb-root 'CommonPrefixes))
(check "one CommonPrefixes entry found" (length common-prefixes) 1)
(check "CommonPrefixes Prefix text"
  (xml-element-text (xml-find (car common-prefixes) 'Prefix)) "photos/2024/")

(define error-xml
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<Error>
  <Code>NoSuchKey</Code>
  <Message>The specified key does not exist.</Message>
  <Key>missing.txt</Key>
  <RequestId>4442587FB7D0A2F9</RequestId>
</Error>")

(define err-root (xml-parse error-xml))
(check "Error root tag" (xml-tag err-root) 'Error)
(check "Error Code text" (xml-element-text (xml-find err-root 'Code)) "NoSuchKey")
(check "Error Message text" (xml-element-text (xml-find err-root 'Message)) "The specified key does not exist.")

;; CompleteMultipartUpload request body shape -- (curry s3) builds this
;; on write, via the same xml-element/make-xml-element/xml-stringify
;; (curry xml) primitives exercised directly here.
(define complete-body
  (xml-stringify
    (make-xml-element 'CompleteMultipartUpload '()
      (list (xml-element 'Part (xml-element 'PartNumber "1") (xml-element 'ETag "\"etag1\""))
            (xml-element 'Part (xml-element 'PartNumber "2") (xml-element 'ETag "\"etag2\""))))))
(define complete-root (xml-parse complete-body))
(check "CompleteMultipartUpload round-trips through xml-parse" (xml-tag complete-root) 'CompleteMultipartUpload)
(define complete-parts (xml-find-all complete-root 'Part))
(check "two Part entries in CompleteMultipartUpload body" (length complete-parts) 2)
(check "first Part's PartNumber" (xml-element-text (xml-find (car complete-parts) 'PartNumber)) "1")
(check "first Part's ETag keeps literal quotes"
  (xml-element-text (xml-find (car complete-parts) 'ETag)) "\"etag1\"")

;; CreateBucketConfiguration/LocationConstraint body shape -- what
;; s3-bucket-create! sends for every region other than us-east-1.
(define bucket-config-body
  (xml-stringify
    (make-xml-element 'CreateBucketConfiguration '()
      (list (xml-element 'LocationConstraint "eu-west-1")))))
(define bucket-config-root (xml-parse bucket-config-body))
(check "CreateBucketConfiguration round-trips through xml-parse"
  (xml-tag bucket-config-root) 'CreateBucketConfiguration)
(check "LocationConstraint text"
  (xml-element-text (xml-find bucket-config-root 'LocationConstraint)) "eu-west-1")

;;; ── optional live round-trip against a real (or MinIO-compatible) S3 ──

(let ((endpoint (get-environment-variable "CURRY_S3_TEST_ENDPOINT"))
      (bucket   (get-environment-variable "CURRY_S3_TEST_BUCKET"))
      (akey     (get-environment-variable "CURRY_S3_TEST_ACCESS_KEY"))
      (skey     (get-environment-variable "CURRY_S3_TEST_SECRET_KEY")))
  (if (and endpoint bucket akey skey)
      (let* ((region (or (get-environment-variable "CURRY_S3_TEST_REGION") "us-east-1"))
             (client (s3-client akey skey region endpoint))
             (key "curry-s3-tests/roundtrip.txt")
             (content "hello from (curry s3) tests"))
        (check "live: put!" (s3-put! client bucket key content "text/plain") #t)
        (check "live: get returns the same bytes"
          (utf8->string (s3-get client bucket key)) content)
        (let ((head (s3-head client bucket key)))
          (check-true "live: head reports a content-length" (assoc 'content-length head)))
        (let ((listed (s3-list client bucket "curry-s3-tests/")))
          (check-true "live: list finds the uploaded key"
            (memv #t (map (lambda (o) (equal? (s3-object-key o) key)) (s3-list-objects listed)))))
        (check "live: delete!" (s3-delete! client bucket key) #t)
        (check "live: get after delete returns #f" (s3-get client bucket key) #f))
      (begin
        (display "SKIP: live S3 round-trip (set CURRY_S3_TEST_ENDPOINT/BUCKET/ACCESS_KEY/SECRET_KEY to enable)")
        (newline))))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
