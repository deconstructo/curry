;;; (curry fits) — FITS (Flexible Image Transport System) reader/writer.
;;;
;;; Pure Scheme, no build step, no external library. Covers the FITS
;;; primary-HDU image case (the common scientific-data use case): a
;;; sequence of 2880-byte blocks of 80-character ASCII header cards
;;; terminated by an END card, followed by big-endian binary image data
;;; (also padded to a multiple of 2880 bytes).
;;;
;;; Not covered: multiple HDUs / extensions (IMAGE/TABLE/BINTABLE
;;; extensions), random-groups records, checksums. Reading always returns
;;; the primary HDU only.
;;;
;;; API:
;;;   (fits-read-image path)   -> (values tensor header-alist)
;;;   (fits-write-image path tensor)
;;;   (fits-write-image path tensor #:header header-alist)
;;;   (fits-header-ref header key [default])  -- case-insensitive lookup

(define-library (curry fits)
  (import (curry private binary-io))
  (import (scheme base))
  (export
    fits-read-image fits-write-image fits-header-ref)
  (begin

(define %fits-block-size 2880)
(define %fits-card-size 80)

;; ── Header card parsing ──────────────────────────────────────────────────────

;; Trim trailing spaces (FITS pads keywords/values with spaces, not NUL).
(define (%fits-rtrim s)
  (let loop ((i (string-length s)))
    (if (and (> i 0) (char=? (string-ref s (- i 1)) #\space))
        (loop (- i 1))
        (substring s 0 i))))

(define (%fits-ltrim s)
  (let ((n (string-length s)))
    (let loop ((i 0))
      (if (and (< i n) (char=? (string-ref s i) #\space))
          (loop (+ i 1))
          (substring s i n)))))

(define (%fits-trim s) (%fits-ltrim (%fits-rtrim s)))

(define (%fits-find-char s ch start)
  (let ((n (string-length s)))
    (let loop ((i start))
      (cond ((>= i n) #f)
            ((char=? (string-ref s i) ch) i)
            (else (loop (+ i 1)))))))

;; A FITS value is quoted-string / logical (T/F) / integer / float.
;; Comments after "/" outside a quoted string are stripped.
(define (%fits-parse-value raw)
  (let ((v (%fits-trim raw)))
    (cond
      ((= (string-length v) 0) "")
      ((char=? (string-ref v 0) #\')
       ;; quoted string: find the closing quote (doubled '' = literal ')
       (let loop ((i 1) (acc '()))
         (cond
           ((>= i (string-length v)) (list->string (reverse acc)))
           ((and (char=? (string-ref v i) #\')
                 (< (+ i 1) (string-length v))
                 (char=? (string-ref v (+ i 1)) #\'))
            (loop (+ i 2) (cons #\' acc)))
           ((char=? (string-ref v i) #\')
            (%fits-rtrim (list->string (reverse acc))))
           (else (loop (+ i 1) (cons (string-ref v i) acc))))))
      ((or (string=? v "T") (string=? v "F"))
       (string=? v "T"))
      (else
       ;; strip an unquoted trailing comment ("/ ...") then parse as number
       (let* ((slash (%fits-find-char v #\/ 0))
              (numpart (%fits-trim (if slash (substring v 0 slash) v))))
         (or (string->number numpart) numpart))))) )

;; One 80-char card -> (values keyword value) or (values #f #f) for
;; blank/COMMENT/HISTORY cards (keyword present but no "= value" form).
(define (%fits-parse-card card)
  (let ((key (%fits-rtrim (substring card 0 8))))
    (if (and (>= (string-length card) 10)
             (char=? (string-ref card 8) #\=))
        (values key (%fits-parse-value (substring card 10 (string-length card))))
        (values key #f))))

(define (%fits-read-header port)
  ;; Returns an alist of (keyword . value); repeated keys (COMMENT/HISTORY)
  ;; all appear, in order.
  (let loop ((acc '()))
    (let* ((block (%read-exact-bytes port %fits-block-size))
           (text (utf8->string block)))
      (let card-loop ((offset 0) (acc acc))
        (if (>= offset %fits-block-size)
            (loop acc)   ; block exhausted, read another
            (let ((card (substring text offset (+ offset %fits-card-size))))
              (if (string=? (%fits-rtrim card) "END")
                  (reverse acc)
                  (call-with-values
                    (lambda () (%fits-parse-card card))
                    (lambda (key val)
                      (card-loop (+ offset %fits-card-size)
                                 (if (and key (not (string=? key "")))
                                     (cons (cons key val) acc)
                                     acc)))))))))))

(define (fits-header-ref header key . default)
  (let ((upkey (string-upcase key)))
    (let loop ((h header))
      (cond
        ((null? h) (if (pair? default) (car default) #f))
        ((string=? (string-upcase (caar h)) upkey) (cdar h))
        (else (loop (cdr h)))))))

;; ── BITPIX element decode ────────────────────────────────────────────────────

;; Returns (values bytes-per-elem decode-proc) for a given BITPIX.
(define (%fits-bitpix-decoder bitpix)
  (cond
    ((= bitpix 8)   (values 1 (lambda (bv i) (bytevector-u8-ref bv i))))
    ((= bitpix 16)  (values 2 %bv-s16be))
    ((= bitpix 32)  (values 4 %bv-s32be))
    ((= bitpix 64)  (values 8 %bv-s64be))
    ((= bitpix -32) (values 4 %bv-f32be))
    ((= bitpix -64) (values 8 %bv-f64be))
    (else (error "fits: unsupported BITPIX" bitpix))))

;; ── Public: read ─────────────────────────────────────────────────────────────

(define (fits-read-image path)
  (let* ((port (open-input-file path))
         (header (%fits-read-header port))
         (bitpix (fits-header-ref header "BITPIX"))
         (naxis (fits-header-ref header "NAXIS"))
         (bscale (fits-header-ref header "BSCALE" 1))
         (bzero (fits-header-ref header "BZERO" 0)))
    (let* ((shape (let loop ((n 1) (acc '()))
                    (if (> n naxis)
                        (reverse acc)
                        (loop (+ n 1)
                              (cons (fits-header-ref header
                                      (string-append "NAXIS" (number->string n)))
                                    acc)))))
           (size (fold-left * 1 shape)))
      (call-with-values (lambda () (%fits-bitpix-decoder bitpix))
        (lambda (elt-size decode)
          (let* ((data-bytes (* size elt-size))
                 (padded (%pad-to-multiple data-bytes %fits-block-size))
                 (raw (%read-exact-bytes port padded))
                 ;; FITS is column-major-first (NAXIS1 varies fastest); curry
                 ;; tensors are row-major with the first listed dim slowest,
                 ;; so reverse the shape to match storage order, fill flat,
                 ;; then reshape+transpose back to the FITS-declared axis order.
                 (t (make-tensor (reverse shape))))
            (let loop ((i 0) (byte 0))
              (when (< i size)
                (tensor-flat-set! t i (+ bzero (* bscale (decode raw byte))))
                (loop (+ i 1) (+ byte elt-size))))
            (close-port port)
            (values (%tensor-reverse-axes t) header)))))))

;; make-tensor gives a fresh tensor; fill it by flat (row-major) index.
(define (tensor-flat-set! t flat-i v)
  (let loop ((shape (tensor-shape t)) (rem flat-i) (idx '()))
    (if (null? shape)
        (apply tensor-set! t (append (reverse idx) (list v)))
        (let* ((sub (apply * (cdr shape)))
               (d (if (null? (cdr shape)) rem (quotient rem sub)))
               (r (if (null? (cdr shape)) 0 (remainder rem sub))))
          (loop (cdr shape) r (cons d idx))))))

;; Reverse a tensor's axis order (matches FITS NAXIS1-fastest convention
;; back onto curry's row-major-first-slowest tensors without re-deriving
;; strides by hand).
(define (%tensor-reverse-axes t)
  (let* ((n (tensor-ndim t))
         (perm (let loop ((i (- n 1)) (acc '()))
                 (if (< i 0) acc (loop (- i 1) (cons i acc))))))
    (tensor-transpose t (reverse perm))))

;; ── Public: write ────────────────────────────────────────────────────────────

(define (%fits-format-card key val comment)
  (define (pad s n) (string-append s (make-string (max 0 (- n (string-length s))) #\space)))
  (when (> (string-length key) 8)
    (error "fits: header keyword longer than 8 characters (FITS limit)" key))
  (let* ((keypart (pad (string-upcase key) 8))
         (valstr (cond
                   ((boolean? val) (if val "T" "F"))
                   ((string? val) (string-append "'" val "'"))
                   ((number? val) (number->string val))
                   (else (error "fits: unsupported header value type" val))))
         (body (string-append "= " (pad valstr 20)
                               (if comment (string-append " / " comment) ""))))
    (pad (string-append keypart body) %fits-card-size)))

(define (fits-write-image path tensor . rest)
  (let* ((extra-header (if (and (pair? rest) (eq? (car rest) '#:header))
                           (cadr rest) '()))
         (shape (reverse (tensor-shape (%tensor-reverse-axes tensor))))
         (naxis (length shape))
         (port (open-output-file path))
         (cards (append
                  (list (cons "SIMPLE" #t)
                        (cons "BITPIX" -64)
                        (cons "NAXIS" naxis))
                  (let loop ((i 1) (s shape) (acc '()))
                    (if (null? s) (reverse acc)
                        (loop (+ i 1) (cdr s)
                              (cons (cons (string-append "NAXIS" (number->string i))
                                          (car s))
                                    acc))))
                  extra-header)))
    (define (write-cards cards)
      (for-each
        (lambda (kv) (write-string (%fits-format-card (car kv) (cdr kv) #f) port))
        cards)
      ;; END has no "= value" part — just the keyword, blank-padded.
      (write-string (string-append "END" (make-string (- %fits-card-size 3) #\space))
                    port))
    (let* ((n-cards (+ (length cards) 1))
           (n-blocks (ceiling (/ n-cards (/ %fits-block-size %fits-card-size))))
           (pad-cards (- (* n-blocks (/ %fits-block-size %fits-card-size)) n-cards)))
      (write-cards cards)
      (do ((i 0 (+ i 1))) ((= i pad-cards))
        (write-string (make-string %fits-card-size #\space) port)))
    ;; Data: reversed axis order (FITS NAXIS1-fastest), always written as
    ;; BITPIX=-64 (double) for simplicity — a lossless round-trip target
    ;; since curry tensors are double-only anyway.
    (let* ((rt (%tensor-reverse-axes tensor))
           (size (tensor-size rt))
           (data-bytes (* size 8))
           (padded (%pad-to-multiple data-bytes %fits-block-size)))
      (let loop ((i 0))
        (when (< i size)
          (%write-f64be port (%tensor-flat-ref rt i))
          (loop (+ i 1))))
      (do ((i data-bytes (+ i 1))) ((= i padded))
        (write-u8 0 port)))
    (close-port port)))

(define (%tensor-flat-ref t flat-i)
  (let loop ((shape (tensor-shape t)) (rem flat-i) (idx '()))
    (if (null? shape)
        (apply tensor-ref t (reverse idx))
        (let* ((sub (apply * (cdr shape)))
               (d (if (null? (cdr shape)) rem (quotient rem sub)))
               (r (if (null? (cdr shape)) 0 (remainder rem sub))))
          (loop (cdr shape) r (cons d idx))))))

(define (%write-f64be port x)
  ;; Encode IEEE-754 double as 8 big-endian bytes; inverse of %bv-f64be's
  ;; sign/exponent/mantissa decomposition.
  (let ((bits (%f64->bits x)))
    (do ((shift 56 (- shift 8))) ((< shift 0))
      (write-u8 (bitwise-and (arithmetic-shift bits (- shift)) #xFF) port))))

(define (%f64->bits x)
  (cond
    ((= x 0.0) (if (eq? (/ 1.0 x) +inf.0) 0 #x8000000000000000))
    ((nan? x) #x7FF8000000000000)
    ((infinite? x) (if (> x 0) #x7FF0000000000000 #xFFF0000000000000))
    (else
     (let* ((sign (if (< x 0) 1 0))
            (ax (abs (inexact->exact x)))
            (e (let loop ((e 0) (v ax))
                 (cond ((>= v 2) (loop (+ e 1) (/ v 2)))
                       ((< v 1) (loop (- e 1) (* v 2)))
                       (else e))))
            (mant-frac (- (* ax (expt 2 (- e))) 1))    ; in [0, 1)
            (mant (round (* mant-frac (expt 2 52)))))
       (+ (arithmetic-shift sign 63)
          (arithmetic-shift (+ e 1023) 52)
          mant)))))

  )) ;; end begin, define-library
