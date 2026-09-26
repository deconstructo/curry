;;; SRFI-28: Basic Format Strings.
;;;
;;; https://srfi.schemers.org/srfi-28/ -- (format format-string obj ...)
;;; interprets a template string containing tilde-escape directives,
;;; substituting each with a stringified argument or literal text.
;;;
;;; Directives:
;;;   ~a   display the next argument (no quoting)
;;;   ~s   write the next argument (machine-readable)
;;;   ~%   literal newline
;;;   ~~   literal tilde
;;;   ~N@* positional jump (SRFI-29's own extension to this same format
;;;        engine, included here rather than kept SRFI-29-only, since
;;;        SRFI-29's own text describes it purely as a directive inside
;;;        a format string passed straight to THIS format procedure --
;;;        splitting it into a second, separate `format` that only
;;;        (srfi 29) callers could reach would make an ordinary format
;;;        string produced by localized-template behave differently
;;;        depending on which library's `format` happened to be in
;;;        scope, which defeats the entire point of a shared, portable
;;;        template mechanism). Sets which argument the NEXT ~a/~s
;;;        directive reads from -- by absolute, zero-based index --
;;;        without disturbing the normal left-to-right cursor: a plain
;;;        ~a right after a ~N@*~a jump still reads whatever the
;;;        sequential cursor was already pointing at, not N+1. Applies
;;;        to exactly one following value-directive, then the cursor
;;;        mechanism resumes as if the jump had never happened.
;;;
;;; Errors (unrecognized directive, a trailing bare ~ at end of string,
;;; too few arguments for a ~a/~s/~N@*, or an out-of-range N) all raise
;;; a clear condition rather than silently misformatting or reading
;;; garbage -- SRFI-28's own text lists exactly these as error
;;; conditions, not implementation-defined behavior.

(define-library (srfi s28 format)
  (import (scheme base))
  (export format)
  (begin

    (define (%fmt-ref args-vec i who)
      (if (or (< i 0) (>= i (vector-length args-vec)))
          (error (string-append who ": not enough arguments for format directive") i)
          (vector-ref args-vec i)))

    ;; Scans a digit run starting at index j, returns (values n after)
    ;; where n is the parsed integer and after is the index just past the
    ;; last digit. Requires at least one digit (caller only invokes this
    ;; when string-ref fmt-string j is already known to be a digit).
    (define (%fmt-scan-digits fmt-string j len)
      (let loop ((j j) (n 0))
        (if (and (< j len) (char-numeric? (string-ref fmt-string j)))
            (loop (+ j 1) (+ (* n 10) (- (char->integer (string-ref fmt-string j)) (char->integer #\0))))
            (values n j))))

    ;; Parses a ~N@*~a or ~N@*~s directive starting at the digit right
    ;; after the initial ~ (index i+1). Returns (values result-string
    ;; next-index) -- next-index points just past the whole ~N@*~a/~s
    ;; run, ready for the main loop to resume from.
    (define (%fmt-positional fmt-string i len args)
      (call-with-values
        (lambda () (%fmt-scan-digits fmt-string (+ i 1) len))
        (lambda (n after-digits)
          (if (or (>= (+ after-digits 1) len)
                  (not (char=? (string-ref fmt-string after-digits) #\@))
                  (not (char=? (string-ref fmt-string (+ after-digits 1)) #\*)))
              (error "format: malformed positional directive (expected ~N@*)" fmt-string)
              (let ((after-star (+ after-digits 2)))
                (if (or (>= (+ after-star 1) len) (not (char=? (string-ref fmt-string after-star) #\~)))
                    (error "format: ~N@* must be followed by ~a or ~s" fmt-string)
                    (let ((e (string-ref fmt-string (+ after-star 1))))
                      (cond
                        ((or (char=? e #\a) (char=? e #\A))
                         (values (%datum->str (%fmt-ref args n "format") #f) (+ after-star 2)))
                        ((or (char=? e #\s) (char=? e #\S))
                         (values (%datum->str (%fmt-ref args n "format") #t) (+ after-star 2)))
                        (else (error "format: ~N@* must be followed by ~a or ~s" fmt-string))))))))))

    (define (%datum->str obj write?)
      (let ((p (open-output-string)))
        (if write? (write obj p) (display obj p))
        (get-output-string p)))

    (define (format fmt-string . objs)
      (let ((args (list->vector objs))
            (out (open-output-string))
            (len (string-length fmt-string)))
        (let loop ((i 0) (cursor 0))
          (if (>= i len)
              (get-output-string out)
              (let ((c (string-ref fmt-string i)))
                (if (not (char=? c #\~))
                    (begin (write-char c out) (loop (+ i 1) cursor))
                    (if (>= (+ i 1) len)
                        (error "format: incomplete escape sequence at end of format string")
                        (let ((d (string-ref fmt-string (+ i 1))))
                          (cond
                            ((or (char=? d #\a) (char=? d #\A))
                             (display (%fmt-ref args cursor "format") out)
                             (loop (+ i 2) (+ cursor 1)))
                            ((or (char=? d #\s) (char=? d #\S))
                             (write (%fmt-ref args cursor "format") out)
                             (loop (+ i 2) (+ cursor 1)))
                            ((char=? d #\%) (newline out) (loop (+ i 2) cursor))
                            ((char=? d #\~) (write-char #\~ out) (loop (+ i 2) cursor))
                            ((char-numeric? d)
                             (call-with-values
                               (lambda () (%fmt-positional fmt-string i len args))
                               (lambda (text next-i)
                                 (display text out)
                                 (loop next-i cursor))))
                            (else
                             (error "format: unrecognized directive" (string #\~ d))))))))))))))
