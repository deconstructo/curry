;;; (curry sexagesimal) — Babylonian/sexagesimal convenience library
;;;
;;; Wraps the built-in sexagesimal primitives (number->string with 'neugebauer
;;; and 'cuneiform, string->number with same, current-number-notation) and
;;; provides pure-Scheme helpers for time, angle, and rational conversion.
;;;
;;; The sexagesimal system is base 60.  Curry represents it in two ways:
;;;
;;;   Neugebauer notation:  "1;30,0"  (1 degree 30 min 0 sec = 1.5)
;;;   Cuneiform Unicode:    𒁹 𒌋𒌋𒌋  (1 ; 30 in actual wedge-sign glyphs)
;;;
;;; History: Otto Neugebauer (1935) introduced the comma/semicolon notation
;;; for reading cuneiform mathematical tablets.  The sexagesimal system
;;; survives today in hours/minutes/seconds and degrees/arcminutes/arcseconds.

(define-library (curry sexagesimal)
  (import (scheme base))
  (export
    rational->sexagesimal sexagesimal->rational
    hms->seconds seconds->hms
    dms->degrees degrees->dms
    cuneiform->neugebauer neugebauer->cuneiform
    sex:ybc7289)
  (begin

;;; ---- Rational ↔ sexagesimal digit list ----

;;; Convert an exact rational (or integer) to a list of sexagesimal digit
;;; groups:  (integer-digit frac-digit-1 frac-digit-2 ...).
;;; The first element is the integer part (≥ 0), subsequent elements are
;;; fractional sexagesimal digits, each 0–59.  Negative values are first
;;; converted to their absolute value; the caller is responsible for sign.
;;; #:places controls maximum fractional digits (default: auto/exact).
(define (rational->sexagesimal r . opts)
  (let* ((neg? (< r 0))
         (r    (if neg? (- r) r))
         (places (let loop ((lst opts))
                   (cond ((null? lst) -1)
                         ((and (pair? lst) (eq? (car lst) #:places)
                               (pair? (cdr lst)))
                          (cadr lst))
                         (else (loop (cdr lst))))))
         ;; Use Neugebauer string of the absolute value as source of truth
         (s (if (>= places 0)
                (number->string r 'neugebauer #:places places)
                (number->string r 'neugebauer)))
         ;; Parse the Neugebauer string into digit lists
         (semi-pos (let loop ((i 0))
                     (cond ((>= i (string-length s)) #f)
                           ((char=? (string-ref s i) #\;) i)
                           (else (loop (+ i 1))))))
         (parse-comma-digits
          (lambda (substr)
            (let loop ((s substr) (acc '()))
              (let ((comma-pos (let lp ((i 0))
                                 (cond ((>= i (string-length s)) #f)
                                       ((char=? (string-ref s i) #\,) i)
                                       (else (lp (+ i 1)))))))
                (if comma-pos
                    (loop (substring s (+ comma-pos 1) (string-length s))
                          (cons (string->number (substring s 0 comma-pos)) acc))
                    (reverse (cons (string->number s) acc)))))))
         (int-str  (if semi-pos (substring s 0 semi-pos) s))
         (frac-str (if semi-pos (substring s (+ semi-pos 1) (string-length s)) ""))
         (int-digs  (parse-comma-digits int-str))
         (frac-digs (if (string=? frac-str "") '()
                        (parse-comma-digits frac-str)))
         (digs (append int-digs frac-digs)))
    (if neg? (cons (- (car digs)) (cdr digs)) digs)))

;;; Convert a sexagesimal digit list to an exact rational.
;;; The first element is the integer sexagesimal digit (integer part);
;;; all subsequent elements are fractional positions (each divided by 60^k).
(define (sexagesimal->rational lst)
  (if (null? lst)
      0
      (let loop ((rest (cdr lst))
                 (result (car lst))
                 (denom 60))
        (if (null? rest)
            result
            (loop (cdr rest)
                  (+ result (/ (car rest) denom))
                  (* denom 60))))))

;;; ---- Time (hours/minutes/seconds) ----

;;; Convert an (h m s) list to total seconds (exact if inputs are exact).
(define (hms->seconds hms)
  (let ((h (car hms)) (m (cadr hms)) (s (caddr hms)))
    (+ (* h 3600) (* m 60) s)))

;;; Convert total seconds to (h m s) list (integer arithmetic).
(define (seconds->hms total-seconds)
  (let* ((s (modulo total-seconds 60))
         (total-minutes (quotient total-seconds 60))
         (m (modulo total-minutes 60))
         (h (quotient total-minutes 60)))
    (list h m s)))

;;; ---- Angle (degrees/arcminutes/arcseconds) ----

;;; Convert a (deg arcmin arcsec) list to decimal degrees.
(define (dms->degrees dms)
  (let ((d (car dms)) (m (cadr dms)) (s (caddr dms)))
    (+ d (/ m 60) (/ s 3600))))

;;; Convert decimal degrees to (deg arcmin arcsec) list.
;;; Result is rounded to nearest arcsecond.
(define (degrees->dms deg)
  (let* ((d   (exact (floor deg)))
         (rem (* (- deg d) 60))
         (m   (exact (floor rem)))
         (s   (exact (round (* (- rem m) 60)))))
    ;; Handle overflow (s = 60 or m = 60)
    (cond
      ((>= s 60) (degrees->dms (+ d (/ (+ (* m 60) s) 3600))))
      (else (list d m s)))))

;;; ---- Cuneiform ↔ Neugebauer conversion ----

;;; Convert a cuneiform glyph string to Neugebauer notation string.
(define (cuneiform->neugebauer str)
  (let ((n (string->number str 'cuneiform)))
    (if n
        (number->string n 'neugebauer)
        (error "cuneiform->neugebauer: invalid cuneiform string" str))))

;;; Convert a Neugebauer notation string to cuneiform glyph string.
(define (neugebauer->cuneiform str)
  (let ((n (string->number str 'neugebauer)))
    (if n
        (number->string n 'cuneiform)
        (error "neugebauer->cuneiform: invalid Neugebauer string" str))))

;;; ---- YBC 7289 demo ----
;;; The Yale Babylonian Collection tablet YBC 7289 (c. 1800 BCE) shows
;;; the diagonal of a unit square annotated with the sexagesimal approximation
;;; of √2 to four places:  1;24,51,10 ≈ 1.41421296...
;;;
;;; (sex:ybc7289)  → 30547/21600  (the exact rational from the tablet)
(define (sex:ybc7289)
  (string->number "1;24,51,10" 'neugebauer))

  )) ;; end begin, define-library
