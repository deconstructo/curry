;;; SRFI-272 intermediate-tier implementation: adds keyword-argument
;;; forms of pp/pprint/pprint-shared/pprint-simple, pp*, pprint-file,
;;; and the pp-radix/pp-level/pp-length parameters plus pretty-style.
;;; The public shim at lib/curry/modules/srfi/272/intermediate.scm just
;;; re-exports this. https://srfi.schemers.org/srfi-272/

(define-library (srfi s272 intermediate-impl)
  (import (scheme base) (scheme read) (srfi s272 engine))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file
    pp-width pp-graph pp-circle pp-radix pp-level pp-length pretty-style)
  (begin
    ;; Splits a (port|kv ...) trailing argument list: the leading
    ;; element is the port when present and not itself a valid keyword
    ;; (i.e. not a parameter object and not a symbol in %kwarg-table --
    ;; approximated here by "is it a port?", since a keyword is always
    ;; a symbol or a parameter procedure, never a port).
    (define (%split-port-and-kvs args)
      (if (and (pair? args) (port? (car args)))
          (values (car args) (cdr args))
          (values (current-output-port) args)))

    (define (pp obj . args)
      (call-with-values (lambda () (%split-port-and-kvs args))
        (lambda (port kvs)
          (%pp-with-kwargs kvs (lambda () (%pp-to-port obj port (%current-mode)))))))

    ;; pp* obj [port] [key value] ... kv-list -- the trailing kv-list
    ;; argument (always present per the spec's own signature) is
    ;; spliced into the call as if by apply. `apply` only splices its
    ;; OWN final argument, so args's last element (the kv-list) must be
    ;; appended onto args's other elements first, THEN that combined
    ;; flat list handed to apply as its one final argument -- passing
    ;; `args` straight to apply would leave the kv-list nested as one
    ;; opaque element instead of spliced in.
    (define (pp* obj . args)
      (if (null? args)
          (pp obj)
          (apply pp obj (append (%all-but-last args) (%last args)))))

    (define (%all-but-last lst) (if (null? (cdr lst)) '() (cons (car lst) (%all-but-last (cdr lst)))))
    (define (%last lst) (if (null? (cdr lst)) (car lst) (%last (cdr lst))))

    ;; pprint/pprint-shared/pprint-simple hardwire their own mode --
    ;; per the spec, keyword/parameter pp-graph and pp-circle values
    ;; have no effect here, so those two keys are filtered out before
    ;; dispatching to %pp-with-kwargs (everything else still applies).
    (define (%drop-graph-circle-keys kvs)
      (let loop ((kvs kvs))
        (cond ((null? kvs) '())
              ((memq (car kvs) (list 'pp-graph 'pp-circle pp-graph pp-circle))
               (loop (cddr kvs)))
              (else (cons (car kvs) (cons (cadr kvs) (loop (cddr kvs))))))))

    (define (pprint obj . args)
      (call-with-values (lambda () (%split-port-and-kvs args))
        (lambda (port kvs)
          (%pp-with-kwargs (%drop-graph-circle-keys kvs) (lambda () (%pp-to-port obj port 'circle))))))

    (define (pprint-shared obj . args)
      (call-with-values (lambda () (%split-port-and-kvs args))
        (lambda (port kvs)
          (%pp-with-kwargs (%drop-graph-circle-keys kvs) (lambda () (%pp-to-port obj port 'graph))))))

    (define (pprint-simple obj . args)
      (call-with-values (lambda () (%split-port-and-kvs args))
        (lambda (port kvs)
          (%pp-with-kwargs (%drop-graph-circle-keys kvs) (lambda () (%pp-to-port obj port 'simple))))))

    ;; pprint-file infile [outfile] [key value] ... -- reads every
    ;; top-level datum from infile and pretty-prints each in turn to
    ;; outfile. This tier makes no attempt to preserve comments/blank
    ;; lines from the input (the fancy tier's pprint-file does).
    ;;
    ;; outfile is a filename string per the spec, but an already-open
    ;; output port is also accepted directly (used verbatim, never
    ;; closed by this procedure) -- a harmless, convenient superset,
    ;; consistent with how pp/pprint already accept a port.
    (define (%split-outfile-and-kvs args)
      (cond
        ((and (pair? args) (string? (car args))) (values (open-output-file (car args)) #t (cdr args)))
        ((and (pair? args) (port? (car args))) (values (car args) #f (cdr args)))
        (else (values (current-output-port) #f args))))

    (define (pprint-file infile . args)
      (call-with-values (lambda () (%split-outfile-and-kvs args))
        (lambda (out owns-out? kvs)
          (let ((in (open-input-file infile)))
            (%pp-with-kwargs kvs
              (lambda ()
                (let loop ()
                  (let ((datum (read in)))
                    (unless (eof-object? datum)
                      (%pp-to-port datum out (%current-mode))
                      (loop))))))
            (close-port in)
            (when owns-out? (close-port out))))))))
