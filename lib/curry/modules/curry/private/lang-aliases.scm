;;; (curry private lang-aliases) — shared macro for giving a library's own
;;; procedures/values foreign-language synonyms, declared right next to the
;;; library that defines them.
;;;
;;; This is the pure-Scheme counterpart to src/akkadian_names.h: that C
;;; table covers names bound directly in the global environment at
;;; builtins_register() time (the R7RS/R6RS core), because those names
;;; exist before any module ever loads. Everything defined inside a
;;; (define-library ...) or plain-file (curry ...) module -- every SRFI,
;;; every (curry oop)/(curry sets)/etc -- only comes to exist once that
;;; file is loaded, so its foreign names belong in that same file, not in
;;; a central header that would otherwise have to grow forever and force a
;;; full C rebuild for every new library word.
;;;
;;; Usage, at the bottom of a library file, after the names being aliased
;;; are already defined/imported. All three positions are bare identifiers
;;; (not strings) -- the english name must already be bound, and each form
;;; becomes a new top-level define, so this only works at a syntactic
;;; top level (a plain file, or a define-library's (begin ...) clause):
;;;
;;;   (import (curry private lang-aliases))
;;;   (define-name-aliases
;;;     (hash-table-ref  kunuk-maḫārim  𒁀𒃲𒁹)
;;;     (hash-table-set! kunuk-šakānim  𒁀𒃲𒋻))
;;;
;;; Each row is (english-name foreign-form ...) -- one english name and
;;; one or more written forms for it (Akkadian supplies two: transliterated
;;; and cuneiform; a future language with a single script just supplies
;;; one). Adding a new language later means adding another form per row,
;;; not new machinery. Inside a define-library, remember to also list the
;;; new forms in the library's own (export ...) clause -- see the note
;;; below.
;;;
;;; define-library modules must also add the new alias names to their own
;;; (export ...) clause -- unlike the C core's global env, a define-library
;;; module only exposes what it explicitly exports. Plain-file (curry ...)
;;; modules (no define-library, no export clause) don't need this: every
;;; top-level binding in such a file is already importable.
;;;
;;; define-syntax-aliases is the same idea for macros (define-syntax
;;; exports): the row's english name must already be a macro, and each
;;; alias becomes a macro that forwards its arguments unchanged.
;;;
;;; Reader gotcha when listing cuneiform forms in an (export ...) clause:
;;; two cuneiform tokens separated only by a space get read as ONE
;;; symbol, not two -- the reader greedily merges adjacent
;;; space-separated cuneiform groups (that's what lets a sexagesimal
;;; numeral like "𒁹 𒌋𒁹" read as a single number). Never list cuneiform
;;; forms back-to-back in an export clause; always interleave them with
;;; their (non-cuneiform) transliterated form, e.g. "tr1 cu1 tr2 cu2 ..."
;;; rather than "tr1 tr2 ... cu1 cu2 ...". The alias rows passed to
;;; define-name-aliases/define-syntax-aliases themselves are unaffected
;;; -- a cuneiform form there is always followed by a closing paren, not
;;; another cuneiform token, so it can never merge.

(define-syntax %lang-alias-row
  (syntax-rules ()
    ((_ eng) (begin))
    ((_ eng form) (define form eng))
    ((_ eng form more ...) (begin (define form eng) (%lang-alias-row eng more ...)))))

(define-syntax define-name-aliases
  (syntax-rules ()
    ((_) (begin))
    ((_ (eng form ...) rest ...)
     (begin (%lang-alias-row eng form ...) (define-name-aliases rest ...)))))

(define-syntax %lang-syntax-alias-row
  (syntax-rules ()
    ((_ eng) (begin))
    ((_ eng form)
     (define-syntax form (syntax-rules () ((_ . args) (eng . args)))))
    ((_ eng form more ...)
     (begin (define-syntax form (syntax-rules () ((_ . args) (eng . args))))
            (%lang-syntax-alias-row eng more ...)))))

(define-syntax define-syntax-aliases
  (syntax-rules ()
    ((_) (begin))
    ((_ (eng form ...) rest ...)
     (begin (%lang-syntax-alias-row eng form ...) (define-syntax-aliases rest ...)))))
