;;; (curry airports) — ICAO/name airport directory lookup
;;;
;;; Data source: mborsetti/airportsdata's airports.csv (MIT licensed,
;;; derived from OurAirports' public-domain data) — roughly 28,000
;;; airports and landing strips worldwide, keyed by ICAO code, with IATA
;;; code, name, city, region, country, elevation, coordinates, timezone,
;;; and FAA LID.
;;;
;;; The CSV is downloaded lazily on first use into ~/.cache/curry/airports.csv
;;; and reused from then on. A cached copy younger than +ttl-seconds+ is
;;; used as-is with no network access at all. Once it's older than that,
;;; a conditional GET (If-None-Match against the stored ETag) checks
;;; whether the source has actually changed before re-downloading — a
;;; 304 just refreshes the local timestamp. If the network is unreachable
;;; the stale cache is used rather than failing outright, so lookups keep
;;; working offline once the data has been fetched once.
;;;
;;; Requires (curry http) (BUILD_MODULE_HTTP, default ON) and
;;; (curry posix) (BUILD_MODULE_POSIX, default ON) for create-directory
;;; and rename-file.
;;;
;;; Entry points:
;;;   (airport-lookup icao)     -> <airport> or #f
;;;   (airport-search substr)   -> list of <airport>, matching by name
;;;                                 (case-insensitive substring)
;;;   (airport-refresh!)        -> forces a conditional check against the
;;;                                 source now, bypassing the TTL
;;;
;;; Accessors on the returned <airport> records:
;;;   airport-icao airport-iata airport-name airport-city airport-region
;;;   airport-country airport-elevation airport-lat airport-lon
;;;   airport-tz airport-lid

