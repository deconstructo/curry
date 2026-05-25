;;; (curry getopt) — general-purpose command-line option parser
;;;
;;; Spec entry: (option name short long has-arg? default description)
;;;   name        — symbol used as key in result alist
;;;   short       — character (#\n) or #f
;;;   long        — string ("steps") or #f
;;;   has-arg?    — #t if option takes a value argument
;;;   default     — value when option is absent (#f for flags, string for values)
;;;   description — string shown in usage output
;;;
;;; Usage:
;;;   (define specs
;;;     (list (option 'steps #\n "steps" #t "40" "number of generations")
;;;           (option 'help  #\h "help"  #f #f   "show help")))
;;;
;;;   (define result (getopt (cdr command-line-args) specs))  ; skip script name
;;;   (opt-get    result 'steps)   ; => "40" or user value
;;;   (opt-rest   result)          ; => list of positional args
;;;   (opt-errors result)          ; => list of error strings (empty = ok)
;;;   (opt-ok?    result)          ; => #t if no errors
;;;   (opt-usage  "prog" specs)    ; => formatted usage string

;; ---- Option spec -------------------------------------------------------------

(define (option name short long has-arg? default description)
  (vector name short long has-arg? default description))

(define (opt-name        o) (vector-ref o 0))
(define (opt-short       o) (vector-ref o 1))   ; char or #f
(define (opt-long        o) (vector-ref o 2))   ; string or #f
(define (opt-has-arg?    o) (vector-ref o 3))   ; boolean
(define (opt-default     o) (vector-ref o 4))   ; default value
(define (opt-description o) (vector-ref o 5))   ; string

;; ---- Internal helpers --------------------------------------------------------

(define (string-index str ch)
  (let loop ((i 0))
    (cond ((= i (string-length str)) #f)
          ((char=? (string-ref str i) ch) i)
          (else (loop (+ i 1))))))

(define (find-by-short specs ch)
  (let loop ((s specs))
    (cond ((null? s) #f)
          ((let ((sc (opt-short (car s))))
             (and sc (char=? sc ch)))
           (car s))
          (else (loop (cdr s))))))

(define (find-by-long specs name)
  (let loop ((s specs))
    (cond ((null? s) #f)
          ((let ((ln (opt-long (car s))))
             (and ln (string=? ln name)))
           (car s))
          (else (loop (cdr s))))))

(define (str-join strs sep)
  (if (null? strs) ""
      (let loop ((rest (cdr strs)) (acc (car strs)))
        (if (null? rest) acc
            (loop (cdr rest) (string-append acc sep (car rest)))))))

;; ---- Main parser -------------------------------------------------------------

(define (getopt args specs)
  ;; Mutable alist: ((name . value) ... (-- . rest-list) (errors . error-list))
  (let* ((result (map (lambda (s) (cons (opt-name s) (opt-default s))) specs))
         (result (append result (list (cons '-- '()) (cons 'errors '())))))

    (define (get-cell key)
      (assq key result))

    (define (set-opt! name value)
      (let ((cell (get-cell name)))
        (if cell
            (set-cdr! cell value)
            (add-error! (string-append "unknown option: " (symbol->string name))))))

    (define (add-error! msg)
      (let ((cell (get-cell 'errors)))
        (set-cdr! cell (append (cdr cell) (list msg)))))

    (define (add-rest! arg)
      (let ((cell (get-cell '--)))
        (set-cdr! cell (append (cdr cell) (list arg)))))

    ;; Process a long option string (everything after --)
    (define (process-long raw next-args)
      (let* ((eq    (string-index raw #\=))
             (name  (if eq (substring raw 0 eq) raw))
             (ival  (and eq (substring raw (+ eq 1) (string-length raw))))
             (spec  (find-by-long specs name)))
        (cond
          ((not spec)
           (add-error! (string-append "unknown option: --" name))
           next-args)
          ((not (opt-has-arg? spec))
           (when ival
             (add-error! (string-append "--" name " does not take a value")))
           (set-opt! (opt-name spec) #t)
           next-args)
          (ival
           (set-opt! (opt-name spec) ival)
           next-args)
          ((null? next-args)
           (add-error! (string-append "--" name " requires a value"))
           '())
          (else
           (set-opt! (opt-name spec) (car next-args))
           (cdr next-args)))))

    ;; Process a cluster of short flags (chars after leading -)
    ;; Returns remaining args after consuming any value argument
    (define (process-short-cluster chars rest-args)
      (cond
        ((null? chars) rest-args)
        (else
         (let* ((ch   (car chars))
                (spec (find-by-short specs ch)))
           (cond
             ((not spec)
              (add-error! (string-append "unknown option: -" (string ch)))
              (process-short-cluster (cdr chars) rest-args))
             ((not (opt-has-arg? spec))
              (set-opt! (opt-name spec) #t)
              (process-short-cluster (cdr chars) rest-args))
             ;; Takes a value: rest of cluster is the value (-nVAL)
             ((pair? (cdr chars))
              (set-opt! (opt-name spec) (list->string (cdr chars)))
              rest-args)
             ;; Next separate argument is the value
             ((null? rest-args)
              (add-error! (string-append "-" (string ch) " requires a value"))
              '())
             (else
              (set-opt! (opt-name spec) (car rest-args))
              (cdr rest-args)))))))

    (let loop ((args args) (done #f))
      (cond
        ((null? args))

        (done
         (add-rest! (car args))
         (loop (cdr args) #t))

        ((string=? (car args) "--")
         (loop (cdr args) #t))

        ;; Long option: --foo or --foo=bar
        ((and (>= (string-length (car args)) 3)
              (char=? (string-ref (car args) 0) #\-)
              (char=? (string-ref (car args) 1) #\-))
         (let ((remaining (process-long
                            (substring (car args) 2 (string-length (car args)))
                            (cdr args))))
           (loop remaining #f)))

        ;; Short option(s): -x or -xyz or -xVAL
        ((and (>= (string-length (car args)) 2)
              (char=? (string-ref (car args) 0) #\-)
              (not (string=? (car args) "-")))
         (let* ((cluster (string->list (substring (car args) 1 (string-length (car args)))))
                (remaining (process-short-cluster cluster (cdr args))))
           (loop remaining #f)))

        ;; Positional
        (else
         (add-rest! (car args))
         (loop (cdr args) #f))))

    result))

;; ---- Accessors ---------------------------------------------------------------

(define (opt-get result name)
  (let ((cell (assq name result)))
    (and cell (cdr cell))))

(define (opt-rest result)
  (let ((cell (assq '-- result)))
    (if cell (cdr cell) '())))

(define (opt-errors result)
  (let ((cell (assq 'errors result)))
    (if cell (cdr cell) '())))

(define (opt-ok? result)
  (null? (opt-errors result)))

;; ---- Usage -------------------------------------------------------------------

(define (opt-usage program specs)
  (define col-width 22)
  (define (flag-str spec)
    (let ((s (and (opt-short spec)
                  (string-append "-" (string (opt-short spec))
                    (if (opt-has-arg? spec) " VAL" ""))))
          (l (and (opt-long spec)
                  (string-append "--" (opt-long spec)
                    (if (opt-has-arg? spec) "=VAL" "")))))
      (cond ((and s l) (string-append s ", " l))
            (s s)
            (l l)
            (else ""))))
  (define (pad str w)
    (let ((n (string-length str)))
      (if (>= n w) (string-append str " ")
          (string-append str (make-string (- w n) #\space)))))
  (define (default-note spec)
    (let ((d (opt-default spec)))
      (cond ((not d)       "")
            ((eq? d #t)    "")
            ((string? d)   (string-append " [" d "]"))
            (else          ""))))
  (string-append
    "Usage: " program " [options] [args...]\nOptions:\n"
    (apply string-append
      (map (lambda (spec)
             (string-append "  "
               (pad (flag-str spec) col-width)
               (opt-description spec)
               (default-note spec)
               "\n"))
           specs))))
