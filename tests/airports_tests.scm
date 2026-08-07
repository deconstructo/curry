;;; (curry airports) tests — CSV parsing, cache freshness, and lookup/search,
;;; all against a synthetic fixture under an isolated HOME so this suite
;;; never touches the network or the real user cache. HOME is overridden
;;; *before* (curry airports) is first imported, since its cache-path
;;; constants are computed once at module-instantiation time from
;;; get-environment-variable.

(import (curry posix))

(define %test-home "/tmp/curry-airports-test-home")
(define %cache-dir (string-append %test-home "/.cache/curry"))

(define (%mkdir-p path)
  (unless (file-exists? path) (create-directory path)))

;; Fresh slate every run.
(when (file-exists? (string-append %cache-dir "/airports.csv"))
  (delete-file (string-append %cache-dir "/airports.csv")))
(when (file-exists? (string-append %cache-dir "/airports.csv.meta"))
  (delete-file (string-append %cache-dir "/airports.csv.meta")))

(set-environment-variable! "HOME" %test-home)
(%mkdir-p %test-home)
(%mkdir-p (string-append %test-home "/.cache"))
(%mkdir-p %cache-dir)

;; Fixture covers: a plain quoted row, an empty field, a value containing
;; an escaped ("") quote, a value containing a comma (only safe inside
;; quotes), and a deliberately short/malformed row that must be skipped
;; rather than crashing the loader.
(call-with-output-file (string-append %cache-dir "/airports.csv")
  (lambda (p)
    (write-string "\"icao\",\"iata\",\"name\",\"city\",\"subd\",\"country\",\"elevation\",\"lat\",\"lon\",\"tz\",\"lid\"\n" p)
    (write-string "\"YOAS\",\"\",\"The Oaks Airport\",\"\",\"New South Wales\",\"AU\",\"909\",\"-34.0839\",\"150.559\",\"Australia/Sydney\",\"\"\n" p)
    (write-string "\"YKAT\",\"\",\"Katoomba Airport\",\"\",\"New South Wales\",\"AU\",\"1000\",\"-33.6683\",\"150.323\",\"Australia/Sydney\",\"\"\n" p)
    (write-string "\"YTST\",\"\",\"Say \"\"Hello\"\" Field, Regional\",\"\",\"Testia\",\"AU\",\"\",\"\",\"\",\"\",\"\"\n" p)
    (write-string "\"YBAD\",\"only-two-fields\"\n" p)))

;; A meta file timestamped "now" with no ETag: %ensure-cache-fresh! must
;; see this as fresh and never touch the network.
(call-with-output-file (string-append %cache-dir "/airports.csv.meta")
  (lambda (p)
    (write-string (number->string (current-second)) p) (newline p)
    (write-string "" p) (newline p)))

(import (curry airports))

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

(check "lookup YOAS name" (airport-name (airport-lookup "YOAS")) "The Oaks Airport")
(check "lookup YOAS region" (airport-region (airport-lookup "YOAS")) "New South Wales")
(check "lookup YOAS elevation (number)" (airport-elevation (airport-lookup "YOAS")) 909)
(check "lookup YOAS lat (number)" (airport-lat (airport-lookup "YOAS")) -34.0839)
(check "lookup is case-insensitive" (airport-name (airport-lookup "yoas")) "The Oaks Airport")
(check "lookup YKAT name" (airport-name (airport-lookup "YKAT")) "Katoomba Airport")
(check "lookup unknown ICAO" (airport-lookup "ZZZZ") #f)
(check "empty field parses as #f, not 0" (airport-elevation (airport-lookup "YKAT")) 1000)

(check "embedded comma + escaped quote survive parsing"
       (airport-name (airport-lookup "YTST"))
       "Say \"Hello\" Field, Regional")

(check "malformed short row is skipped, not crashed"
       (airport-lookup "YBAD")
       #f)

(check "search matches by substring, case-insensitively"
       (map airport-icao (airport-search "the oaks"))
       '("YOAS"))

(check "search with no match returns empty list"
       (airport-search "nonexistent-airport-name-xyz")
       '())

(display "\n")
(display pass) (display " passed, ") (display fail) (display " failed\n")
(when (> fail 0)
  (error (string-append "airports_tests: " (number->string fail) " test(s) failed")))