(define-library (curry airports)
  (import (scheme base) (scheme write) (scheme file))
  (import (curry http))
  (import (curry posix))
  (export
    airport? airport-icao airport-iata airport-name airport-city
    airport-region airport-country airport-elevation airport-lat
    airport-lon airport-tz airport-lid
    airport-lookup airport-search airport-refresh!)
  (begin

;;; ── record ──────────────────────────────────────────────────────────

(define-record-type <airport>
  (make-airport icao iata name city region country elevation lat lon tz lid)
  airport?
  (icao      airport-icao)
  (iata      airport-iata)
  (name      airport-name)
  (city      airport-city)
  (region    airport-region)
  (country   airport-country)
  (elevation airport-elevation)
  (lat       airport-lat)
  (lon       airport-lon)
  (tz        airport-tz)
  (lid       airport-lid))

;;; ── cache configuration ─────────────────────────────────────────────

(define +source-url+
  "https://raw.githubusercontent.com/mborsetti/airportsdata/main/airportsdata/airports.csv")

(define +cache-dir+
  (string-append (or (get-environment-variable "HOME") ".") "/.cache/curry"))

(define +cache-path+ (string-append +cache-dir+ "/airports.csv"))
(define +meta-path+  (string-append +cache-dir+ "/airports.csv.meta"))

(define +ttl-seconds+ (* 30 24 60 60)) ; 30 days

(define *airports* #f)      ; hash-table: ICAO string -> <airport>
(define *airport-list* '()) ; all <airport> records, for name search

;;; ── CSV parsing (no embedded newlines within a quoted field — true of
;;; this dataset, and a standard simplifying assumption for line-oriented
;;; CSV readers) ──────────────────────────────────────────────────────

(define (%split-csv-line line)
  (let ((len (string-length line)))
    (let loop ((i 0) (field '()) (fields '()) (in-quotes #f))
      (cond
        ((>= i len)
         (reverse (cons (list->string (reverse field)) fields)))
        (in-quotes
         (let ((c (string-ref line i)))
           (cond
             ((and (char=? c #\") (< (+ i 1) len) (char=? (string-ref line (+ i 1)) #\"))
              (loop (+ i 2) (cons #\" field) fields #t))
             ((char=? c #\")
              (loop (+ i 1) field fields #f))
             (else (loop (+ i 1) (cons c field) fields #t)))))
        (else
         (let ((c (string-ref line i)))
           (cond
             ((char=? c #\")
              (loop (+ i 1) field fields #t))
             ((char=? c #\,)
              (loop (+ i 1) '() (cons (list->string (reverse field)) fields) #f))
             (else (loop (+ i 1) (cons c field) fields #f)))))))))

(define (%maybe-number s) (and (not (string=? s "")) (string->number s)))

(define (%fields->airport f)
  (and (= (length f) 11)
       (let ((icao (list-ref f 0)))
         (and (not (string=? icao ""))
              (make-airport
                icao (list-ref f 1) (list-ref f 2) (list-ref f 3)
                (list-ref f 4) (list-ref f 5)
                (%maybe-number (list-ref f 6))
                (%maybe-number (list-ref f 7))
                (%maybe-number (list-ref f 8))
                (list-ref f 9) (list-ref f 10))))))

(define (%load-index!)
  (set! *airports* (make-hash-table))
  (set! *airport-list* '())
  (call-with-input-file +cache-path+
    (lambda (port)
      (read-line port) ; header row
      (let loop ((line (read-line port)))
        (unless (eof-object? line)
          (unless (string=? line "")
            (let ((a (%fields->airport (%split-csv-line line))))
              (when a
                (hash-table-set! *airports* (airport-icao a) a)
                (set! *airport-list* (cons a *airport-list*)))))
          (loop (read-line port))))))
  (set! *airport-list* (reverse *airport-list*)))

;;; ── cache freshness / download ──────────────────────────────────────

(define (%read-meta)
  (if (file-exists? +meta-path+)
      (call-with-input-file +meta-path+
        (lambda (p)
          (let* ((fetched-line (read-line p))
                 (etag-line    (read-line p)))
            (cons (if (eof-object? fetched-line) 0 (or (string->number fetched-line) 0))
                  (if (eof-object? etag-line) "" etag-line)))))
      (cons 0 "")))

(define (%write-meta! fetched-at etag)
  (unless (file-exists? +cache-dir+) (create-directory +cache-dir+))
  (let ((tmp (string-append +meta-path+ ".tmp")))
    (call-with-output-file tmp
      (lambda (p)
        (write-string (number->string fetched-at) p) (newline p)
        (write-string etag p) (newline p)))
    (rename-file tmp +meta-path+)))

;; Returns 'updated, 'not-modified, or raises on a genuine HTTP-level
;; failure (network error, non-2xx/304 status).
(define (%download-and-cache! etag)
  (let* ((req-headers (if (string=? etag "") '() (list (cons "If-None-Match" etag))))
         (res    (http-request/headers "GET" +source-url+ req-headers))
         (status (car res))
         (hdrs   (cadr res))
         (body   (caddr res)))
    (cond
      ((= status 304)
       (%write-meta! (current-second) etag)
       'not-modified)
      ((= status 200)
       (unless (file-exists? +cache-dir+) (create-directory +cache-dir+))
       (let ((tmp (string-append +cache-path+ ".tmp")))
         (call-with-output-file tmp (lambda (p) (write-string body p)))
         (rename-file tmp +cache-path+))
       (%write-meta! (current-second)
                      (let ((e (assoc "etag" hdrs))) (if e (cdr e) "")))
       'updated)
      (else
       (error "airports: failed to fetch airport data" status)))))

;; force? bypasses the TTL and always does a conditional GET (still cheap
;; on a 304). Falls back silently to whatever's on disk if the network
;; check itself fails, so long as a cache already exists.
(define (%ensure-cache-fresh! force?)
  (if (not (file-exists? +cache-path+))
      (%download-and-cache! "")
      (let* ((meta       (%read-meta))
             (fetched-at (car meta))
             (etag       (cdr meta))
             (age        (- (current-second) fetched-at)))
        (if (and (not force?) (< age +ttl-seconds+))
            'fresh
            (guard (e (#t 'stale-fallback))
              (%download-and-cache! etag))))))

;; Reload the in-memory index whenever the on-disk cache actually changed
;; underneath it (status 'updated) — matters for long-running processes
;; (e.g. an MCP server) whose TTL expires mid-run, not just cold starts.
(define (%ensure-loaded!)
  (let ((status (%ensure-cache-fresh! #f)))
    (when (or (not *airports*) (eq? status 'updated))
      (%load-index!))))

;;; ── public API ──────────────────────────────────────────────────────

(define (airport-lookup icao)
  (%ensure-loaded!)
  (hash-table-ref *airports* (string-upcase icao) #f))

(define (airport-search substr)
  (%ensure-loaded!)
  (let ((q (string-downcase substr)))
    (filter (lambda (a) (string-contains (string-downcase (airport-name a)) q))
            *airport-list*)))

(define (airport-refresh!)
  (%ensure-cache-fresh! #t)
  (%load-index!)
  'ok)

)) ;; end begin, define-library
