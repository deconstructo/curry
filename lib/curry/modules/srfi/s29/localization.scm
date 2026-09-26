;;; SRFI-29: Localization.
;;;
;;; https://srfi.schemers.org/srfi-29/ -- register locale-specific
;;; translations of "message templates" (format strings, per SRFI-28,
;;; whose own format procedure is layered in (srfi s28 format) --
;;; including that SRFI's ~N@* positional-reference extension, since
;;; this SRFI is the one that actually specifies it) under a "bundle
;;; specifier" (package-name . language . country . details...), then
;;; look one up later via the CURRENT locale, with progressively looser
;;; fallback as the current locale's own specifics run out.
;;;
;;; Locale state (current-language/current-country/current-locale-
;;; details) is a plain global mutable variable per property, exactly
;;; matching the SRFI's own reference implementation (a bare set!, no
;;; thread-local/parameterize integration) -- the SRFI's own text only
;;; ever says "for the executing thread (or the entire system if no
;;; thread distinction is made)", explicitly leaving genuine per-thread
;;; isolation optional. curry's actors don't share a mutable global
;;; frame with each other's own top-level defines (see CLAUDE.md's
;;; module-isolation notes), so in practice each actor already gets an
;;; independent binding of these three variables for free -- this
;;; wasn't specifically engineered for SRFI-29, just inherited from how
;;; curry's own library/global-environment model already works.
;;;
;;; store-bundle!/load-bundle! always return #f: this SRFI's own text
;;; explicitly sanctions this ("compliant systems may always return
;;; #f") for a system with no bundle persistence backend, distinct from
;;; the other procedures here, which are all real, complete
;;; implementations of their required behavior.

(define-library (srfi s29 localization)
  (import (scheme base) (srfi s28 format))
  (export
    current-language current-country current-locale-details
    declare-bundle! store-bundle! load-bundle!
    localized-template)
  (begin

    (define %current-language 'en)
    (define %current-country 'us)
    (define %current-locale-details '())

    (define (current-language . new)
      (if (pair? new) (begin (set! %current-language (car new)) (if #f #f)) %current-language))
    (define (current-country . new)
      (if (pair? new) (begin (set! %current-country (car new)) (if #f #f)) %current-country))
    (define (current-locale-details . new)
      (if (pair? new) (begin (set! %current-locale-details (car new)) (if #f #f)) %current-locale-details))

    ;; Bundles: an alist from bundle-specifier (a list of symbols,
    ;; compared with equal?) to an alist of (template-name . string).
    ;; A plain alist, not a hash table, since specifiers are short lists
    ;; compared structurally and bundle registration/lookup is not a
    ;; hot path -- matching this SRFI's own reference implementation's
    ;; choice of a flat association list (*localization-bundles*).
    (define %bundles '())

    (define (%valid-specifier? spec)
      (and (pair? spec) (let loop ((s spec)) (or (null? s) (and (symbol? (car s)) (loop (cdr s)))))))

    (define (declare-bundle! bundle-specifier alist)
      (if (not (%valid-specifier? bundle-specifier))
          (error "declare-bundle!: bundle specifier must be a non-empty list of symbols" bundle-specifier))
      (set! %bundles (cons (cons bundle-specifier alist)
                            (let loop ((bs %bundles))
                              (cond
                                ((null? bs) '())
                                ((equal? (caar bs) bundle-specifier) (loop (cdr bs)))
                                (else (cons (car bs) (loop (cdr bs))))))))
      (if #f #f))

    (define (store-bundle! bundle-specifier) #f)
    (define (load-bundle! bundle-specifier) #f)

    ;; Builds the fallback sequence of candidate specifiers for
    ;; package-name under the CURRENT locale, longest (most specific)
    ;; first: (package lang country . details), then with the last
    ;; detail dropped, ... down to just (package). Mirrors the SRFI's
    ;; own search algorithm text exactly ("remove last element and
    ;; retry... until specifier becomes empty" -- "empty" there means
    ;; down to the bare package name, since a specifier is defined as
    ;; always including at least the package name).
    (define (%candidate-specifiers package-name)
      (let* ((full (cons package-name
                     (cons (current-language)
                       (cons (current-country) (current-locale-details))))))
        (let loop ((spec full))
          (cons spec (if (or (null? (cdr spec)) (null? spec)) '() (loop (%drop-last spec)))))))

    (define (%drop-last lst)
      (if (null? (cdr lst)) '() (cons (car lst) (%drop-last (cdr lst)))))

    (define (localized-template package-name message-template-name)
      (let loop ((specs (%candidate-specifiers package-name)))
        (if (null? specs)
            #f
            (let ((bundle (assoc (car specs) %bundles)))
              (let ((hit (and bundle (assq message-template-name (cdr bundle)))))
                (if hit (cdr hit) (loop (cdr specs))))))))))
