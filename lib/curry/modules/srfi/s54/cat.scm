(define-library (srfi s54 cat)
  (import (scheme base) (scheme write))
  (export cat)
  (begin

    ; SRFI-54 is unusually large and its optional arguments interact in
    ; genuinely exotic ways for numbers (radix combined with inexactness
    ; produces dual "#i#o..." prefixes; precision applied to a complex
    ; number formats real and imaginary parts independently). This
    ; implementation covers the well-defined, commonly-useful core fully
    ; -- every non-number argument (writer/pipe/take/width/char/port/
    ; string/converter) and every number argument for real numbers in
    ; decimal radix (exactness/sign/precision/separator/width/char/port/
    ; string) -- and treats non-decimal radix as scoped to EXACT INTEGERS
    ; only (the one case the spec itself gives an unambiguous single
    ; example for: `(cat #x123 'octal 'sign) => "#o+443"`). Combining
    ; non-decimal radix with precision raises, matching the spec's own
    ; stated error for that combination; combining non-decimal radix with
    ; a non-integer, or precision with a complex number, are not
    ; implemented (documented in docs/reference/srfi/s54.md) rather than
    ; guessed at.

    ; ── argument classification ──────────────────────────────────────────
    ; Every optional argument to `cat` is distinguished purely by its
    ; shape -- order doesn't matter, per the spec. Converter (a genuinely
    ; dotted pair of two procedures) is checked before pipe/separator/take
    ; (proper lists) since `list?` alone already excludes it.

    (define (%cat-classify arg)
      (cond
        ((and (pair? arg) (list? arg) (procedure? (car arg))) (cons 'pipe arg))
        ((and (pair? arg) (list? arg) (char? (car arg))) (cons 'separator arg))
        ((and (pair? arg) (list? arg) (integer? (car arg)) (exact? (car arg))
              (or (null? (cdr arg)) (and (integer? (cadr arg)) (exact? (cadr arg)) (null? (cddr arg)))))
         (cons 'take arg))
        ((and (pair? arg) (procedure? (car arg)) (procedure? (cdr arg))) (cons 'converter arg))
        ((procedure? arg) (cons 'writer arg))
        ((eq? arg 'exact) (cons 'exactness 'exact))
        ((eq? arg 'inexact) (cons 'exactness 'inexact))
        ((memq arg '(binary octal decimal hexadecimal)) (cons 'radix arg))
        ((eq? arg 'sign) (cons 'sign #t))
        ((char? arg) (cons 'char arg))
        ((string? arg) (cons 'string arg))
        ((boolean? arg) (cons 'port arg))
        ((output-port? arg) (cons 'port arg))
        ((and (number? arg) (exact-integer? arg)) (cons 'width arg))
        ((and (number? arg) (inexact? arg) (integer? arg)) (cons 'precision arg))
        (else (error "cat: unrecognized optional argument" arg))))

    ; opts is an alist; later same-category args override earlier ones,
    ; except 'string, which accumulates (each appended in the order given).
    (define (%cat-parse-args args)
      (let loop ((args args) (opts '()) (strings '()))
        (if (null? args)
            (cons (cons 'strings (reverse strings)) opts)
            (let ((c (%cat-classify (car args))))
              (if (eq? (car c) 'string)
                  (loop (cdr args) opts (cons (cdr c) strings))
                  (loop (cdr args) (cons c (%cat-remove opts (car c))) strings))))))

    (define (%cat-remove alist key)
      (filter (lambda (p) (not (eq? (car p) key))) alist))

    (define (%opt opts key default)
      (let ((p (assq key opts))) (if p (cdr p) default)))

    ; ── non-number formatting: writer -> pipe -> take ──────────────────────

    (define (%self-evaluating? x) (or (number? x) (string? x) (char? x) (boolean? x)))

    (define (%cat-default-writer obj port)
      (if (%self-evaluating? obj) (display obj port) (write obj port)))

    (define (%cat-write-to-string obj writer)
      (let ((out (open-output-string)))
        ((or writer %cat-default-writer) obj out)
        (get-output-string out)))

    (define (%cat-take str spec)
      (let* ((len (string-length str))
             (n (car spec))
             (m (if (pair? (cdr spec)) (cadr spec) 0))
             (left (if (>= n 0) (substring str 0 (min n len)) (substring str (min (abs n) len) len)))
             (right (if (>= m 0)
                        (substring str (max 0 (- len m)) len)
                        (substring str 0 (max 0 (- len (abs m)))))))
        (string-append left right)))

    (define (%cat-format-other obj opts)
      (let* ((writer (%opt opts 'writer #f))
             (s (%cat-write-to-string obj writer))
             (pipe (%opt opts 'pipe #f))
             (s (if pipe (fold-pipe s pipe) s))
             (take (%opt opts 'take #f)))
        (if take (%cat-take s take) s)))

    (define (fold-pipe s procs)
      (if (null? procs) s (fold-pipe ((car procs) s) (cdr procs))))

    ; ── number formatting: exactness -> radix -> precision -> separator
    ;    -> sign ─────────────────────────────────────────────────────────

    (define (%group-from-right digits n)
      (let ((len (string-length digits)))
        (if (<= len n)
            digits
            (string-append (%group-from-right (substring digits 0 (- len n)) n)
                            ","
                            (substring digits (- len n) len)))))

    (define (%group-from-left digits n)
      (let ((len (string-length digits)))
        (if (<= len n)
            digits
            (string-append (substring digits 0 n) "," (%group-from-left (substring digits n len) n)))))

    (define (%apply-separator magnitude sep-char sep-n)
      ; magnitude has no sign and no #e/radix prefix -- just digits, and
      ; optionally one "." -- group the integer part from the right and
      ; the fraction part from the left, mirroring outward from the point.
      (let ((dot (%string-index magnitude #\.)))
        (let* ((int-part  (if dot (substring magnitude 0 dot) magnitude))
               (frac-part (if dot (substring magnitude (+ dot 1) (string-length magnitude)) #f))
               (grouped-int  (%group-from-right int-part sep-n))
               (grouped-frac (and frac-part (%group-from-left frac-part sep-n))))
          (%replace-char
            (if frac-part (string-append grouped-int "." grouped-frac) grouped-int)
            #\, sep-char))))

    (define (%string-index s ch)
      (let ((n (string-length s)))
        (let loop ((i 0)) (cond ((>= i n) #f) ((char=? (string-ref s i) ch) i) (else (loop (+ i 1)))))))

    (define (%replace-char s from to)
      (if (char=? from to)
          s
          (list->string (map (lambda (c) (if (char=? c from) to c)) (string->list s)))))

    (define (%radix->number radix) (case radix ((binary) 2) ((octal) 8) ((hexadecimal) 16) (else 10)))
    (define (%radix->prefix radix) (case radix ((binary) "#b") ((octal) "#o") ((hexadecimal) "#x") (else "")))

    ; Rounds |x| to `precision` decimal digits and returns the unsigned
    ; magnitude as a string with exactly that many fractional digits (no
    ; sign, no #e prefix -- those are added by the caller).
    ;
    ; An inexact magnitude is rounded from its OWN canonical decimal
    ; string (via number->string), not by scaling its exact binary value
    ; -- those two disagree right at a rounding boundary. 129.985's actual
    ; double value is a hair ABOVE 129.985 (129.985 + 3/2199023255552),
    ; so scaling-then-rounding that exact value rounds UP to 129.99; but
    ; the spec's own worked example expects 129.98 (round-half-to-even
    ; applied to the digits "129.985" as printed, where the discarded "5"
    ; sits exactly on the boundary and 8 is already even). An exact
    ; magnitude (e.g. 1/3) has no such ambiguity -- its value truly IS
    ; exact, so scaling it and rounding is both correct and simplest.
    (define (%format-precision magnitude precision)
      (let* ((scaled
               (if (exact? magnitude)
                   (round (* magnitude (expt 10 precision)))
                   (let* ((s (number->string magnitude))
                          (dot (%string-index s #\.))
                          (digit-str (if dot (string-append (substring s 0 dot) (substring s (+ dot 1) (string-length s))) s))
                          (frac-len (if dot (- (string-length s) dot 1) 0))
                          (digits-int (string->number digit-str)))
                     (if (<= frac-len precision)
                         (* digits-int (expt 10 (- precision frac-len)))
                         (round (/ digits-int (expt 10 (- frac-len precision))))))))
             (digits (number->string scaled)))
        (if (= precision 0)
            ; the spec keeps the decimal point even with zero digits after
            ; it (verified against a worked example that immediately
            ; take's off exactly one trailing character, the "."):
            ; (cat (cat 129.995 0.) '(0 -1)) => "130", so (cat 129.995 0.)
            ; itself must be "130." for that take to land on the point.
            (string-append digits ".")
            (let* ((digits (if (<= (string-length digits) precision)
                                (string-append (make-string (- (+ precision 1) (string-length digits)) #\0) digits)
                                digits))
                   (len (string-length digits)))
              (string-append (substring digits 0 (- len precision)) "." (substring digits (- len precision) len))))))

    (define (%cat-format-number obj opts)
      (let* ((exactness (%opt opts 'exactness #f))
             (obj (cond ((eq? exactness 'exact) (exact obj))
                        ((eq? exactness 'inexact) (inexact obj))
                        (else obj)))
             (radix (%opt opts 'radix 'decimal))
             (precision (%opt opts 'precision #f))
             (separator (%opt opts 'separator #f))
             (force-sign (%opt opts 'sign #f))
             (negative? (< (%cat-real-part obj) 0)))
        (when (and precision (not (eq? radix 'decimal)))
          (error "cat: non-decimal cannot have a decimal point"))
        (let* ((magnitude
                 (if (not (eq? radix 'decimal))
                     (begin
                       (unless (exact-integer? obj)
                         (error "cat: non-decimal radix only supports exact integers" obj))
                       (number->string (abs obj) (%radix->number radix)))
                     (if precision
                         (%format-precision (abs obj) (inexact->exact (round (abs precision))))
                         (let ((s (number->string (abs obj))))
                           ; number->string on an exact rational with no
                           ; fractional part (e.g. after (exact 4.0) => 4)
                           ; never has a decimal point to separate on;
                           ; that's fine, %apply-separator handles it.
                           s))))
               (magnitude (if separator
                              (%apply-separator magnitude
                                                 (car separator)
                                                 (if (pair? (cdr separator)) (cadr separator) 3))
                              magnitude))
               (add-#e? (and precision (>= precision 0) (exact? obj) (eq? radix 'decimal)))
               (sign-str (cond (negative? "-") (force-sign "+") (else ""))))
          (string-append (if (not (eq? radix 'decimal)) (%radix->prefix radix) "")
                          (if add-#e? "#e" "")
                          sign-str
                          magnitude))))

    ; `separator` is stored as (separator . (char [n])) by %cat-classify;
    ; %apply-separator above is called with the raw (cdr separator) shape
    ; via a small adapter so the (char n) destructuring stays in one place.
    (define (%cat-real-part obj) (if (real? obj) obj (real-part obj)))

    ; ── the width/char/port/string wrapper shared by both paths ────────

    (define (%cat-pad str width char)
      (let* ((w (abs width)) (len (string-length str)))
        (if (<= w len)
            str
            (let ((padding (make-string (- w len) char)))
              (cond
                ; zero-padding a number respects any leading sign/#e/#b/
                ; #o/#x prefix run -- the padding goes AFTER it, not
                ; before, e.g. "#e+129.00" zero-padded to width 10 becomes
                ; "#e+0129.00", not "0#e+129.00".
                ((and (char=? char #\0) (>= width 0))
                 (let ((cut (%cat-prefix-run-length str)))
                   (string-append (substring str 0 cut) padding (substring str cut len))))
                ((>= width 0) (string-append padding str))
                (else (string-append str padding)))))))

    (define (%cat-prefix-run-length str)
      (let ((len (string-length str)))
        (let loop ((i 0))
          (cond
            ((>= i len) i)
            ((memv (string-ref str i) '(#\# #\e #\i #\b #\o #\x #\+ #\-)) (loop (+ i 1)))
            (else i)))))

    (define (cat obj . args)
      (let* ((opts (%cat-parse-args args))
             (converter (%opt opts 'converter #f))
             (formatted
               (cond
                 (converter (if ((car converter) obj) ((cdr converter) obj) (%cat-format-fallback obj opts)))
                 ((number? obj) (%cat-format-number obj opts))
                 (else (%cat-format-other obj opts))))
             (width (%opt opts 'width 0))
             (char (%opt opts 'char #\space))
             (padded (%cat-pad formatted width char))
             (strings (%opt opts 'strings '()))
             (result (apply string-append padded strings))
             (port (%opt opts 'port #f)))
        (cond
          ((eq? port #t) (display result (current-output-port)) result)
          ((output-port? port) (display result port) result)
          (else result))))

    ; A converter whose predicate doesn't match `obj` falls back to the
    ; normal (number vs. other) formatting -- the spec doesn't specify
    ; this case explicitly, but it's the only reasonable behavior short of
    ; raising, and matches "width/char/port/string remain effective" only
    ; describing what happens once a converter DOES match.
    (define (%cat-format-fallback obj opts)
      (if (number? obj) (%cat-format-number obj opts) (%cat-format-other obj opts)))))
