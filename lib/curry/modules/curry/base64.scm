;;; (curry base64) — RFC 4648 base64 encode/decode, pure Scheme.
;;;
;;; Port-based streaming transcoders (base64-encode-port/base64-decode-port)
;;; are the core; the string/bytevector procedures below are thin wrappers
;;; around them built on curry's own string/bytevector ports
;;; (open-input-string/open-input-bytevector/open-output-string/
;;; open-output-bytevector) — the same "port is the primitive, string/file
;;; are convenience layers on top" shape (curry fits)/(curry netcdf) already
;;; use for binary formats. No new dependency: (curry crypto) already offers
;;; base64-encode/base64-decode backed by OpenSSL, but this module exists so
;;; anything that wants base64 without pulling in the optional crypto
;;; module's build dependency (and its libssl requirement) has a pure-Scheme
;;; option — (curry naips) is the first consumer.
;;;
;;; Standard alphabet only (RFC 4648 §4: 'A'-'Z','a'-'z','0'-'9','+','/',
;;; '=' padding) — not the URL-safe variant (§5).

(define-library (curry base64)
  (import (scheme base))
  (export
    base64-encode-port base64-decode-port
    base64-encode base64-decode
    base64-encode-string base64-decode-string)
  (begin

;;; =========================================================================
;;; Alphabet
;;; =========================================================================

(define %b64-alphabet "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")

(define (%b64-char-value c)
  (let loop ((i 0))
    (cond ((>= i 64) #f)
          ((char=? c (string-ref %b64-alphabet i)) i)
          (else (loop (+ i 1))))))

;;; =========================================================================
;;; Encoding: binary input port -> textual output port
;;; =========================================================================

;; Encodes 1-3 bytes (bv, holding exactly n of them) as 4 base64 characters,
;; padding with '=' when n < 3 — the standard RFC 4648 3-bytes-to-4-chars
;; group, generalized to a short final group.
(define (%encode-group bv n out)
  (let* ((b0 (bytevector-u8-ref bv 0))
         (b1 (if (> n 1) (bytevector-u8-ref bv 1) 0))
         (b2 (if (> n 2) (bytevector-u8-ref bv 2) 0))
         (c0 (arithmetic-shift b0 -2))
         (c1 (bitwise-or (arithmetic-shift (bitwise-and b0 3) 4) (arithmetic-shift b1 -4)))
         (c2 (bitwise-or (arithmetic-shift (bitwise-and b1 15) 2) (arithmetic-shift b2 -6)))
         (c3 (bitwise-and b2 63)))
    (write-char (string-ref %b64-alphabet c0) out)
    (write-char (string-ref %b64-alphabet c1) out)
    (write-char (if (> n 1) (string-ref %b64-alphabet c2) #\=) out)
    (write-char (if (> n 2) (string-ref %b64-alphabet c3) #\=) out)))

;; (base64-encode-port in out) — reads bytes from binary input port `in`
;; 3 at a time (read-bytevector returns fewer only on the final, possibly
;; short, group — curry's string/bytevector/file ports are not partial-read
;; sockets, so a short read here always means "this is the last group"),
;; writing base64 text to textual output port `out`. No trailing newline.
(define (base64-encode-port in out)
  (let loop ()
    (let ((chunk (read-bytevector 3 in)))
      (unless (eof-object? chunk)
        (%encode-group chunk (bytevector-length chunk) out)
        (loop)))))

;;; =========================================================================
;;; Decoding: textual input port -> binary output port
;;; =========================================================================

;; Whitespace between groups (e.g. line-wrapped base64) is skipped; '='
;; padding characters contribute no bits and are otherwise ignored — the
;; group they terminate has already emitted every full byte it can by the
;; time a '=' is reached, so there's nothing left to do with it but skip it.
;; A char outside the alphabet and not whitespace/'=' is a hard error rather
;; than being silently dropped, so silently-corrupt input never decodes to
;; silently-wrong bytes.
(define (base64-decode-port in out)
  (let loop ((bits 0) (nbits 0))
    (let ((c (read-char in)))
      (cond
        ((eof-object? c) (if #f #f)) ; done; any leftover < 8 bits are padding, discarded
        ((memv c '(#\space #\tab #\newline #\return)) (loop bits nbits))
        ((char=? c #\=) (loop bits nbits))
        (else
         (let ((v (%b64-char-value c)))
           (if (not v)
               (error "base64-decode: invalid character" c)
               (let* ((bits2 (bitwise-or (arithmetic-shift bits 6) v))
                      (nbits2 (+ nbits 6)))
                 (if (>= nbits2 8)
                     (let* ((shift (- nbits2 8))
                            (byte (bitwise-and 255 (arithmetic-shift bits2 (- shift)))))
                       (write-u8 byte out)
                       (loop (bitwise-and bits2 (- (arithmetic-shift 1 shift) 1)) shift))
                     (loop bits2 nbits2))))))))))

;;; =========================================================================
;;; String / bytevector convenience wrappers
;;; =========================================================================

;; (base64-encode bv) -> string
(define (base64-encode bv)
  (let ((in (open-input-bytevector bv))
        (out (open-output-string)))
    (base64-encode-port in out)
    (get-output-string out)))

;; (base64-decode s) -> bytevector
(define (base64-decode s)
  (let ((in (open-input-string s))
        (out (open-output-bytevector)))
    (base64-decode-port in out)
    (get-output-bytevector out)))

;; (base64-encode-string s) -> string — encodes s's UTF-8 bytes.
(define (base64-encode-string s)
  (base64-encode (string->utf8 s)))

;; (base64-decode-string s) -> string — decodes s as base64, then interprets
;; the resulting bytes as UTF-8 text. For 7-bit-ASCII payloads (the common
;; case for embedded report/config text) this is exactly byte-for-byte the
;; same as the source; it's also correct for genuine multi-byte UTF-8,
;; unlike a naive byte->char mapping.
(define (base64-decode-string s)
  (utf8->string (base64-decode s)))

  )) ;; end begin, define-library
