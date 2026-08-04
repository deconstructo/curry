;;; (curry oop) — Slim CLOS: classes, generic functions, multiple dispatch.
;;;
;;; Layer 1 of docs/thoughts/oop.md: a pure-Scheme macro layer, no C changes.
;;; Dispatch goes through a hash of method tables keyed by argument class —
;;; acceptable overhead for application-level code; Layers 2 (VM-level
;;; polymorphic inline cache) and 3 (numeric-tower integration) are future
;;; work, not attempted here.
;;;
;;; What this deliberately does NOT build (per oop.md's own recommendation):
;;; no metaobject protocol, no method-combination qualifiers (:before/:after/
;;; :around) — call-next-method in primary methods only, no metaclasses.
;;;
;;; Slots are immutable by default (#:mutable opts in). This is the one
;;; non-CLOS choice oop.md makes deliberately: an object whose state can be
;;; accidentally mutated is a worse simulation primitive for curry's physics/
;;; CAS domain than one that can't, and immutable objects share freely across
;;; actors with no synchronization.
;;;
;;; The built-in type hierarchy below is a pragmatic subset of oop.md's
;;; aspirational tree, constrained to what's actually distinguishable via
;;; predicates that exist today — there is no Scheme-visible fixnum?/bignum?/
;;; flonum? distinction in this codebase (see src/numeric.c's C-level val_t
;;; tag checks for why), so <fixnum>/<bignum>/<flonum> from the design doc
;;; collapse into <integer> (exact-integer?) and <inexact-real> here.

(define-library (curry oop)
  (import (scheme base))
  (export
    ;; Public API
    make slot-ref slot-set!
    class-of is-a? subclass? class-name class-slots class-precedence-list
    define-class define-generic define-method call-next-method
    ;; Helper procedures/macros referenced by the expansion of the macros
    ;; above — curry's syntax-rules is not hygienic across define-library
    ;; boundaries, so anything a macro's *expansion* (as opposed to a
    ;; procedure's own body) refers to must be visible at the use site,
    ;; which means exported here even though it's not meant to be called
    ;; directly.
    %build-class %parse-slot %parse-slot-opts %make-slot
    %gen-accessors %gen-accessors-for-spec
    %make-generic %ensure-generic! %add-method!
    ;; Built-in type hierarchy — named directly in user code as
    ;; define-method/is-a? specializers, so these need exporting too.
    <object>
    <number> <integer> <inexact-real>
    <quaternion> <octonion> <multivector> <surreal> <symbolic> <quantum>
    <tuple> <up-tuple> <down-tuple>
    <boolean> <pair> <null> <vector> <bytevector> <string> <symbol> <char>
    <procedure> <port> <actor> <promise> <hash-table> <set>)
  (begin

;;; =========================================================================
;;; Small self-contained helpers (avoid depending on SRFI-1 availability)
;;; =========================================================================

(define (%every2 pred l1 l2)
  (or (null? l1)
      (and (pred (car l1) (car l2)) (%every2 pred (cdr l1) (cdr l2)))))

(define (%position x lst)
  (let loop ((l lst) (i 0))
    (cond ((null? l) -1)
          ((eq? (car l) x) i)
          (else (loop (cdr l) (+ i 1))))))

(define (%split-list lst)
  (if (or (null? lst) (null? (cdr lst)))
      (cons lst '())
      (let ((rest (%split-list (cddr lst))))
        (cons (cons (car lst) (car rest))
              (cons (cadr lst) (cdr rest))))))

(define (%merge-sort lst less?)
  (define (merge a b)
    (cond ((null? a) b)
          ((null? b) a)
          ((less? (car a) (car b)) (cons (car a) (merge (cdr a) b)))
          (else (cons (car b) (merge a (cdr b))))))
  (if (or (null? lst) (null? (cdr lst)))
      lst
      (let* ((halves (%split-list lst))
             (a (car halves))
             (b (cdr halves)))
        (merge (%merge-sort a less?) (%merge-sort b less?)))))

;;; =========================================================================
;;; C3 linearization (class precedence list)
;;; =========================================================================

;; Standard C3 merge: repeatedly pick the head of the first list whose head
;; does not appear in the tail of any list, remove it everywhere it heads a
;; list, and repeat. Raises on an inconsistent precedence graph.
(define (%c3-in-any-tail? x lists)
  (let loop ((ls lists))
    (and (pair? ls)
         (or (and (pair? (car ls)) (memq x (cdr (car ls))))
             (loop (cdr ls))))))

(define (%c3-find-candidate lists)
  (let loop ((ls lists))
    (cond ((null? ls) #f)
          ((null? (car ls)) (loop (cdr ls)))
          ((%c3-in-any-tail? (caar ls) lists) (loop (cdr ls)))
          (else (caar ls)))))

(define (%c3-merge lists)
  (if (every-null? lists)
      '()
      (let ((candidate (%c3-find-candidate lists)))
        (if (not candidate)
            (error "class-precedence-list: inconsistent precedence graph")
            (cons candidate
                  (%c3-merge (map (lambda (l)
                                    (if (and (pair? l) (eq? (car l) candidate))
                                        (cdr l)
                                        l))
                                  lists)))))))

(define (every-null? lists)
  (or (null? lists) (and (null? (car lists)) (every-null? (cdr lists)))))

;;; =========================================================================
;;; Classes
;;; =========================================================================

(define-record-type %slot
  (%make-slot name init has-init? accessor mutable?)
  %slot?
  (name      %slot-name)
  (init      %slot-init)
  (has-init? %slot-has-init?)
  (accessor  %slot-accessor)
  (mutable?  %slot-mutable?))

;; cpl and all-slots are filled in by %build-class after construction (they
;; need the class object itself to exist first — C3 conses the class onto
;; the front of its merged parents' CPLs).
(define-record-type %class
  (%make-class-raw name direct-supers direct-slots)
  %class?
  (name          %class-name)
  (direct-supers %class-direct-supers)
  (direct-slots  %class-direct-slots)
  (cpl           %class-cpl           %class-set-cpl!)
  (all-slots     %class-all-slots     %class-set-all-slots!))

(define (%merge-slots cpl)
  ;; Walk the CPL most-specific-first, collecting each class's direct slots,
  ;; skipping any name already seen (so a subclass's own slot definition
  ;; wins over an ancestor's of the same name).
  (let loop ((classes cpl) (acc '()) (seen '()))
    (if (null? classes)
        (reverse acc)
        (let inner ((slots (%class-direct-slots (car classes)))
                    (acc acc) (seen seen))
          (if (null? slots)
              (loop (cdr classes) acc seen)
              (let* ((s (car slots)) (nm (%slot-name s)))
                (if (memq nm seen)
                    (inner (cdr slots) acc seen)
                    (inner (cdr slots) (cons s acc) (cons nm seen)))))))))

(define (%build-class name direct-supers direct-slots)
  ;; An empty superclass list means "direct subclass of <object>", per the
  ;; design doc's own convention — except for <object> itself, which must
  ;; stay rootless (and can't reference the <object> binding anyway while
  ;; it's still being constructed by this very call).
  (let* ((supers (if (and (null? direct-supers) (not (eq? name '<object>)))
                     (list <object>)
                     direct-supers))
         (cls (%make-class-raw name supers direct-slots))
         (parent-cpls (map %class-cpl supers))
         (merged (%c3-merge (append parent-cpls (list supers))))
         (cpl (cons cls merged)))
    (%class-set-cpl! cls cpl)
    (%class-set-all-slots! cls (%merge-slots cpl))
    cls))

;;; ---- define-class ----
;;; Slot spec: (name [#:init expr] [#:accessor sym] [#:mutable] [#:type cls])
;;; #:type is parsed but not enforced in Layer 1 (no runtime type checking).

(define-syntax %parse-slot
  (syntax-rules ()
    ((_ (name . opts)) (%parse-slot-opts name opts #f #f #f #f))))

(define-syntax %parse-slot-opts
  (syntax-rules (#:init #:accessor #:mutable #:type)
    ;; init is wrapped in a thunk, not stored pre-evaluated: #:init is
    ;; re-run fresh for every instance (see %slot-init's call site in
    ;; `make`), the same way CLOS re-evaluates :initform per instance —
    ;; without this, a mutable default like #:init (make-hash-table) would
    ;; be a single object eq?-shared across every instance of the class.
    ((_ name () init has-init? accessor mutable?)
     (%make-slot 'name (lambda () init) has-init? 'accessor mutable?))
    ((_ name (#:init v . rest) init has-init? accessor mutable?)
     (%parse-slot-opts name rest v #t accessor mutable?))
    ((_ name (#:accessor a . rest) init has-init? accessor mutable?)
     (%parse-slot-opts name rest init has-init? a mutable?))
    ((_ name (#:mutable . rest) init has-init? accessor mutable?)
     (%parse-slot-opts name rest init has-init? accessor #t))
    ((_ name (#:type t . rest) init has-init? accessor mutable?)
     (%parse-slot-opts name rest init has-init? accessor mutable?))))

(define-syntax %gen-accessors
  (syntax-rules (#:init #:accessor #:mutable #:type)
    ((_ name ()) (begin))
    ((_ name (#:accessor a . rest))
     (begin (define (a obj) (slot-ref obj 'name))
            (%gen-accessors name rest)))
    ((_ name (#:init v . rest))    (%gen-accessors name rest))
    ((_ name (#:mutable . rest))   (%gen-accessors name rest))
    ((_ name (#:type t . rest))    (%gen-accessors name rest))))

(define-syntax %gen-accessors-for-spec
  (syntax-rules ()
    ((_ (name . opts)) (%gen-accessors name opts))))

(define-syntax define-class
  (syntax-rules ()
    ((_ cname (super ...) slot-spec ...)
     (begin
       (define cname
         (%build-class 'cname (list super ...) (list (%parse-slot slot-spec) ...)))
       (%gen-accessors-for-spec slot-spec) ...))))

;;; =========================================================================
;;; Instances
;;; =========================================================================

(define-record-type %instance
  (%make-instance class slots)
  %instance?
  (class %instance-class)
  (slots %instance-slots))

;; Fresh sentinel per load (never eq? to any user value) marking an unbound
;; slot — distinct from #f so an actual #f slot value round-trips correctly.
(define %unbound-slot (list '%unbound-slot))

(define (%name->keyword name)
  (string->symbol (string-append "#:" (symbol->string name))))

(define (%kwargs->alist kwargs)
  (let loop ((l kwargs) (acc '()))
    (if (null? l)
        (reverse acc)
        (loop (cddr l) (cons (cons (car l) (cadr l)) acc)))))

(define (%find-slot-desc cls name)
  (let loop ((slots (%class-all-slots cls)))
    (cond ((null? slots) #f)
          ((eq? (%slot-name (car slots)) name) (car slots))
          (else (loop (cdr slots))))))

(define (make cls . kwargs)
  (if (not (%class? cls)) (error "make: not a class" cls))
  (let ((table (make-hash-table))
        (kwalist (%kwargs->alist kwargs)))
    (for-each
     (lambda (s)
       (let* ((name (%slot-name s))
              (given (assq (%name->keyword name) kwalist)))
         (hash-table-set! table name
                          (cond (given (cdr given))
                                ((%slot-has-init? s) ((%slot-init s)))
                                (else %unbound-slot)))))
     (%class-all-slots cls))
    (%make-instance cls table)))

(define (slot-ref obj name)
  (if (not (%instance? obj)) (error "slot-ref: not a class instance" obj))
  (let ((v (hash-table-ref (%instance-slots obj) name %unbound-slot)))
    (if (eq? v %unbound-slot)
        (error "slot-ref: unbound slot" name)
        v)))

(define (slot-set! obj name val)
  (if (not (%instance? obj)) (error "slot-set!: not a class instance" obj))
  ;; %unbound-slot itself is not exported, but slot-set! is — so this
  ;; guards the one direct misuse of that internal sentinel a caller could
  ;; still trigger via some other eq? route: storing it back into a slot
  ;; would make slot-ref wrongly report the slot as still unbound.
  (if (eq? val %unbound-slot)
      (error "slot-set!: cannot store the internal unbound-slot marker"))
  (let ((desc (%find-slot-desc (%instance-class obj) name)))
    (cond
      ((not desc) (error "slot-set!: no such slot" name))
      ((not (%slot-mutable? desc))
       (error "slot-set!: slot is immutable (declare #:mutable to allow)" name))
      (else (hash-table-set! (%instance-slots obj) name val)))))

;;; =========================================================================
;;; Built-in type hierarchy
;;; =========================================================================

(define <object> (%build-class '<object> '() '()))

(define <number>        (%build-class '<number>        (list <object>) '()))
(define <integer>       (%build-class '<integer>       (list <number>) '()))
(define <inexact-real>  (%build-class '<inexact-real>  (list <number>) '()))

(define <quaternion>    (%build-class '<quaternion>    (list <object>) '()))
(define <octonion>      (%build-class '<octonion>      (list <object>) '()))
(define <multivector>   (%build-class '<multivector>   (list <object>) '()))
(define <surreal>       (%build-class '<surreal>       (list <object>) '()))
(define <symbolic>      (%build-class '<symbolic>      (list <object>) '()))
(define <quantum>       (%build-class '<quantum>       (list <object>) '()))

(define <tuple>         (%build-class '<tuple>         (list <object>) '()))
(define <up-tuple>      (%build-class '<up-tuple>      (list <tuple>)  '()))
(define <down-tuple>    (%build-class '<down-tuple>    (list <tuple>)  '()))

(define <boolean>       (%build-class '<boolean>       (list <object>) '()))
(define <pair>          (%build-class '<pair>          (list <object>) '()))
(define <null>          (%build-class '<null>          (list <object>) '()))
(define <vector>        (%build-class '<vector>        (list <object>) '()))
(define <bytevector>    (%build-class '<bytevector>    (list <object>) '()))
(define <string>        (%build-class '<string>        (list <object>) '()))
(define <symbol>        (%build-class '<symbol>        (list <object>) '()))
(define <char>          (%build-class '<char>          (list <object>) '()))
(define <procedure>     (%build-class '<procedure>     (list <object>) '()))
(define <port>          (%build-class '<port>          (list <object>) '()))
(define <actor>         (%build-class '<actor>         (list <object>) '()))
(define <promise>       (%build-class '<promise>       (list <object>) '()))
(define <hash-table>    (%build-class '<hash-table>    (list <object>) '()))
(define <set>           (%build-class '<set>           (list <object>) '()))

(define (%builtin-class-of obj)
  (cond
    ((exact-integer? obj)               <integer>)
    ((and (real? obj) (inexact? obj))   <inexact-real>)
    ((quaternion? obj)                  <quaternion>)
    ((octonion? obj)                    <octonion>)
    ((mv? obj)                          <multivector>)
    ((surreal? obj)                     <surreal>)
    ((symbolic? obj)                    <symbolic>)
    ((quantum? obj)                     <quantum>)
    ((up? obj)                          <up-tuple>)
    ((down? obj)                        <down-tuple>)
    ((tuple? obj)                       <tuple>)
    ((number? obj)                      <number>)
    ((boolean? obj)                     <boolean>)
    ((null? obj)                        <null>)
    ((pair? obj)                        <pair>)
    ((vector? obj)                      <vector>)
    ((bytevector? obj)                  <bytevector>)
    ((string? obj)                      <string>)
    ((symbol? obj)                      <symbol>)
    ((char? obj)                        <char>)
    ((procedure? obj)                   <procedure>)
    ((port? obj)                        <port>)
    ((actor? obj)                       <actor>)
    ((promise? obj)                     <promise>)
    ((hash-table? obj)                  <hash-table>)
    ((set? obj)                         <set>)
    (else                               <object>)))

;;; =========================================================================
;;; Introspection
;;; =========================================================================

(define (class-of obj)
  (if (%instance? obj) (%instance-class obj) (%builtin-class-of obj)))

(define (is-a? obj cls) (and (memq cls (%class-cpl (class-of obj))) #t))
(define (subclass? c1 c2) (and (memq c2 (%class-cpl c1)) #t))
(define (class-name cls) (%class-name cls))
(define (class-slots cls) (map %slot-name (%class-all-slots cls)))
(define (class-precedence-list cls) (%class-cpl cls))

;;; =========================================================================
;;; Generic functions and methods
;;; =========================================================================

;; A generic function's dispatcher closure closes over a #(name methods
;; fallback) vector. External code (define-method, %ensure-generic!) reaches
;; that vector by calling the closure with a reserved sentinel first
;; argument — avoids needing an eq?-keyed identity registry.
(define %%gf-introspect-tag (list '%%gf-introspect))

(define %next-methods (make-parameter '()))
(define %current-args  (make-parameter '()))

(define (%applicable? specializers args)
  (and (= (length specializers) (length args))
       (%every2 is-a? args specializers)))

(define (%method-more-specific? m1 m2 args)
  (let loop ((s1 (car m1)) (s2 (car m2)) (as args))
    (cond
      ((null? s1) #f)
      ((eq? (car s1) (car s2)) (loop (cdr s1) (cdr s2) (cdr as)))
      (else
       (let ((cpl (%class-cpl (class-of (car as)))))
         (< (%position (car s1) cpl) (%position (car s2) cpl)))))))

(define (%sort-methods methods args)
  (%merge-sort methods (lambda (a b) (%method-more-specific? a b args))))

(define (%invoke-generic box args)
  (let* ((methods    (vector-ref box 1))
         (fallback   (vector-ref box 2))
         (applicable (filter (lambda (m) (%applicable? (car m) args)) methods))
         (sorted     (%sort-methods applicable args))
         (chain      (if fallback
                         (append sorted (list (cons '() fallback)))
                         sorted)))
    (if (null? chain)
        (error (string-append "no applicable method for generic function "
                              (symbol->string (vector-ref box 0))))
        (parameterize ((%next-methods (cdr chain)) (%current-args args))
          (apply (cdar chain) args)))))

(define (%make-generic name)
  (let ((box (vector name '() #f)))
    (lambda args
      (if (and (pair? args) (eq? (car args) %%gf-introspect-tag))
          box
          (%invoke-generic box args)))))

(define (%generic-box gf)
  (guard (e (#t #f))
    (let ((r (gf %%gf-introspect-tag)))
      (if (and (vector? r) (= (vector-length r) 3)) r #f))))

;; Only ever called with `name` already bound to *some* value — the guard
;; in define-generic/define-method's expansion catches a genuinely unbound
;; `name` before this is reached and auto-vivifies a fresh generic directly
;; (see those macros below), so a non-procedure `current` here means the
;; name really is bound to something else already (a number, a string, an
;; unrelated helper) — raise rather than silently clobbering it.
(define (%ensure-generic! name current)
  (if (procedure? current)
      (if (%generic-box current)
          current    ; already a generic — reuse the procedure, not its box
          (let ((gf (%make-generic name)))
            ;; Promote an existing ordinary procedure (e.g. the builtin +)
            ;; into a fallback: user-defined methods are tried first (most
            ;; specific wins as usual); if none apply, the original
            ;; procedure runs exactly as it did before this generic existed.
            (vector-set! (%generic-box gf) 2 current)
            gf))
      (error "define-generic/define-method: name is already bound to a non-procedure value"
             name current)))

(define (%specializers-eq? s1 s2)
  ;; Identity comparison, not equal?: classes are singletons (each created
  ;; once, bound to a unique top-level variable), and equal?'s structural
  ;; recursion into two DIFFERENT class records could spuriously call them
  ;; "equal" if their fields happen to look alike.
  (cond ((and (null? s1) (null? s2)) #t)
        ((or (null? s1) (null? s2)) #f)
        ((eq? (car s1) (car s2)) (%specializers-eq? (cdr s1) (cdr s2)))
        (else #f)))

(define (%add-method! gf specializers proc)
  (let ((box (%generic-box gf)))
    (if (not box) (error "define-method: not a generic function"))
    ;; Replace an existing method with the same specializers rather than
    ;; appending a duplicate — otherwise re-evaluating a define-method (the
    ;; ordinary REPL/script-reload workflow) leaves the OLD definition as
    ;; the one dispatch actually picks (the new one only reachable, if at
    ;; all, via call-next-method), which is exactly backwards from what a
    ;; redefinition should do.
    (vector-set! box 1
      (cons (cons specializers proc)
            (filter (lambda (m) (not (%specializers-eq? (car m) specializers)))
                    (vector-ref box 1))))))

(define (call-next-method . new-args)
  (let ((remaining (%next-methods)))
    (if (null? remaining)
        (error "call-next-method: no next method")
        (let* ((entry (car remaining))
               (proc  (cdr entry))
               (args  (if (null? new-args) (%current-args) new-args)))
          (parameterize ((%next-methods (cdr remaining)))
            (apply proc args))))))

(define-syntax define-generic
  (syntax-rules ()
    ((_ name (arg ...))
     (define name
       (guard (e (#t (%make-generic 'name)))
         (%ensure-generic! 'name name))))))

(define-syntax define-method
  (syntax-rules ()
    ((_ name ((arg spec) ...) body ...)
     (begin
       (define name
         (guard (e (#t (%make-generic 'name)))
           (%ensure-generic! 'name name)))
       (%add-method! name (list spec ...) (lambda (arg ...) body ...))))))

  )) ;; end begin, define-library
