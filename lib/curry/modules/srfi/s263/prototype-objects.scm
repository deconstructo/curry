;;; (srfi s263 prototype-objects) — SRFI-263 Prototype Object System
;;;
;;; define-library wrapper: see below the header comment for the imports/
;;; exports clause (kept short, everything else lives in one big `begin`,
;;; matching the shape of the other multi-hundred-line srfi/sN/*.scm files
;;; in this directory, e.g. s19/time.scm).
;;;
;;; A "Self"-inspired prototype/message-passing object system: objects are
;;; plain procedures invoked as `(obj 'message arg ...)`, derived from
;;; other objects (cloning-with-a-parent-pointer) rather than instantiated
;;; from classes. Complements, and is unrelated to, (curry oop) (a CLOS-
;;; style class/generic-function system already in this codebase) -- the
;;; two are different paradigms, not competing implementations of the
;;; same thing.
;;;
;;; Implementation notes (the SRFI's own prose is thin/example-driven in
;;; a few places; these are the interpretive choices made here):
;;;
;;; - An object's slot table is private to its closure, reachable only via
;;;   a reserved, unforgeable sentinel object (%introspect) that a plain
;;;   user message send can never construct (it's a freshly-allocated,
;;;   never-exported pair, matched by eq?) -- this is how the generic
;;;   dispatch/search code below can inspect ANY object's slots without
;;;   exposing that as part of the public message protocol.
;;; - Ambiguity: "resolve-parents" collects a result from every immediate
;;;   parent slot (each recursively searched); zero hits is
;;;   message-not-understood, exactly one hit is used, more than one hit
;;;   (or any hit that was itself ambiguous further up the tree) is
;;;   ambiguous-message-send.
;;; - `resend` is `(resend target-override . args)`: with target-override
;;;   #f, the search continues at the object's own parents (skipping the
;;;   level that owns the currently-running method, since re-checking it
;;;   would just find the same slot again); with a specific target-override
;;;   object, a fresh full search (including the target's own local slots)
;;;   starts there. Omitting args reuses the original message arguments.
;;; - `copy`, sent as a zero-argument message to an object, clones that
;;;   object's OWN slots into a fresh, independent object (deep enough
;;;   that mutating a value slot on the copy doesn't affect the original).
;;;   The SRFI's own prose ("creates an object with the messages and slots
;;;   copied from its parent") is describing the object being copied
;;;   (colloquially, "its parent" = "the thing copied from"), not a
;;;   literal `parent` slot -- `copy-object`'s definition confirms this by
;;;   itself taking an explicit parent argument to copy from.
;;; - Extra parents given to `define-object`/`derive-object`/`copy-object`
;;;   (multiple inheritance) are installed under auto-generated getter
;;;   names `parent2`, `parent3`, ... (the SRFI doesn't specify a naming
;;;   convention for these).

(define-library (srfi s263 prototype-objects)
  (import (scheme base))
  (export
    *the-root-object*
    slot? slot-getter slot-setter slot-type
    define-method define-object derive-object copy-object
    ;; Internal helper macros, referenced from derive-object/copy-object's
    ;; own expansion -- exported (not just defined) because a using
    ;; module's expansion of derive-object needs to resolve these, same
    ;; pattern as (srfi s227 optional-arguments)'s %opt-bind/
    ;; %opt-bind-optional.
    %install-object-slots! %install-extra-parents!)
  (begin

;;; =========================================================================
;;; Small local helpers (kept self-contained, matching how e.g.
;;; (srfi s1 lists) doesn't depend on other srfi libraries either)
;;; =========================================================================

(define (%any pred lst)
  (and (pair? lst) (or (pred (car lst)) (%any pred (cdr lst)))))

(define (%filter-map f lst)
  (let loop ((lst lst) (acc '()))
    (if (null? lst)
        (reverse acc)
        (let ((r (f (car lst))))
          (loop (cdr lst) (if r (cons r acc) acc))))))

;;; =========================================================================
;;; Slots
;;; =========================================================================

(define-record-type <proto-slot>
  (%make-slot type getter setter value)
  proto-slot?
  (type   proto-slot-type)
  (getter proto-slot-getter)
  (setter proto-slot-setter)
  (value  proto-slot-value %set-slot-value!))

(define slot? proto-slot?)
(define slot-getter proto-slot-getter)
(define slot-setter proto-slot-setter)
(define slot-type proto-slot-type)

;;; =========================================================================
;;; Objects: a procedure closing over a private, mutable slot table.
;;; The table maps message-name -> slot record; a slot with a setter is
;;; reachable under BOTH its getter and setter name.
;;; =========================================================================

(define %introspect (list 'srfi-263-introspect))

(define (%make-bare-object)
  (let ((slots (make-hash-table)))
    (letrec ((obj (lambda (message . args)
                    (if (eq? message %introspect)
                        slots
                        (%send obj message args)))))
      obj)))

(define (%object-slots obj) (obj %introspect))

(define (%install-slot! obj type getter setter value)
  (let ((s (%make-slot type getter setter value)))
    (hash-table-set! (%object-slots obj) getter s)
    (if setter (hash-table-set! (%object-slots obj) setter s))
    s))

(define (%local-slot obj message)
  (hash-table-ref (%object-slots obj) message #f))

(define (%parent-slots obj)
  ;; Every (key . slot) pair in the table where key is that slot's OWN
  ;; getter name (so a setter-keyed duplicate entry isn't counted twice).
  (%filter-map (lambda (kv)
                 (let ((k (car kv)) (v (cdr kv)))
                   (and (eq? (proto-slot-type v) 'parent) (eq? k (proto-slot-getter v)) v)))
               (hash-table->alist (%object-slots obj))))

;;; ---- Search: find-slot / search-parents ----
;;; Both return (list slot-or-'ambiguous-or-#f found-origin-or-#f).

;; Diamond inheritance: two different immediate parents can both
;; transitively resolve to the SAME slot record (e.g. both eventually
;; reach *the-root-object*'s own message-not-understood/
;; ambiguous-message-send handler, or any other common ancestor). That's
;; not a genuine ambiguity -- it's one slot, reachable by more than one
;; path -- so hits are deduped by slot identity (eq?) before counting.
;; Without this, sending an unhandled/genuinely-ambiguous message to an
;; object with >1 parent recurses forever: %deliver's ambiguous branch
;; re-sends 'ambiguous-message-send, whose own search across those same
;; >1 parents converges on root's single ambiguous-message-send slot,
;; which -- undeduped -- looks like >1 hits again, forever.
(define (%dedupe-hits hits)
  (let loop ((hits hits) (seen '()) (acc '()))
    (cond
      ((null? hits) (reverse acc))
      ((%any (lambda (s) (eq? s (caar hits))) seen) (loop (cdr hits) seen acc))
      (else (loop (cdr hits) (cons (caar hits) seen) (cons (car hits) acc))))))

;; `visited`: objects already on the current search path, most-recent
;; first. Without this, a cyclic parent graph (reachable from ordinary
;; Scheme code via the public `set-parent-slot!` message -- nothing
;; stops `(o 'set-parent-slot! 'parent #f o)`, or a longer cycle through
;; several objects) recurses without ever bottoming out, crashing the
;; process with a native stack overflow instead of raising a Scheme-level
;; error. A parent already in `visited` is treated the same as "no slot
;; here" (a dead end for this branch), not specially reported -- the
;; SRFI has nothing to say about cycles, so silently refusing to loop
;; through one is the conservative choice.
(define (%search-parents obj message visited)
  (let* ((pslots (%parent-slots obj))
         (results (map (lambda (p)
                          (let ((parent (proto-slot-value p)))
                            (if (memq parent visited)
                                (list #f #f)
                                (%find-slot parent message (cons parent visited)))))
                        pslots))
         (hits (filter car results)))
    (cond
      ((null? hits) (list #f #f))
      ((%any (lambda (r) (eq? (car r) 'ambiguous)) hits) (list 'ambiguous #f))
      (else
       (let ((distinct (%dedupe-hits hits)))
         (if (pair? (cdr distinct)) (list 'ambiguous #f) (car distinct)))))))

(define (%find-slot obj message visited)
  (let ((local (%local-slot obj message)))
    (if local (list local obj) (%search-parents obj message visited))))

;;; ---- Delivery: turn a search result into an actual value/effect ----

(define (%deliver self result message args)
  ;; message-not-understood/ambiguous-message-send are themselves method
  ;; slots, so %invoke below will auto-prepend self/resend -- the args
  ;; list here only needs the two "real" arguments the spec describes
  ;; (message args), not self again.
  ;;
  ;; Base case: normally message-not-understood/ambiguous-message-send is
  ;; always reachable (it's on *the-root-object*, and every object derives
  ;; from root eventually) -- but overwriting an object's own `parent`
  ;; slot (via set-parent-slot!, e.g. a self-referencing or otherwise
  ;; root-severing cycle) can cut root off entirely. Without this check,
  ;; failing to find message-not-understood would recurse into sending
  ;; message-not-understood again, forever. If the search for one of these
  ;; two fallback messages itself comes up empty or ambiguous, raise
  ;; directly instead of resending.
  (if (and (or (not (car result)) (eq? (car result) 'ambiguous))
           (memq message '(message-not-understood ambiguous-message-send)))
      (error "srfi-263: root unreachable (severed parent chain?) -- can't deliver" message args)
      (let ((slot (car result)) (origin (cadr result)))
        (cond
          ((eq? slot 'ambiguous) (%send self 'ambiguous-message-send (list message args)))
          ((not slot) (%send self 'message-not-understood (list message args)))
          (else (%invoke slot self origin message args))))))

(define (%make-resend self origin message default-args)
  (lambda (target-override . resend-args)
    (let ((use-args (if (null? resend-args) default-args resend-args)))
      (if target-override
          (%deliver self (%find-slot target-override message (list target-override)) message use-args)
          (%deliver self (%search-parents origin message (list origin)) message use-args)))))

(define (%invoke slot self origin message args)
  (case (proto-slot-type slot)
    ((value parent)
     (cond
       ((and (eq? message (proto-slot-getter slot)) (null? args))
        (proto-slot-value slot))
       ((and (proto-slot-setter slot) (eq? message (proto-slot-setter slot)) (pair? args) (null? (cdr args)))
        (%set-slot-value! slot (car args))
        (if #f #f))
       (else (error "srfi-263: wrong arity for value/parent slot" message args))))
    ((method)
     (apply (proto-slot-value slot) self (%make-resend self origin message args) args))
    (else (error "srfi-263: unknown slot type" (proto-slot-type slot)))))

(define (%send self message args) (%deliver self (%find-slot self message (list self)) message args))

;;; =========================================================================
;;; Root-level message bodies
;;; =========================================================================

(define (%parse-slot-args who args)
  (case (length args)
    ((2) (list (car args) #f (cadr args)))
    ((3) (list (car args) (cadr args) (caddr args)))
    (else (error (string-append "srfi-263: " who ": expected (getter [setter] value)") args))))

(define (%set-value-slot-body self resend . args)
  (let ((parsed (%parse-slot-args "set-value-slot!" args)))
    (%install-slot! self 'value (car parsed) (cadr parsed) (caddr parsed))
    (if #f #f)))

(define (%set-method-slot-body self resend . args)
  (let ((parsed (%parse-slot-args "set-method-slot!" args)))
    (%install-slot! self 'method (car parsed) (cadr parsed) (caddr parsed))
    (if #f #f)))

(define (%set-parent-slot-body self resend . args)
  (let ((parsed (%parse-slot-args "set-parent-slot!" args)))
    (%install-slot! self 'parent (car parsed) (cadr parsed) (caddr parsed))
    (if #f #f)))

(define (%delete-slot-body self resend name)
  (let* ((table (%object-slots self))
         (s (hash-table-ref table name #f)))
    (if s
        (begin
          (hash-table-delete! table (proto-slot-getter s))
          (if (proto-slot-setter s) (hash-table-delete! table (proto-slot-setter s)))))
    (if #f #f)))

(define (%derive-body self resend)
  (let ((new (%make-bare-object)))
    (%install-slot! new 'parent 'parent #f self)
    new))

(define (%copy-body self resend)
  ;; See the file-header note on "copy" for why this clones SELF's own
  ;; slots (not self's `parent` slot).
  (let ((new (%make-bare-object)))
    (for-each
      (lambda (kv)
        (let ((k (car kv)) (v (cdr kv)))
          (if (eq? k (proto-slot-getter v))
              (%install-slot! new (proto-slot-type v) (proto-slot-getter v) (proto-slot-setter v) (proto-slot-value v)))))
      (hash-table->alist (%object-slots self)))
    new))

;; These three walk the same parent graph %search-parents does, and need
;; the same cycle guard (see the comment on %search-parents) since a
;; mirror can be requested on any object, cyclic parent graph or not.

(define (%has-ancestor? obj candidate visited)
  (let ((ps (%parent-slots obj)))
    (%any (lambda (p)
            (let ((parent (proto-slot-value p)))
              (and (not (memq parent visited))
                   (or (eq? parent candidate)
                       (%has-ancestor? parent candidate (cons parent visited))))))
          ps)))

(define (%full-ancestor-list obj visited)
  (let* ((parents (%filter-map (lambda (p)
                                  (let ((parent (proto-slot-value p)))
                                    (and (not (memq parent visited)) parent)))
                                (%parent-slots obj))))
    (append parents
            (apply append (map (lambda (p) (%full-ancestor-list p (cons p visited))) parents)))))

(define (%full-slot-list obj visited)
  (let* ((immediate (map cdr (%filter-map
                               (lambda (kv) (and (eq? (car kv) (proto-slot-getter (cdr kv))) kv))
                               (hash-table->alist (%object-slots obj)))))
         (parents (%filter-map (lambda (p)
                                  (let ((parent (proto-slot-value p)))
                                    (and (not (memq parent visited)) parent)))
                                (%parent-slots obj))))
    (append immediate
            (apply append (map (lambda (p) (%full-slot-list p (cons p visited))) parents)))))

(define (%mirror-body self resend)
  (let ((m (%make-bare-object)))
    (%install-slot! m 'method 'has-ancestor #f
      (lambda (mself mresend candidate) (%has-ancestor? self candidate (list self))))
    (%install-slot! m 'method 'immediate-ancestor-list #f
      (lambda (mself mresend) (map proto-slot-value (%parent-slots self))))
    (%install-slot! m 'method 'full-ancestor-list #f
      (lambda (mself mresend) (%full-ancestor-list self (list self))))
    (%install-slot! m 'method 'immediate-slot-list #f
      (lambda (mself mresend) (map cdr (%filter-map
                                          (lambda (kv) (and (eq? (car kv) (proto-slot-getter (cdr kv))) kv))
                                          (hash-table->alist (%object-slots self))))))
    (%install-slot! m 'method 'full-slot-list #f
      (lambda (mself mresend) (%full-slot-list self (list self))))
    m))

(define (%message-not-understood-body self resend message args)
  (error "srfi-263: message not understood" message args))

(define (%ambiguous-message-send-body self resend message args)
  (error "srfi-263: ambiguous message send (found via multiple parents)" message args))

;;; =========================================================================
;;; The root object
;;; =========================================================================

(define *the-root-object*
  (let ((obj (%make-bare-object)))
    (%install-slot! obj 'method 'derive #f %derive-body)
    (%install-slot! obj 'method 'copy #f %copy-body)
    (%install-slot! obj 'method 'mirror #f %mirror-body)
    (%install-slot! obj 'method 'set-value-slot! #f %set-value-slot-body)
    (%install-slot! obj 'method 'set-method-slot! #f %set-method-slot-body)
    (%install-slot! obj 'method 'set-parent-slot! #f %set-parent-slot-body)
    (%install-slot! obj 'method 'delete-slot! #f %delete-slot-body)
    (%install-slot! obj 'method 'message-not-understood #f %message-not-understood-body)
    (%install-slot! obj 'method 'ambiguous-message-send #f %ambiguous-message-send-body)
    obj))

;;; =========================================================================
;;; Syntactic sugar
;;; =========================================================================

(define-syntax define-method
  (syntax-rules ()
    ((_ (obj message self resend . args) body ...)
     (obj 'set-method-slot! 'message (lambda (self resend . args) body ...)))))

;; Install one slot spec, either:
;;   ((message self resend . args) body ...)   -- method slot
;;   (getter setter value)                     -- value slot, with setter
;;   (getter value)                             -- value slot, getter only
;; The method-slot pattern MUST be tried first: syntax-rules pattern
;; variables match anything, so a flat `(getter value)` pattern would also
;; (wrongly) match a method spec, binding getter to the whole
;; `(message self resend . args)` sublist.
(define-syntax %install-object-slots!
  (syntax-rules ()
    ((_ obj) (if #f #f))
    ((_ obj ((message self resend . args) body ...) rest ...)
     (begin
       (obj 'set-method-slot! 'message (lambda (self resend . args) body ...))
       (%install-object-slots! obj rest ...)))
    ((_ obj (getter setter value) rest ...)
     (begin
       (obj 'set-value-slot! 'getter 'setter value)
       (%install-object-slots! obj rest ...)))
    ((_ obj (getter value) rest ...)
     (begin
       (obj 'set-value-slot! 'getter value)
       (%install-object-slots! obj rest ...)))))

(define-syntax %install-extra-parents!
  (syntax-rules ()
    ((_ obj n) (if #f #f))
    ((_ obj n p rest ...)
     (begin
       (obj 'set-parent-slot! (string->symbol (string-append "parent" (number->string n))) #f p)
       (%install-extra-parents! obj (+ n 1) rest ...)))))

(define-syntax derive-object
  (syntax-rules ()
    ((_ (parent other-parents ...) slots ...)
     (let ((%obj (parent 'derive)))
       (%install-extra-parents! %obj 2 other-parents ...)
       (%install-object-slots! %obj slots ...)
       %obj))))

(define-syntax copy-object
  (syntax-rules ()
    ((_ (parent other-parents ...) slots ...)
     (let ((%obj (parent 'copy)))
       (%install-extra-parents! %obj 2 other-parents ...)
       (%install-object-slots! %obj slots ...)
       %obj))))

(define-syntax define-object
  (syntax-rules ()
    ((_ name (parent other-parents ...) slots ...)
     (define name (derive-object (parent other-parents ...) slots ...)))))

  ))
