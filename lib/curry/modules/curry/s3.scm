;;; (curry s3) — Amazon S3 client, pure Scheme.
;;;
;;; Also works against any S3-compatible endpoint: Cloudflare R2, MinIO,
;;; Ceph, or Google Cloud Storage via its S3 interoperability mode. This
;;; replaces the C (curry storage) module's old s3-client/s3-put!/s3-get/
;;; s3-delete! (removed — see modules/storage/storage.c's own header
;;; comment) with a fuller-featured client: bucket listing, object
;;; metadata (HEAD), copy, bucket create/delete, presigned URLs, and
;;; multipart upload for large objects. (curry storage) keeps its
;;; OpenStack Swift and Azure Blob Storage support unchanged.
;;;
;;; Built entirely on existing curry building blocks — no new C code:
;;;   (curry crypto)  hmac-sha256 / sha256-hex for AWS Signature Version 4
;;;   (curry http)    http-request/headers, extended to accept a
;;;                   bytevector body (not just a string) so binary
;;;                   object uploads aren't forced through a UTF-8-
;;;                   validating conversion — see modules/http/http.c's
;;;                   resolve_body
;;;   (curry xml)     parses ListBucketResult / Error / multipart-upload
;;;                   response bodies (S3's XML has no namespace
;;;                   prefixes to resolve, just a default xmlns on the
;;;                   root — squarely within (curry xml)'s documented
;;;                   scope)
;;;   (srfi 19)       UTC date formatting for the x-amz-date header
;;;   (srfi 132)      list-sort, for canonical-header/query-string
;;;                   ordering SigV4 requires
;;;
;;; Entry points:
;;;   (s3-client access-key secret-key region [endpoint]) -> <s3-client>
;;;   (s3-client? obj) -> boolean
;;;
;;;   (s3-put! client bucket key data [content-type])     -> #t
;;;   (s3-get client bucket key)                          -> bytevector or #f
;;;   (s3-delete! client bucket key)                      -> #t
;;;   (s3-head client bucket key)                         -> alist or #f
;;;   (s3-copy! client bucket key src-bucket src-key)     -> #t
;;;
;;;   (s3-list client bucket [prefix] [delimiter] [continuation-token] [max-keys])
;;;     -> <s3-list-result>  ; positional trailing optionals, R7RS has no
;;;                          ; keyword args; pass #f to skip one and set a
;;;                          ; later one, e.g. (s3-list c b #f "/" tok)
;;;   (s3-list-objects result) -> list of <s3-object>
;;;   (s3-list-common-prefixes result) -> list of strings
;;;   (s3-list-truncated? result) -> boolean
;;;   (s3-list-next-token result) -> string or #f
;;;   <s3-object> accessors: s3-object-key s3-object-last-modified
;;;     s3-object-etag s3-object-size s3-object-storage-class
;;;
;;;   (s3-bucket-create! client bucket)                   -> #t
;;;   (s3-bucket-delete! client bucket)                   -> #t
;;;
;;;   (s3-presign client bucket key seconds [method])     -> url string
;;;
;;;   (s3-put-large! client bucket key data [content-type] [part-size])
;;;     -> #t  ; automatic multipart upload for data too big for one PUT
;;;   (s3-multipart-create! client bucket key [content-type]) -> upload-id
;;;   (s3-multipart-upload-part! client bucket key upload-id part-number data)
;;;     -> etag string  ; caller accumulates (part-number . etag) pairs
;;;   (s3-multipart-complete! client bucket key upload-id parts) -> #t
;;;     ; parts: list of (part-number . etag), any order
;;;   (s3-multipart-abort! client bucket key upload-id)   -> #t
;;;
;;; Errors: any non-2xx response raises via (error message code status
;;; s3-message) — (error-object-irritants e) gives
;;; (list s3-error-code http-status s3-message-text), so callers that
;;; need to distinguish e.g. "NoSuchKey" from "AccessDenied" can
;;; pattern-match on (car (error-object-irritants e)) rather than
;;; parsing the message string. s3-get returns #f (not an error)
;;; specifically for a 404/NoSuchKey, matching R7RS association-style
;;; "not found" conventions elsewhere in curry (e.g.
;;; hash-table-ref/default); every other operation raises on any
;;; non-2xx.

(define-library (curry s3)
  (import (scheme base) (scheme write) (scheme char))
  (import (curry crypto) (curry http) (curry xml))
  (import (srfi 19) (srfi 132))
  (export
    s3-client s3-client?
    s3-put! s3-get s3-delete! s3-head s3-copy!
    s3-list s3-list-objects s3-list-common-prefixes
    s3-list-truncated? s3-list-next-token
    s3-object? s3-object-key s3-object-last-modified
    s3-object-etag s3-object-size s3-object-storage-class
    s3-bucket-create! s3-bucket-delete!
    s3-presign
    s3-put-large!
    s3-multipart-create! s3-multipart-upload-part!
    s3-multipart-complete! s3-multipart-abort!)
  (begin

;;; ── client ──────────────────────────────────────────────────────────

(define-record-type <s3-client>
  (make-s3-client access-key secret-key region host use-tls?)
  s3-client?
  (access-key s3-client-access-key)
  (secret-key s3-client-secret-key)
  (region     s3-client-region)
  ;; host: bare host[:port], scheme stripped -- e.g. "s3.us-east-1.amazonaws.com"
  ;; or "localhost:9000" for MinIO. Always used with https:// when building
  ;; request URLs; pass an http:// endpoint (MinIO/Ceph on plain HTTP) and
  ;; this constructor remembers that via use-tls?.
  (host       s3-client-host)
  (use-tls?   s3-client-use-tls?))

(define (%strip-scheme s)
  (cond
    ((and (>= (string-length s) 8) (string=? (substring s 0 8) "https://"))
     (values (substring s 8 (string-length s)) #t))
    ((and (>= (string-length s) 7) (string=? (substring s 0 7) "http://"))
     (values (substring s 7 (string-length s)) #f))
    (else (values s #t))))

(define (s3-client access-key secret-key region . opt)
  (let ((endpoint (if (pair? opt) (car opt) #f)))
    (if endpoint
        (call-with-values (lambda () (%strip-scheme endpoint))
          (lambda (host tls?) (make-s3-client access-key secret-key region host tls?)))
        (make-s3-client access-key secret-key region
                         (string-append "s3." region ".amazonaws.com") #t))))

;;; ── URI / query encoding (RFC 3986, AWS SigV4 conventions) ─────────────

;; Strictly ASCII A-Za-z0-9-_.~ -- NOT char-alphabetic?/char-numeric?,
;; which are Unicode-aware and would leave e.g. accented letters
;; unescaped. AWS's own canonicalization always percent-encodes
;; anything outside this exact ASCII set; disagreeing here would send
;; a URI that doesn't match what was signed.
(define (%unreserved-char? ch)
  (or (and (char<=? #\a ch) (char<=? ch #\z))
      (and (char<=? #\A ch) (char<=? ch #\Z))
      (and (char<=? #\0 ch) (char<=? ch #\9))
      (memv ch '(#\- #\_ #\. #\~))))

;; Almost always a single ASCII byte (one %XX) -- the loop only runs
;; more than once for a multi-byte UTF-8 character (at most 4 bytes),
;; so plain string-append accumulation beats the overhead of opening
;; an output-string port per call.
(define (%percent-encode-char ch)
  (let* ((bv (string->utf8 (string ch))) (n (bytevector-length bv)))
    (let loop ((i 0) (acc ""))
      (if (= i n)
          acc
          (loop (+ i 1) (string-append acc "%" (%byte->hex (bytevector-u8-ref bv i))))))))

;; Two separate tables, not one: AWS requires UPPERCASE hex digits for
;; percent-encoding (%2F, not %2f) but LOWERCASE hex for the SigV4
;; signature itself and payload/canonical-request hashes -- conflating
;; them into one table would make every real request fail server-side
;; signature verification despite every step up to the final compare
;; looking fine locally.
(define +hex-digits-upper+ "0123456789ABCDEF")
(define +hex-digits-lower+ "0123456789abcdef")

(define (%byte->hex b)
  (string (string-ref +hex-digits-upper+ (quotient b 16))
          (string-ref +hex-digits-upper+ (remainder b 16))))

(define (%byte->hex-lower b)
  (string (string-ref +hex-digits-lower+ (quotient b 16))
          (string-ref +hex-digits-lower+ (remainder b 16))))

;; keep-slash?: #t for a path (S3's canonical-URI rule leaves '/' as the
;; literal segment separator, never percent-encoded), #f for a query
;; value (where '/' is just another reserved character).
(define (%uri-encode s keep-slash?)
  (let ((out (open-output-string)))
    (string-for-each
      (lambda (ch)
        (cond
          ((%unreserved-char? ch) (write-char ch out))
          ((and keep-slash? (char=? ch #\/)) (write-char ch out))
          (else (write-string (%percent-encode-char ch) out))))
      s)
    (get-output-string out)))

(define (%canonical-uri bucket key)
  (string-append "/" (%uri-encode bucket #f)
                 (if (and key (> (string-length key) 0))
                     (string-append "/" (%uri-encode key #t))
                     "")))

(define (%canonical-query-string params)
  ;; params: alist of (name . value) strings, already unencoded.
  (let* ((encoded (map (lambda (kv) (cons (%uri-encode (car kv) #f)
                                           (%uri-encode (cdr kv) #f)))
                        params))
         (sorted (list-sort (lambda (a b) (string<? (car a) (car b))) encoded)))
    (let loop ((ps sorted) (acc '()))
      (if (null? ps)
          (%join (reverse acc) "&")
          (loop (cdr ps) (cons (string-append (caar ps) "=" (cdar ps)) acc))))))

(define (%join strs sep)
  (cond
    ((null? strs) "")
    ((null? (cdr strs)) (car strs))
    (else (string-append (car strs) sep (%join (cdr strs) sep)))))

;;; ── hex / hashing helpers ───────────────────────────────────────────

;; Lowercase -- this is the SigV4 signature encoding, not a percent-encode.
(define (%bytevector->hex bv)
  (let* ((n (bytevector-length bv)) (out (open-output-string)))
    (let loop ((i 0))
      (when (< i n)
        (write-string (%byte->hex-lower (bytevector-u8-ref bv i)) out)
        (loop (+ i 1))))
    (get-output-string out)))

(define (%data->bytevector data)
  (if (bytevector? data) data (string->utf8 data)))

(define (%hmac k m) (hmac-sha256 k m))

;; SigV4 spec: trim leading/trailing whitespace from each header value
;; and collapse internal whitespace runs to a single space before
;; including it in CanonicalHeaders -- AWS's own server-side signature
;; check does the same normalization, so a caller-supplied header (e.g.
;; content-type with incidental extra spaces) that isn't normalized
;; here would sign one string while AWS verifies against another,
;; producing a mismatch. Only affects the signature computation, not
;; what's literally sent on the wire -- HTTP header value whitespace is
;; insignificant per RFC 7230, so the actual request doesn't need
;; rewriting to match.
(define (%normalize-header-value s)
  (let ((len (string-length s)) (out (open-output-string)))
    (let loop ((i 0) (pending-space? #f) (started? #f))
      (if (= i len)
          (get-output-string out)
          (let ((c (string-ref s i)))
            (if (or (char=? c #\space) (char=? c #\tab))
                (loop (+ i 1) #t started?)
                (begin
                  (when (and pending-space? started?) (write-char #\space out))
                  (write-char c out)
                  (loop (+ i 1) #f #t))))))))

;; AWS4-HMAC-SHA256 signing-key derivation chain -- shared by %sign
;; (Authorization-header requests) and s3-presign (query-string
;; requests), which otherwise duplicated this verbatim.
(define (%signing-key client date8)
  (let* ((region (s3-client-region client))
         (k-date    (%hmac (string->utf8 (string-append "AWS4" (s3-client-secret-key client)))
                            (string->utf8 date8)))
         (k-region  (%hmac k-date   (string->utf8 region)))
         (k-service (%hmac k-region (string->utf8 "s3"))))
    (%hmac k-service (string->utf8 "aws4_request"))))

;;; ── UTC timestamps ──────────────────────────────────────────────────

(define (%now-strings)
  (let ((d (time-utc->date (current-time) 0)))
    (values (date->string d "~Y~m~d") (date->string d "~Y~m~dT~H~M~SZ"))))

;;; ── AWS Signature Version 4 ─────────────────────────────────────────

;; Builds the Authorization header value plus the x-amz-date used to
;; compute it. extra-headers: alist of (lowercase-name . value) beyond
;; host/x-amz-content-sha256/x-amz-date, e.g. content-type -- merged
;; into the signed set and sorted alphabetically together with the rest.
(define (%sign client method canonical-uri canonical-query
                payload-hash extra-headers)
  (call-with-values %now-strings
    (lambda (date8 datetime16)
      (let* ((region (s3-client-region client))
             (host   (s3-client-host client))
             (base-headers
               (list (cons "host" host)
                     (cons "x-amz-content-sha256" payload-hash)
                     (cons "x-amz-date" datetime16)))
             (all-headers (list-sort (lambda (a b) (string<? (car a) (car b)))
                                      (map (lambda (kv) (cons (car kv) (%normalize-header-value (cdr kv))))
                                           (append base-headers extra-headers))))
             (canonical-headers
               (%join (map (lambda (kv) (string-append (car kv) ":" (cdr kv) "\n")) all-headers) ""))
             (signed-headers (%join (map car all-headers) ";"))
             (canonical-request
               (string-append method "\n" canonical-uri "\n" canonical-query "\n"
                               canonical-headers "\n" signed-headers "\n" payload-hash))
             (cr-hash (sha256-hex (string->utf8 canonical-request)))
             (scope (string-append date8 "/" region "/s3/aws4_request"))
             (string-to-sign
               (string-append "AWS4-HMAC-SHA256\n" datetime16 "\n" scope "\n" cr-hash))
             (signature (%bytevector->hex (%hmac (%signing-key client date8) (string->utf8 string-to-sign))))
             (auth (string-append
                     "AWS4-HMAC-SHA256 Credential=" (s3-client-access-key client) "/" scope
                     ",SignedHeaders=" signed-headers ",Signature=" signature)))
        (values auth datetime16)))))

;;; ── request plumbing ────────────────────────────────────────────────

(define (%base-url client) (string-append (if (s3-client-use-tls? client) "https://" "http://")
                                           (s3-client-host client)))

(define (%s3-request client method bucket key . opt)
  ;; opt: [query-params] [body-bytevector] [content-type] [extra-headers]
  ;; extra-headers: alist of (lowercase-name . value) signed and sent
  ;; alongside content-type, e.g. x-amz-copy-source for s3-copy!.
  (let* ((query    (if (and (pair? opt) (car opt)) (car opt) '()))
         (body     (if (and (pair? opt) (pair? (cdr opt)) (cadr opt)) (cadr opt) #f))
         (ct       (if (and (pair? opt) (pair? (cdr opt)) (pair? (cddr opt))) (caddr opt) #f))
         (user-extra (if (and (pair? opt) (pair? (cdr opt)) (pair? (cddr opt)) (pair? (cdddr opt)))
                          (cadddr opt) '()))
         (payload  (if body (%data->bytevector body) (make-bytevector 0 0)))
         (payload-hash (sha256-hex payload))
         (canonical-uri (%canonical-uri bucket key))
         (canonical-query (%canonical-query-string query))
         (extra-headers (append (if ct (list (cons "content-type" ct)) '()) user-extra)))
    (call-with-values
      (lambda () (%sign client method canonical-uri canonical-query payload-hash extra-headers))
      (lambda (auth datetime16)
        (let* ((qs (if (string=? canonical-query "") "" (string-append "?" canonical-query)))
               (url (string-append (%base-url client) canonical-uri qs))
               (headers (append
                          (list (cons "Authorization" auth)
                                (cons "x-amz-date" datetime16)
                                (cons "x-amz-content-sha256" payload-hash))
                          (if ct (list (cons "Content-Type" ct)) '())
                          user-extra))
               (result (http-request/headers method url headers
                                              (if body payload #f))))
          (values (car result) (cadr result) (caddr result)))))))

(define (%xml-text-or-false el tag)
  (let ((c (xml-find el tag))) (and c (xml-element-text c))))

;; Single xml-parse, not one per field -- (values #f #f) for anything
;; that isn't a well-formed <Error> body (empty body, HTML error page
;; from a non-AWS S3-compatible endpoint, etc).
(define (%parse-s3-error body)
  (guard (e (#t (values #f #f)))
    (let ((el (xml-parse body)))
      (if (eq? (xml-tag el) 'Error)
          (values (%xml-text-or-false el 'Code) (%xml-text-or-false el 'Message))
          (values #f #f)))))

(define (%s3-error! op bucket key status body)
  (call-with-values (lambda () (%parse-s3-error body))
    (lambda (code msg)
      (error (string-append op ": HTTP " (number->string status)
                            " for s3://" bucket (if key (string-append "/" key) "")
                            (if code (string-append " (" code ")") ""))
             (or code "unknown") status (or msg "")))))

(define (%2xx? status) (and (>= status 200) (< status 300)))

;;; ── object operations ───────────────────────────────────────────────

(define (s3-put! client bucket key data . opt)
  (let ((ct (if (pair? opt) (car opt) "application/octet-stream")))
    (call-with-values (lambda () (%s3-request client "PUT" bucket key '() data ct))
      (lambda (status headers body)
        (if (%2xx? status) #t (%s3-error! "s3-put!" bucket key status body))))))

;; A 404 is only a soft #f when it's specifically NoSuchKey -- S3 also
;; returns 404 for NoSuchBucket (a real config/typo bug worth raising,
;; not silently indistinguishable from a normal cache-miss-style
;; lookup) and, on an unparseable body, we can't tell which happened,
;; so err toward raising rather than masking a possible real failure.
(define (s3-get client bucket key)
  (call-with-values (lambda () (%s3-request client "GET" bucket key))
    (lambda (status headers body)
      (cond
        ((%2xx? status) (string->utf8 body))
        ((= status 404)
         (call-with-values (lambda () (%parse-s3-error body))
           (lambda (code msg)
             (if (equal? code "NoSuchKey")
                 #f
                 (%s3-error! "s3-get" bucket key status body)))))
        (else (%s3-error! "s3-get" bucket key status body))))))

(define (s3-delete! client bucket key)
  (call-with-values (lambda () (%s3-request client "DELETE" bucket key))
    (lambda (status headers body)
      (if (or (%2xx? status) (= status 404)) #t (%s3-error! "s3-delete!" bucket key status body)))))

(define (s3-head client bucket key)
  (call-with-values (lambda () (%s3-request client "HEAD" bucket key))
    (lambda (status headers body)
      (cond
        ((= status 404) #f)
        ((%2xx? status)
         (list (cons 'content-length (let ((v (assoc "content-length" headers)))
                                        (and v (string->number (cdr v)))))
               (cons 'content-type   (let ((v (assoc "content-type" headers))) (and v (cdr v))))
               (cons 'etag           (let ((v (assoc "etag" headers))) (and v (cdr v))))
               (cons 'last-modified  (let ((v (assoc "last-modified" headers))) (and v (cdr v))))))
        (else (%s3-error! "s3-head" bucket key status body))))))

;; PUT-copy: no body of its own (the source object's content becomes the
;; new object's content server-side), identified purely by the
;; x-amz-copy-source header. On success S3 returns 200 with a
;; <CopyObjectResult><ETag>.../LastModified>...</CopyObjectResult> body,
;; not (as its status code alone might suggest) a guaranteed-successful
;; copy -- a mid-transfer failure can still show up as a 200 with an
;; <Error> body, but that's the same "trust the body over the status
;; code" case any S3 client has to handle; not specially detected here.
(define (s3-copy! client bucket key src-bucket src-key)
  (let* ((source (string-append "/" (%uri-encode src-bucket #f) "/" (%uri-encode src-key #t)))
         (extra (list (cons "x-amz-copy-source" source))))
    (call-with-values
      (lambda () (%s3-request client "PUT" bucket key '() #f #f extra))
      (lambda (status headers body)
        (if (%2xx? status) #t (%s3-error! "s3-copy!" bucket key status body))))))

;;; ── bucket listing ──────────────────────────────────────────────────

(define-record-type <s3-object>
  (make-s3-object key last-modified etag size storage-class)
  s3-object?
  (key            s3-object-key)
  (last-modified  s3-object-last-modified)
  (etag           s3-object-etag)
  (size           s3-object-size)
  (storage-class  s3-object-storage-class))

(define-record-type <s3-list-result>
  (make-s3-list-result objects common-prefixes truncated? next-token)
  s3-list-result?
  (objects         s3-list-objects)
  (common-prefixes s3-list-common-prefixes)
  (truncated?      s3-list-truncated?)
  (next-token      s3-list-next-token))

(define (%contents->object el)
  (make-s3-object (%xml-text-or-false el 'Key)
                   (%xml-text-or-false el 'LastModified)
                   (%xml-text-or-false el 'ETag)
                   (let ((s (%xml-text-or-false el 'Size))) (and s (string->number s)))
                   (%xml-text-or-false el 'StorageClass)))

(define (s3-list client bucket . opt)
  ;; opt keyword-ish plist: (prefix delimiter continuation-token max-keys),
  ;; each defaulting to #f/absent -- passed positionally for R7RS simplicity.
  (let* ((prefix    (if (>= (length opt) 1) (list-ref opt 0) #f))
         (delimiter (if (>= (length opt) 2) (list-ref opt 1) #f))
         (cont-tok  (if (>= (length opt) 3) (list-ref opt 2) #f))
         (max-keys  (if (>= (length opt) 4) (list-ref opt 3) #f))
         (params (append (list (cons "list-type" "2"))
                          (if prefix    (list (cons "prefix" prefix)) '())
                          (if delimiter (list (cons "delimiter" delimiter)) '())
                          (if cont-tok  (list (cons "continuation-token" cont-tok)) '())
                          (if max-keys  (list (cons "max-keys" (number->string max-keys))) '()))))
    (call-with-values (lambda () (%s3-request client "GET" bucket #f params))
      (lambda (status headers body)
        (if (not (%2xx? status)) (%s3-error! "s3-list" bucket #f status body)
            (let* ((root (xml-parse body))
                   (objects (map %contents->object (xml-find-all root 'Contents)))
                   (prefixes (map (lambda (cp) (%xml-text-or-false cp 'Prefix))
                                   (xml-find-all root 'CommonPrefixes)))
                   (trunc (let ((t (%xml-text-or-false root 'IsTruncated)))
                            (and t (string=? t "true"))))
                   (next (%xml-text-or-false root 'NextContinuationToken)))
              (make-s3-list-result objects prefixes trunc next)))))))

;;; ── buckets ──────────────────────────────────────────────────────────

;; AWS rejects a bucket-create PUT with no body for any region other
;; than us-east-1 (its own legacy default) -- every other region
;; requires an explicit CreateBucketConfiguration/LocationConstraint
;; body naming the region, or the request fails with
;; IllegalLocationConstraintException despite the client and bucket
;; nominally agreeing on where the bucket should live.
(define (s3-bucket-create! client bucket)
  (let* ((region (s3-client-region client))
         (body (if (string=? region "us-east-1")
                   #f
                   (string->utf8
                     (xml-stringify
                       (make-xml-element 'CreateBucketConfiguration '()
                         (list (xml-element 'LocationConstraint region))))))))
    (call-with-values
      (lambda () (%s3-request client "PUT" bucket #f '() body (if body "application/xml" #f)))
      (lambda (status headers resp-body)
        (if (%2xx? status) #t (%s3-error! "s3-bucket-create!" bucket #f status resp-body))))))

(define (s3-bucket-delete! client bucket)
  (call-with-values (lambda () (%s3-request client "DELETE" bucket #f))
    (lambda (status headers body)
      (if (%2xx? status) #t (%s3-error! "s3-bucket-delete!" bucket #f status body)))))

;;; ── presigned URLs ───────────────────────────────────────────────────

;; Query-string SigV4: no Authorization header, the signature itself
;; becomes a query parameter. payload is always "UNSIGNED-PAYLOAD" per
;; AWS's own spec for presigned URLs (the eventual requester supplies
;; the body, which the signer here never sees).
(define (s3-presign client bucket key seconds . opt)
  (let ((method (if (pair? opt) (car opt) "GET")))
    (call-with-values %now-strings
      (lambda (date8 datetime16)
        (let* ((region (s3-client-region client))
               (host   (s3-client-host client))
               (scope (string-append date8 "/" region "/s3/aws4_request"))
               (canonical-uri (%canonical-uri bucket key))
               (base-params
                 (list (cons "X-Amz-Algorithm" "AWS4-HMAC-SHA256")
                       (cons "X-Amz-Credential" (string-append (s3-client-access-key client) "/" scope))
                       (cons "X-Amz-Date" datetime16)
                       (cons "X-Amz-Expires" (number->string seconds))
                       (cons "X-Amz-SignedHeaders" "host")))
               (canonical-query (%canonical-query-string base-params))
               (canonical-headers (string-append "host:" host "\n"))
               (canonical-request
                 (string-append method "\n" canonical-uri "\n" canonical-query "\n"
                                 canonical-headers "\nhost\nUNSIGNED-PAYLOAD"))
               (cr-hash (sha256-hex (string->utf8 canonical-request)))
               (string-to-sign (string-append "AWS4-HMAC-SHA256\n" datetime16 "\n" scope "\n" cr-hash))
               (signature (%bytevector->hex (%hmac (%signing-key client date8) (string->utf8 string-to-sign)))))
          (string-append (%base-url client) canonical-uri "?" canonical-query
                          "&X-Amz-Signature=" signature))))))

;;; ── multipart upload ─────────────────────────────────────────────────

;; S3 requires every part but the last to be >= 5 MiB.
(define +default-part-size+ (* 8 1024 1024))
(define +s3-min-part-size+  (* 5 1024 1024))

(define (s3-multipart-create! client bucket key . opt)
  (let* ((ct (if (pair? opt) (car opt) "application/octet-stream")))
    (call-with-values
      (lambda () (%s3-request client "POST" bucket key (list (cons "uploads" "")) #f ct))
      (lambda (status headers body)
        (if (not (%2xx? status)) (%s3-error! "s3-multipart-create!" bucket key status body)
            (%xml-text-or-false (xml-parse body) 'UploadId))))))

(define (s3-multipart-upload-part! client bucket key upload-id part-number data)
  (let ((params (list (cons "partNumber" (number->string part-number))
                       (cons "uploadId" upload-id))))
    (call-with-values (lambda () (%s3-request client "PUT" bucket key params data))
      (lambda (status headers body)
        (if (%2xx? status)
            (let ((v (assoc "etag" headers))) (and v (cdr v)))
            (%s3-error! "s3-multipart-upload-part!" bucket key status body))))))

(define (%part->xml part)
  (xml-element 'Part (xml-element 'PartNumber (number->string (car part)))
                      (xml-element 'ETag (cdr part))))

(define (s3-multipart-complete! client bucket key upload-id parts)
  (let* ((sorted (list-sort (lambda (a b) (< (car a) (car b))) parts))
         (body-el (make-xml-element 'CompleteMultipartUpload '() (map %part->xml sorted)))
         (body (string->utf8 (xml-stringify body-el)))
         (params (list (cons "uploadId" upload-id))))
    (call-with-values
      (lambda () (%s3-request client "POST" bucket key params body "application/xml"))
      (lambda (status headers resp-body)
        (if (%2xx? status) #t (%s3-error! "s3-multipart-complete!" bucket key status resp-body))))))

(define (s3-multipart-abort! client bucket key upload-id)
  (call-with-values
    (lambda () (%s3-request client "DELETE" bucket key (list (cons "uploadId" upload-id))))
    (lambda (status headers body)
      (if (%2xx? status) #t (%s3-error! "s3-multipart-abort!" bucket key status body)))))

(define (s3-put-large! client bucket key data . opt)
  (let* ((ct (if (pair? opt) (car opt) "application/octet-stream"))
         ;; Clamped up to S3's own minimum (every part but the last
         ;; must be >= 5 MiB) rather than trusting the caller's value
         ;; outright -- an unclamped too-small part-size would still
         ;; upload every part successfully and only fail late, on
         ;; s3-multipart-complete!, with EntityTooSmall.
         (part-size (max +s3-min-part-size+
                          (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) +default-part-size+)))
         (bv (%data->bytevector data))
         (n (bytevector-length bv)))
    (if (<= n part-size)
        (s3-put! client bucket key bv ct)
        (let ((upload-id (s3-multipart-create! client bucket key ct)))
          (guard (e (#t (s3-multipart-abort! client bucket key upload-id) (raise e)))
            (let loop ((offset 0) (part-number 1) (parts '()))
              (if (>= offset n)
                  (begin (s3-multipart-complete! client bucket key upload-id (reverse parts)) #t)
                  (let* ((end (min n (+ offset part-size)))
                         (chunk (bytevector-copy bv offset end))
                         (etag (s3-multipart-upload-part! client bucket key upload-id part-number chunk)))
                    (loop end (+ part-number 1) (cons (cons part-number etag) parts))))))))))

)) ;; end define-library
