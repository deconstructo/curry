;;; Regression: modules.c's export-list filtering (modules_import(),
;;; branching on Module.has_exports) was fixed in commit de98abe to
;;; actually gatekeep what an (import ...) brings in, rather than
;;; discarding the declared (export ...) clause and importing every
;;; binding in a library's environment regardless of what it exported.
;;; That fix shipped with no negative test anywhere in the suite --
;;; every existing library test (r6rs_tests.scm's test-lib-a/b/c) only
;;; asserts that included/exported names ARE visible, never that a
;;; non-exported or only/except-excluded name ISN'T. This file closes
;;; that gap: own ctest entry (not folded into r7rs_tests.scm/
;;; r6rs_tests.scm) matching this suite's convention for scenarios
;;; worth isolating (see define_library_stack_guard_tests.scm's own
;;; header comment for the same reasoning), and because this is meant
;;; to be the regression gate for later work that changes HOW
;;; define-library bodies execute (compiled+VM-run vs tree-walked) --
;;; isolation semantics need to be independently, explicitly verified
;;; unaffected by that, not just implied by other tests passing.
;;;
;;; IMPORTANT test-design note, discovered while writing this file:
;;; (scheme base) (and the other (scheme *) libraries) alias the
;;; SAME flat GLOBAL_ENV (documented in CLAUDE.md's module-system
;;; section) -- they are NOT a filtered "core builtins only" import.
;;; So if a name X is ever imported at top level ANYWHERE earlier in
;;; the same process, every later (import (scheme base)) -- which is
;;; effectively every library, since that's how you get define/car/
;;; etc. -- transitively re-exposes that same X, regardless of
;;; whether the later library asked for it. This is expected, by-
;;; design behavior, not an isolation bug -- but it means a test like
;;; this one MUST use symbol names that are never reused across
;;; scenarios in the same process, or a later scenario's "is this
;;; name truly absent" assertion is meaningless (it'll appear present
;;; via GLOBAL_ENV pollution from an earlier scenario's own full
;;; import, not via any real leak in the scenario under test). Every
;;; library below therefore uses a unique name prefix.

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;; thunk must raise unbound-variable specifically -- not just "raise
;; something" (check-error-style helpers elsewhere in this suite, e.g.
;; xml_tests.scm, only check "did it raise at all"; a wrong error type
;; here would mask the export filter not actually doing its job, e.g.
;; if the name happened to resolve to something else by accident).
(define (check-unbound label thunk)
  (let ((result (guard (e (#t (if (and (error-object? e)
                                        (string-contains-unbound? (error-object-message e)))
                                   'unbound
                                   (list 'wrong-error e))))
                  (thunk)
                  'did-not-raise)))
    (check label result 'unbound)))

(define (string-contains-unbound? s)
  (let ((slen (string-length s)) (needle "unbound"))
    (let ((nlen (string-length needle)))
      (let loop ((i 0))
        (cond
          ((> (+ i nlen) slen) #f)
          ((string=? (substring s i (+ i nlen)) needle) #t)
          (else (loop (+ i 1))))))))

;;; ── R7RS define-library / import: full import ───────────────────────

(define-library (isolation r7-full)
  (import (scheme base))
  (export r7-full-exported)
  (begin
    (define (r7-full-exported) 'exported)
    (define (r7-full-internal) 'internal)))  ; never exported

(import (isolation r7-full))
(check "r7rs full import: exported binding visible" (r7-full-exported) 'exported)
(check-unbound "r7rs full import: non-exported binding NOT visible"
  (lambda () (r7-full-internal)))

;;; ── R7RS `only` ───────────────────────────────────────────────────────

(define-library (isolation r7-only-src)
  (import (scheme base))
  (export r7-only-included r7-only-excluded)
  (begin
    (define (r7-only-included) 'included)
    (define (r7-only-excluded) 'excluded)))

(define-library (isolation r7-only-user)
  (import (scheme base) (only (isolation r7-only-src) r7-only-included))
  (export r7-only-run)
  (begin
    (define (r7-only-run)
      (guard (e (#t 'unbound-as-expected)) (r7-only-excluded)))))

(import (isolation r7-only-user))
(check "r7rs only: excluded name not visible even inside the importing library"
  (r7-only-run) 'unbound-as-expected)
;; The included name should still work when imported directly at top level.
(import (only (isolation r7-only-src) r7-only-included))
(check "r7rs only: included name visible via direct top-level import"
  (r7-only-included) 'included)

;;; ── R7RS `except` ────────────────────────────────────────────────────

(define-library (isolation r7-except-src)
  (import (scheme base))
  (export r7-except-kept r7-except-dropped)
  (begin
    (define (r7-except-kept) 'kept)
    (define (r7-except-dropped) 'dropped)))

(define-library (isolation r7-except-user)
  (import (scheme base) (except (isolation r7-except-src) r7-except-dropped))
  (export r7-except-run)
  (begin
    (define (r7-except-run)
      (guard (e (#t 'unbound-as-expected)) (r7-except-dropped)))))

(import (isolation r7-except-user))
(check "r7rs except: excluded name not visible even inside the importing library"
  (r7-except-run) 'unbound-as-expected)
(import (except (isolation r7-except-src) r7-except-dropped))
(check "r7rs except: non-excluded name visible via direct top-level import"
  (r7-except-kept) 'kept)

;;; ── R6RS library / import: full import ──────────────────────────────

(library (isolation r6-full)
  (export r6-full-exported)
  (import (rnrs))
  (define (r6-full-exported) 'exported)
  (define (r6-full-internal) 'internal))

(import (isolation r6-full))
(check "r6rs full import: exported binding visible" (r6-full-exported) 'exported)
(check-unbound "r6rs full import: non-exported binding NOT visible"
  (lambda () (r6-full-internal)))

;;; ── R6RS `only` ──────────────────────────────────────────────────────

(library (isolation r6-only-src)
  (export r6-only-included r6-only-excluded)
  (import (rnrs))
  (define (r6-only-included) 'included)
  (define (r6-only-excluded) 'excluded))

(library (isolation r6-only-user)
  (export r6-only-run)
  (import (rnrs) (only (isolation r6-only-src) r6-only-included))
  (define (r6-only-run)
    (guard (e (#t 'unbound-as-expected)) (r6-only-excluded))))

(import (isolation r6-only-user))
(check "r6rs only: excluded name not visible even inside the importing library"
  (r6-only-run) 'unbound-as-expected)

;;; ── R6RS `except` ────────────────────────────────────────────────────

(library (isolation r6-except-src)
  (export r6-except-kept r6-except-dropped)
  (import (rnrs))
  (define (r6-except-kept) 'kept)
  (define (r6-except-dropped) 'dropped))

(library (isolation r6-except-user)
  (export r6-except-run)
  (import (rnrs) (except (isolation r6-except-src) r6-except-dropped))
  (define (r6-except-run)
    (guard (e (#t 'unbound-as-expected)) (r6-except-dropped))))

(import (isolation r6-except-user))
(check "r6rs except: excluded name not visible even inside the importing library"
  (r6-except-run) 'unbound-as-expected)

;;; ── Module registry concurrency (issue #143) ────────────────────────────
;;; modules.c's registry (the name-list -> Module* linked list backing
;;; every `import`) was read and written with zero synchronization,
;;; despite curry actors being real OS threads with no global interpreter
;;; lock -- registry_insert's unsynchronized "read old head, write new
;;; head" prepend could lose a concurrent registration, the same lost-
;;; update sx_rule_add had before #141's identical fix for sx_rules.c's
;;; rtab. Several actors concurrently importing modules must not crash or
;;; hang. (Deliberately run only under the default Boehm backend: the
;;; same underlying script -- concurrent module import with no sx_rules/
;;; sx_algebra involved at all -- was separately found to trip an
;;; unrelated, pre-existing corruption bug under `--gc generational`,
;;; confirmed to reproduce identically without this locking fix and
;;; tracked as issue #144, not something this test is meant to catch.)
(define (importer-loop n)
  (if (> n 0)
      (begin
        (import (srfi 1))
        (import (srfi 128))
        (importer-loop (- n 1)))))
(define mreg-a1 (spawn (lambda () (importer-loop 500))))
(define mreg-a2 (spawn (lambda () (importer-loop 500))))
(define mreg-a3 (spawn (lambda () (importer-loop 500))))
(define mreg-a4 (spawn (lambda () (importer-loop 500))))
(let mreg-wait ()
  (if (or (actor-alive? mreg-a1) (actor-alive? mreg-a2)
          (actor-alive? mreg-a3) (actor-alive? mreg-a4))
      (mreg-wait)))
(check "module registry concurrency: concurrent import across actors completes without crash or hang"
  #t #t)

;;; ── modules_import's no-export-list env walk (issue #148) ──────────────
;;; The test above imports srfi 1/128, which both declare explicit
;;; (export ...) clauses -- so it never actually exercises has_exports =
;;; false's code path (modules_import's raw f->syms[i]/f->vals[i] walk,
;;; TSan-confirmed racing frame_define's unsynchronized frame_grow/
;;; frame_hash_rehash). (scheme base) and its sibling aliases ARE
;;; registered with has_exports = false (modules_register_builtin,
;;; modules.c) and alias GLOBAL_ENV directly -- so importing (scheme
;;; base) walks GLOBAL_ENV's own frame chain via exactly the vulnerable
;;; path, and GLOBAL_ENV is the one frame actually reachable from more
;;; than one actor thread (env.c's own seqlock design doc). This test
;;; imports (scheme base) repeatedly from two actors while two other
;;; actors concurrently grow GLOBAL_ENV with fresh top-level defines
;;; (forcing real frame_grow/rehash calls mid-import), which reliably
;;; reproduced the race under ThreadSanitizer before the fix
;;; (frame_snapshot_bindings, env.c) and does not after.
(define (mreg148-definer-loop n tag)
  (if (> n 0)
      (begin
        (eval (list 'define
                     (string->symbol (string-append "mreg148-" (symbol->string tag) "-" (number->string n)))
                     n)
              (interaction-environment))
        (mreg148-definer-loop (- n 1) tag))))
(define (mreg148-importer-loop n)
  (if (> n 0)
      (begin
        (import (scheme base))
        (mreg148-importer-loop (- n 1)))))
(define mreg148-d1 (spawn (lambda () (mreg148-definer-loop 2000 'a))))
(define mreg148-d2 (spawn (lambda () (mreg148-definer-loop 2000 'b))))
(define mreg148-i1 (spawn (lambda () (mreg148-importer-loop 300))))
(define mreg148-i2 (spawn (lambda () (mreg148-importer-loop 300))))
(let mreg148-wait ()
  (if (or (actor-alive? mreg148-d1) (actor-alive? mreg148-d2)
          (actor-alive? mreg148-i1) (actor-alive? mreg148-i2))
      (mreg148-wait)))
(check "modules_import no-export-list walk: importing (scheme base) while GLOBAL_ENV grows concurrently completes without crash or hang"
  #t #t)

;;; ── C-module (.so/.dylib) concurrent first-import race (issue #149) ────
;;; modules_try_load's .so/.dylib branch used to lack the re-check step
;;; the .sld/.scm branches already had (checking the registry again after
;;; a successful load, before inserting, and preferring whatever's already
;;; there): two actors racing to first-import the same not-yet-loaded C
;;; module could each independently dlopen + run curry_module_init, then
;;; each registry_insert its own Module* -- whichever insert ran last won
;;; as the identity every future lookup resolves to, while the OTHER
;;; racing importer still kept and used the Module* it personally
;;; created, leaving actors bound to two different "singleton" module
;;; instances. Many actors each doing exactly one FIRST-EVER import of
;;; the same not-yet-loaded C module (curry json, unused elsewhere in
;;; this file, so this is a genuine first load) maximizes simultaneous
;;; race-window contention. Doesn't assert instance identity (no
;;; primitive exposes Module* to Scheme) -- like #141/#143/#148's own
;;; concurrency tests, this checks the fix doesn't introduce a NEW hazard
;;; (crash/hang) under heavy contention on the actual vulnerable path.
(define cmod149-actors
  (list (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))
        (spawn (lambda () (import (curry json))))))
(define (cmod149-all-done? lst)
  (or (null? lst)
      (and (not (actor-alive? (car lst))) (cmod149-all-done? (cdr lst)))))
(let cmod149-wait () (if (cmod149-all-done? cmod149-actors) 'done (cmod149-wait)))
(check "C-module concurrent first-import race: 16 actors racing to first-load the same .so module completes without crash or hang"
  #t #t)
;; Confirm the module actually works afterward regardless of which racing
;; actor's load "won" -- a basic functional smoke test, not just "didn't crash".
(check "C-module concurrent first-import race: the module is actually usable afterward"
  (json-stringify (list->vector (list 1 2 3))) "[1,2,3]")

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
