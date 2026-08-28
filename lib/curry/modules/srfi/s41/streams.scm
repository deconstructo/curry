;;; SRFI-41: Streams.
;;;
;;; The reference implementation builds its own memoizing lazy-promise
;;; machinery from scratch (a record type wrapping an eager/lazy tagged
;;; thunk) because SRFI-41 predates R7RS's delay-force. curry already
;;; has R7RS delay/delay-force/force natively -- with the exact
;;; iterative, stack-safe chain-flattening SRFI-41's own spec calls
;;; "vitally critical" already verified correct (a 3+-level delay-force
;;; chain-flattening bug was found and fixed here independently of this
;;; SRFI, see CHANGELOG v1.22.0) -- so this port replaces the from-
;;; scratch promise machinery with curry's own, rather than porting it.
;;;
;;; Representation: a stream IS a curry promise. stream-null is an
;;; already-forced promise holding a unique sentinel; stream-cons
;;; wraps its car in an ordinary memoizing (delay ...) and its cdr in
;;; (delay-force ...) -- ordinary delay would NOT be safe for the cdr,
;;; since a long chain of nested delay-forces (e.g. stream-filter's own
;;; recursive definition) is exactly the case delay-force's iterative
;;; flattening exists for; plain delay would grow the stack one frame
;;; per element instead.
;;;
;;; Deliberate deviation from the spec, clearly noted: reusing curry's
;;; raw promise type directly (rather than wrapping it in a distinct
;;; record, the way the reference implementation's own stream-type
;;; does) means stream? is really "is this a promise", not "is this
;;; specifically a stream" -- (stream? (delay 5)) is #t here, where the
;;; reference implementation would say #f. This is unavoidable: wrapping
;;; the promise in a separate record for stronger typing would break
;;; delay-force's own chain-flattening (it only continues chaining when
;;; the value it unwraps is itself a promise; a custom record wouldn't
;;; qualify), defeating the entire point of using delay-force for the
;;; cdr. In exchange, this is a strictly more capable representation on
;;; the one axis that actually matters for a stream library: no stack
;;; growth walking an arbitrarily long stream.
;;;
;;; stream-match ported using curry's own established syntax-rules
;;; wildcard idiom (a literal `_` in the literals list, matched by
;;; position before the general pattern-variable clause) -- the same
;;; technique (curry matchable) already uses -- rather than the
;;; reference implementation's syntax-case/identifier?/free-identifier=?
;;; version, since curry's macro system is syntax-rules only.
(define-library (srfi s41 streams)
  (import (scheme base) (srfi s1 lists))
  (export
    stream-null stream-cons stream? stream-null? stream-pair?
    stream-car stream-cdr stream-lambda
    define-stream list->stream stream stream->list
    stream-filter stream-map stream-from stream-take
    stream-append stream-concat stream-constant
    stream-drop stream-drop-while stream-fold stream-for-each
    stream-iterate stream-length stream-of stream-of-aux
    stream-range stream-ref stream-reverse stream-scan
    stream-take-while stream-unfold stream-unfolds stream-zip
    stream-let port->stream stream-match
    stream-match-test stream-match-pattern
    %make-stream-pare) ; not public API -- stream-cons's expansion
                       ; references it, and curry's syntax-rules isn't
                       ; hygienic across define-library boundaries
  (begin

    ;; ---- primitives ----

    (define %stream-null-tag (list 'stream-null))
    (define stream-null (make-promise %stream-null-tag))

    (define (stream? obj) (promise? obj))
    (define (stream-null? obj) (eqv? (force obj) %stream-null-tag))

    (define-record-type <stream-pare>
      (%make-stream-pare kar kdr) %stream-pare?
      (kar %stream-pare-kar)
      (kdr %stream-pare-kdr))

    (define (stream-pair? obj) (and (promise? obj) (%stream-pare? (force obj))))

    (define-syntax stream-cons
      (syntax-rules ()
        ((_ obj strm) (make-promise (%make-stream-pare (delay obj) (delay-force strm))))))

    (define (stream-car strm)
      (cond
        ((not (stream? strm)) (error "stream-car: non-stream argument" strm))
        ((stream-null? strm) (error "stream-car: null stream" strm))
        (else (force (%stream-pare-kar (force strm))))))

    (define (stream-cdr strm)
      (cond
        ((not (stream? strm)) (error "stream-cdr: non-stream argument" strm))
        ((stream-null? strm) (error "stream-cdr: null stream" strm))
        (else (%stream-pare-kdr (force strm)))))

    (define-syntax stream-lambda
      (syntax-rules ()
        ((_ formals body0 body1 ...) (lambda formals (delay-force (let () body0 body1 ...))))))

    ;; ---- derived ----

    (define-syntax define-stream
      (syntax-rules ()
        ((_ (name . formal) body0 body1 ...) (define name (stream-lambda formal body0 body1 ...)))))

    (define (list->stream objs)
      (define %list->stream
        (stream-lambda (objs)
          (if (null? objs) stream-null (stream-cons (car objs) (%list->stream (cdr objs))))))
      (if (not (list? objs)) (error "list->stream: non-list argument" objs) (%list->stream objs)))

    (define-syntax stream
      (syntax-rules ()
        ((_) stream-null)
        ((_ x y ...) (stream-cons x (stream y ...)))))

    ;; curry has no case-lambda (see e.g. lib/curry/modules/curry/schematic/
    ;; format.scm's own note on the same gap) -- dispatch on argument count
    ;; via a plain rest-arg, matching the reference implementation's own
    ;; approach exactly (it doesn't use case-lambda either).
    (define (stream->list . args)
      (let ((n (if (= 1 (length args)) #f (car args)))
            (strm (if (= 1 (length args)) (car args) (cadr args))))
        (cond
          ((not (stream? strm)) (error "stream->list: non-stream argument" strm))
          ((and n (not (integer? n))) (error "stream->list: non-integer count" n))
          ((and n (negative? n)) (error "stream->list: negative count" n))
          (else
           (let loop ((n (if n n -1)) (strm strm))
             (if (or (zero? n) (stream-null? strm))
                 '()
                 (cons (stream-car strm) (loop (- n 1) (stream-cdr strm)))))))))

    (define (stream-filter pred? strm)
      (define %stream-filter
        (stream-lambda (strm)
          (cond
            ((stream-null? strm) stream-null)
            ((pred? (stream-car strm)) (stream-cons (stream-car strm) (%stream-filter (stream-cdr strm))))
            (else (%stream-filter (stream-cdr strm))))))
      (cond
        ((not (procedure? pred?)) (error "stream-filter: non-procedural argument" pred?))
        ((not (stream? strm)) (error "stream-filter: non-stream argument" strm))
        (else (%stream-filter strm))))

    (define (stream-map proc . strms)
      (define %stream-map
        (stream-lambda (strms)
          (if (any stream-null? strms)
              stream-null
              (stream-cons (apply proc (map stream-car strms)) (%stream-map (map stream-cdr strms))))))
      (cond
        ((not (procedure? proc)) (error "stream-map: non-procedural argument" proc))
        ((null? strms) (error "stream-map: no stream arguments"))
        ((any (lambda (x) (not (stream? x))) strms) (error "stream-map: non-stream argument"))
        (else (%stream-map strms))))

    (define (stream-from first . step)
      (define %stream-from
        (stream-lambda (first delta)
          (stream-cons first (%stream-from (+ first delta) delta))))
      (let ((delta (if (null? step) 1 (car step))))
        (cond
          ((not (number? first)) (error "stream-from: non-numeric starting value" first))
          ((not (number? delta)) (error "stream-from: non-numeric step" delta))
          (else (%stream-from first delta)))))

    (define (stream-take n strm)
      (define %stream-take
        (stream-lambda (n strm)
          (if (or (stream-null? strm) (zero? n))
              stream-null
              (stream-cons (stream-car strm) (%stream-take (- n 1) (stream-cdr strm))))))
      (cond
        ((not (stream? strm)) (error "stream-take: non-stream argument" strm))
        ((not (integer? n)) (error "stream-take: non-integer argument" n))
        ((negative? n) (error "stream-take: negative argument" n))
        (else (%stream-take n strm))))

    ;; Not in the spec's own reference-implementation appendix -- derived
    ;; here by mirroring stream-concat's own structure directly below
    ;; (a plain Scheme list of stream arguments, not a stream-of-streams)
    ;; and verified by hand-tracing a two-stream example.
    (define (stream-append . strms)
      (define %stream-append
        (stream-lambda (strms)
          (cond
            ((null? strms) stream-null)
            ((not (stream? (car strms))) (error "stream-append: non-stream argument"))
            ((stream-null? (car strms)) (%stream-append (cdr strms)))
            (else (stream-cons (stream-car (car strms))
                                (%stream-append (cons (stream-cdr (car strms)) (cdr strms))))))))
      (%stream-append strms))

    (define (stream-concat strms)
      (define %stream-concat
        (stream-lambda (strms)
          (cond
            ((stream-null? strms) stream-null)
            ((not (stream? (stream-car strms))) (error "stream-concat: non-stream object in input stream"))
            ((stream-null? (stream-car strms)) (%stream-concat (stream-cdr strms)))
            (else (stream-cons (stream-car (stream-car strms))
                                (%stream-concat (stream-cons (stream-cdr (stream-car strms)) (stream-cdr strms))))))))
      (if (not (stream? strms)) (error "stream-concat: non-stream argument" strms) (%stream-concat strms)))

    (define stream-constant
      (stream-lambda objs
        (cond
          ((null? objs) stream-null)
          ((null? (cdr objs)) (stream-cons (car objs) (stream-constant (car objs))))
          (else (stream-cons (car objs) (apply stream-constant (append (cdr objs) (list (car objs)))))))))

    (define (stream-drop n strm)
      (define %stream-drop
        (stream-lambda (n strm)
          (if (or (zero? n) (stream-null? strm)) strm (%stream-drop (- n 1) (stream-cdr strm)))))
      (cond
        ((not (integer? n)) (error "stream-drop: non-integer argument" n))
        ((negative? n) (error "stream-drop: negative argument" n))
        ((not (stream? strm)) (error "stream-drop: non-stream argument" strm))
        (else (%stream-drop n strm))))

    (define (stream-drop-while pred? strm)
      (define %stream-drop-while
        (stream-lambda (strm)
          (if (and (stream-pair? strm) (pred? (stream-car strm)))
              (%stream-drop-while (stream-cdr strm))
              strm)))
      (cond
        ((not (procedure? pred?)) (error "stream-drop-while: non-procedural argument" pred?))
        ((not (stream? strm)) (error "stream-drop-while: non-stream argument" strm))
        (else (%stream-drop-while strm))))

    (define (stream-fold proc base strm)
      (cond
        ((not (procedure? proc)) (error "stream-fold: non-procedural argument" proc))
        ((not (stream? strm)) (error "stream-fold: non-stream argument" strm))
        (else (let loop ((base base) (strm strm))
                (if (stream-null? strm) base (loop (proc base (stream-car strm)) (stream-cdr strm)))))))

    (define (stream-for-each proc . strms)
      (define (%stream-for-each strms)
        (if (not (any stream-null? strms))
            (begin (apply proc (map stream-car strms)) (%stream-for-each (map stream-cdr strms)))))
      (cond
        ((not (procedure? proc)) (error "stream-for-each: non-procedural argument" proc))
        ((null? strms) (error "stream-for-each: no stream arguments"))
        ((any (lambda (x) (not (stream? x))) strms) (error "stream-for-each: non-stream argument"))
        (else (%stream-for-each strms))))

    (define (stream-iterate proc base)
      (define %stream-iterate
        (stream-lambda (base) (stream-cons base (%stream-iterate (proc base)))))
      (if (not (procedure? proc)) (error "stream-iterate: non-procedural argument" proc) (%stream-iterate base)))

    (define (stream-length strm)
      (if (not (stream? strm))
          (error "stream-length: non-stream argument" strm)
          (let loop ((len 0) (strm strm))
            (if (stream-null? strm) len (loop (+ len 1) (stream-cdr strm))))))

    (define-syntax stream-of
      (syntax-rules ()
        ((_ expr rest ...) (stream-of-aux expr stream-null rest ...))))

    (define-syntax stream-of-aux
      (syntax-rules (in is)
        ((_ expr base) (stream-cons expr base))
        ((_ expr base (var in strm) rest ...)
         (stream-let %loop ((s strm))
           (if (stream-null? s)
               base
               (let ((var (stream-car s)))
                 (stream-of-aux expr (%loop (stream-cdr s)) rest ...)))))
        ((_ expr base (var is exp) rest ...)
         (let ((var exp)) (stream-of-aux expr base rest ...)))
        ((_ expr base pred? rest ...)
         (if pred? (stream-of-aux expr base rest ...) base))))

    (define (stream-range first past . step)
      (define %stream-range
        (stream-lambda (first past delta lt?)
          (if (lt? first past)
              (stream-cons first (%stream-range (+ first delta) past delta lt?))
              stream-null)))
      (cond
        ((not (number? first)) (error "stream-range: non-numeric starting number" first))
        ((not (number? past)) (error "stream-range: non-numeric ending number" past))
        (else
         (let ((delta (cond ((pair? step) (car step)) ((< first past) 1) (else -1))))
           (if (not (number? delta))
               (error "stream-range: non-numeric step size" delta)
               (let ((lt? (if (< 0 delta) < >)))
                 (%stream-range first past delta lt?)))))))

    (define (stream-ref strm n)
      (cond
        ((not (stream? strm)) (error "stream-ref: non-stream argument" strm))
        ((not (and (integer? n) (>= n 0))) (error "stream-ref: invalid index" n))
        (else (let loop ((strm strm) (n n))
                (cond
                  ((stream-null? strm) (error "stream-ref: index out of range"))
                  ((zero? n) (stream-car strm))
                  (else (loop (stream-cdr strm) (- n 1))))))))

    (define (stream-reverse strm)
      (define %stream-reverse
        (stream-lambda (strm rev)
          (if (stream-null? strm) rev (%stream-reverse (stream-cdr strm) (stream-cons (stream-car strm) rev)))))
      (if (not (stream? strm)) (error "stream-reverse: non-stream argument" strm) (%stream-reverse strm stream-null)))

    (define (stream-scan proc base strm)
      (define %stream-scan
        (stream-lambda (base strm)
          (if (stream-null? strm)
              (stream base)
              (stream-cons base (%stream-scan (proc base (stream-car strm)) (stream-cdr strm))))))
      (cond
        ((not (procedure? proc)) (error "stream-scan: non-procedural argument" proc))
        ((not (stream? strm)) (error "stream-scan: non-stream argument" strm))
        (else (%stream-scan base strm))))

    (define (stream-take-while pred? strm)
      (define %stream-take-while
        (stream-lambda (strm)
          (cond
            ((stream-null? strm) stream-null)
            ((pred? (stream-car strm)) (stream-cons (stream-car strm) (%stream-take-while (stream-cdr strm))))
            (else stream-null))))
      (cond
        ((not (stream? strm)) (error "stream-take-while: non-stream argument" strm))
        ((not (procedure? pred?)) (error "stream-take-while: non-procedural argument" pred?))
        (else (%stream-take-while strm))))

    (define (stream-unfold mapper pred? generator base)
      (define %stream-unfold
        (stream-lambda (base)
          (if (pred? base)
              (stream-cons (mapper base) (%stream-unfold (generator base)))
              stream-null)))
      (cond
        ((not (procedure? mapper)) (error "stream-unfold: non-procedural mapper" mapper))
        ((not (procedure? pred?)) (error "stream-unfold: non-procedural pred?" pred?))
        ((not (procedure? generator)) (error "stream-unfold: non-procedural generator" generator))
        (else (%stream-unfold base))))

    (define (stream-unfolds gen seed)
      (define (%len-values gen seed)
        (call-with-values (lambda () (gen seed)) (lambda vs (- (length vs) 1))))
      (define %unfold-result-stream
        (stream-lambda (gen seed)
          (call-with-values
           (lambda () (gen seed))
           (lambda (next . results) (stream-cons results (%unfold-result-stream gen next))))))
      (define %result-stream->output-stream
        (stream-lambda (result-stream i)
          (let ((result (list-ref (stream-car result-stream) (- i 1))))
            (cond
              ((pair? result) (stream-cons (car result) (%result-stream->output-stream (stream-cdr result-stream) i)))
              ((not result) (%result-stream->output-stream (stream-cdr result-stream) i))
              ((null? result) stream-null)
              (else (error "stream-unfolds: can't happen"))))))
      (define (%result-stream->output-streams result-stream)
        (let loop ((i (%len-values gen seed)) (outputs '()))
          (if (zero? i)
              (apply values outputs)
              (loop (- i 1) (cons (%result-stream->output-stream result-stream i) outputs)))))
      (if (not (procedure? gen))
          (error "stream-unfolds: non-procedural argument" gen)
          (%result-stream->output-streams (%unfold-result-stream gen seed))))

    (define (stream-zip . strms)
      (define %stream-zip
        (stream-lambda (strms)
          (if (any stream-null? strms)
              stream-null
              (stream-cons (map stream-car strms) (%stream-zip (map stream-cdr strms))))))
      (cond
        ((null? strms) (error "stream-zip: no stream arguments"))
        ((any (lambda (x) (not (stream? x))) strms) (error "stream-zip: non-stream argument"))
        (else (%stream-zip strms))))

    (define-syntax stream-let
      (syntax-rules ()
        ((_ tag ((name val) ...) body1 body2 ...)
         ((letrec ((tag (stream-lambda (name ...) body1 body2 ...))) tag) val ...))))

    (define (port->stream . port)
      (define %port->stream
        (stream-lambda (p)
          (let ((c (read-char p)))
            (if (eof-object? c) stream-null (stream-cons c (%port->stream p))))))
      (let ((p (if (null? port) (current-input-port) (car port))))
        (if (not (input-port? p)) (error "port->stream: non-input-port argument" p) (%port->stream p))))

    ;; stream-match: ported using curry's own established syntax-rules
    ;; wildcard idiom (literal `_`, matched before the general
    ;; pattern-variable clause) rather than the reference's syntax-case
    ;; version -- see the module header comment.
    (define-syntax stream-match
      (syntax-rules ()
        ((_ strm-expr clause ...)
         (let ((s strm-expr))
           (cond
             ((not (stream? s)) (error "stream-match: non-stream argument"))
             ((stream-match-test s clause) => car)
             ...
             (else (error "stream-match: pattern failure")))))))

    (define-syntax stream-match-test
      (syntax-rules ()
        ((_ strm (pattern fender expr)) (stream-match-pattern strm pattern () (and fender (list expr))))
        ((_ strm (pattern expr)) (stream-match-pattern strm pattern () (list expr)))))

    (define-syntax stream-match-pattern
      (syntax-rules (_)
        ((_ strm () (binding ...) body) (and (stream-null? strm) (let (binding ...) body)))
        ((_ strm (_ . rest) (binding ...) body)
         (and (stream-pair? strm)
              (let ((strm (stream-cdr strm))) (stream-match-pattern strm rest (binding ...) body))))
        ((_ strm (var . rest) (binding ...) body)
         (and (stream-pair? strm)
              (let ((temp (stream-car strm)) (strm (stream-cdr strm)))
                (stream-match-pattern strm rest ((var temp) binding ...) body))))
        ((_ strm _ (binding ...) body) (let (binding ...) body))
        ((_ strm var (binding ...) body) (let ((var strm) binding ...) body))))))
