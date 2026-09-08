;;; Actor concurrency tests — requires (curry sync) for synchronization

(import (curry sync))

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

;;; Basic actor creation
;;; Spawn an actor that waits for a semaphore so we can test actor-alive?
(define sem0 (make-semaphore 0))
(define a1 (spawn (lambda () (sem-wait! sem0))))
(check "actor? yes"         (actor? a1) #t)
(check "actor? no"          (actor? 42) #f)
(check "actor-alive? live"  (actor-alive? a1) #t)
(sem-post! sem0)  ; let it finish

;;; Actor sets result via shared mutable variable; semaphore signals completion
;;; sync module names: sem-wait!, sem-post!, make-semaphore
(define result1 #f)
(define sem1 (make-semaphore 0))
(spawn (lambda ()
         (set! result1 (* 6 7))
         (sem-post! sem1)))
(sem-wait! sem1)
(check "actor computes result" result1 42)

;;; Actor that accumulates a sum
(define result2 #f)
(define sem2 (make-semaphore 0))
(spawn (lambda ()
         (let loop ((i 1) (acc 0))
           (if (> i 100)
               (begin (set! result2 acc) (sem-post! sem2))
               (loop (+ i 1) (+ acc i))))))
(sem-wait! sem2)
(check "actor loop sum 1..100" result2 5050)

;;; Multiple actors all posting to same semaphore
(define sem3 (make-semaphore 0))
(define n-actors 5)
(let loop ((i 0))
  (when (< i n-actors)
    (spawn (lambda () (sem-post! sem3)))
    (loop (+ i 1))))
(let loop ((i 0))
  (when (< i n-actors)
    (sem-wait! sem3)
    (loop (+ i 1))))
(check "multiple actors all complete" #t #t)

;;; send! doesn't crash
(define a2 (spawn (lambda () 'quick)))
(send! a2 'hello)
(check "send! no crash" #t #t)

;;; (receive) — the actor-mailbox primitive, zero args. This shares its name
;;; with the R7RS `receive` special form (formals producer body...); the
;;; compiler must disambiguate by argument count instead of always compiling
;;; a bare `(receive ...)` as the special form, which previously segfaulted
;;; on args this shape (see solar-system-qt6.scm's actor loops).
(define result-recv0 #f)
(define sem-recv0 (make-semaphore 0))
(define a3 (spawn (lambda ()
                     (set! result-recv0 (receive))
                     (sem-post! sem-recv0))))
(send! a3 'mailbox-msg)
(sem-wait! sem-recv0)
(check "(receive) zero-arg mailbox primitive" result-recv0 'mailbox-msg)

;;; (receive timeout) — the primitive's one-arg form (also disambiguated by
;;; argument count, not just zero vs. nonzero).
(define result-recv1 #f)
(define sem-recv1 (make-semaphore 0))
(define a4 (spawn (lambda ()
                     (set! result-recv1 (receive 5000))
                     (sem-post! sem-recv1))))
(send! a4 'timed-msg)
(sem-wait! sem-recv1)
(check "(receive timeout) one-arg mailbox primitive" result-recv1 'timed-msg)

;;; Mutex: protected shared counter
;;; sync module names: make-mutex, mutex-lock!, mutex-unlock!
(define mtx (make-mutex))
(define shared-count 0)
(define sem4 (make-semaphore 0))
(define n-incr 50)

(let loop ((i 0))
  (when (< i n-incr)
    (spawn (lambda ()
             (mutex-lock! mtx)
             (set! shared-count (+ shared-count 1))
             (mutex-unlock! mtx)
             (sem-post! sem4)))
    (loop (+ i 1))))

(let loop ((i 0))
  (when (< i n-incr)
    (sem-wait! sem4)
    (loop (+ i 1))))

(check "mutex protected counter" shared-count n-incr)

;;; Condvar signal/wait
;;; sync module names: make-condvar, cond-wait!, cond-signal!
(define cv-mtx  (make-mutex))
(define cv      (make-condvar))
(define cv-result #f)
(define sem5 (make-semaphore 0))

(spawn (lambda ()
         (mutex-lock! cv-mtx)
         (cond-wait! cv cv-mtx)
         (set! cv-result 'signaled)
         (mutex-unlock! cv-mtx)
         (sem-post! sem5)))

(spawn (lambda ()
         (mutex-lock! cv-mtx)
         (cond-signal! cv)
         (mutex-unlock! cv-mtx)))

(sem-wait! sem5)
(check "condvar signal/wait" cv-result 'signaled)

;;; spawn + closure upvalue race — regression test.
;;;
;;; A closure handed to spawn can still have an upvalue open into the
;;; SPAWNING thread's live stack at the moment the new actor thread starts
;;; running it. If the spawning thread is a loop reusing that exact stack
;;; slot for its next iteration (which TCO does by design), the actor can
;;; read the wrong value with no error, no lock, nothing — a silent
;;; correctness bug, not a crash. Fixed by force-closing a spawned
;;; closure's open upvalues synchronously in the spawning thread, before
;;; the new thread starts (see vm_force_close_upvalue, actor_spawn).
;;;
;;; This spawns N actors from inside a tight tail-recursive loop, each
;;; capturing the loop variable and writing it into a distinct vector
;;; slot; every actor must see its own iteration's value, never a
;;; neighbor's. Before the fix, this reliably produced either a wrong
;;; value (silently) or a type-confused crash within a handful of runs.
(define N-race 2000)
(define race-results (make-vector N-race #f))
(define sem-race (make-semaphore 0))
(let loop ((i 0))
  (when (< i N-race)
    (spawn (lambda ()
             (vector-set! race-results i i)
             (sem-post! sem-race)))
    (loop (+ i 1))))
(let loop ((i 0))
  (when (< i N-race)
    (sem-wait! sem-race)
    (loop (+ i 1))))
(define race-wrong 0)
(let loop ((i 0))
  (when (< i N-race)
    (unless (equal? (vector-ref race-results i) i)
      (set! race-wrong (+ race-wrong 1)))
    (loop (+ i 1))))
(check "spawn from a loop: every actor captures its own iteration's value" race-wrong 0)

;;; spawn must not freeze a shared upvalue for same-thread sharers —
;;; regression test for a review finding against an earlier version of the
;;; fix above. An open upvalue is shared (by pointer) by every closure
;;; that captured the same variable from the same still-live scope; an
;;; in-place close (to make it safe to hand to a new actor thread) wrongly
;;; froze the value for ALL of them, not just the escaping one, breaking
;;; an ordinary same-thread sibling closure that should still observe a
;;; later set! to the shared variable. The fix instead gives the actor its
;;; own private snapshot, leaving the original (and anything else sharing
;;; it) untouched.
(define sem-sib (make-semaphore 0))
(define sib-result #f)
(define (make-sibling-test)
  (let ((counter 0))
    (define get-counter (lambda () counter))
    (spawn (lambda () (sem-post! sem-sib)))
    (sem-wait! sem-sib)
    (set! counter 999)
    (get-counter)))
(set! sib-result (make-sibling-test))
(check "spawn does not freeze a shared upvalue for same-thread sibling closures"
       sib-result 999)

;;; STM: concurrent tvar increments from several actors must all land.
;;; Companion smoke test for a torn-read fix in stm_tvar_read (src/stm.c):
;;; the read protocol used to check tv->version before reading tv->value
;;; but never re-check it afterward, so a commit landing in the window
;;; between those two reads was invisible to the reader. Fixed by
;;; re-reading the version after the value and retrying immediately on
;;; any mismatch. Independent review noted, honestly, that this
;;; particular test does NOT actually distinguish pre-fix from post-fix
;;; behavior: each transaction here is a single read-modify-write, so any
;;; torn read that occurs still only ever discards its own transaction
;;; via read-set validation at that transaction's own commit — it can
;;; never corrupt the final total, whether or not the version-recheck
;;; fix is present. A test that truly isolates the single-tvar torn-read
;;; window would need either a transaction body whose control flow
;;; diverges when handed torn data (raising before ever reaching
;;; commit-time validation — but constructing that without ALSO
;;; depending on this STM's separate, unaddressed opacity/extend-
;;; validation gap across multiple tvars turns out to be its own
;;; can of worms) or direct test-only instrumentation of stm_tvar_read.
;;; Kept anyway as a concurrent-load correctness smoke test (and because
;;; the fix itself was verified correct by close reading, independently,
;;; twice) rather than as true regression coverage for this specific
;;; defect. Kept modest (4 actors x 200 increments) since heavy
;;; contention on a single tvar is slow independent of correctness.
(define stm-counter (make-tvar 0))
(define stm-n-actors 4)
(define stm-n-incr 200)
(define stm-done-sem (make-semaphore 0))
(define (stm-worker)
  (let loop ((i 0))
    (if (< i stm-n-incr)
        (begin
          (atomically (lambda () (tvar-write! stm-counter (+ (tvar-read stm-counter) 1))))
          (loop (+ i 1)))
        (sem-post! stm-done-sem))))
(let loop ((i 0)) (when (< i stm-n-actors) (spawn stm-worker) (loop (+ i 1))))
(let loop ((i 0)) (when (< i stm-n-actors) (sem-wait! stm-done-sem) (loop (+ i 1))))
(check "STM: concurrent tvar increments across actors all land"
       (tvar-read stm-counter) (* stm-n-actors stm-n-incr))

;;; list-actors: global registry (introspection uplift work,
;;; docs/thoughts/introspection-uplift-plan.md) -- an actor is registered
;;; right before its thread starts and unregistered right as it exits.
(define la-done-sem (make-semaphore 0))
(define la-actor
  (spawn (lambda () (receive) (sem-post! la-done-sem))))
;; Other tests earlier in this file spawn many actors of their own (some
;; still winding down their own thread teardown at this point) -- check
;; presence, not that la-actor is the only entry.
(check "list-actors: a live actor appears"
       (if (member la-actor (list-actors)) #t #f) #t)
(send! la-actor 'go)
(sem-wait! la-done-sem)
;; Give the exiting thread a moment to reach actor_registry_remove --
;; sem-post! happens just before the actor closure returns and the
;; thread's own cleanup (which does the unregister) runs, so there's a
;; small window. No sleep-ms primitive exists in curry, so poll with a
;; real (non-trivial, not optimizable-away) amount of busy work between
;; checks instead of a tight spin on list-actors alone.
(define (la-busy-wait-a-bit)
  (let loop ((i 0) (acc 0)) (if (< i 50000) (loop (+ i 1) (+ acc i)) acc)))
(let loop ((tries 0))
  (when (and (member la-actor (list-actors)) (< tries 100))
    (la-busy-wait-a-bit)
    (loop (+ tries 1))))
(check "list-actors: no longer present after the actor exits"
       (member la-actor (list-actors)) #f)

;;; Regression: actor_registry_add used to run AFTER pthread_create
;;; returned, so a fast-exiting actor's own actor_registry_remove (run on
;;; its own thread, concurrently) could complete before the spawning
;;; thread ever got scheduled to run the add -- remove found nothing, add
;;; then inserted a permanently dead entry. Found by independent review:
;;; 500 immediately-returning actors leaked 368 stranded registry slots
;;; under the old post-create ordering. Fixed by registering before
;;; pthread_create instead of after.
;; Collect handles rather than asserting the whole registry is empty --
;; other tests earlier in this file may still have their own actors
;; mid-teardown at this point (benign, unrelated to this regression).
(define la-race-actors
  (let loop ((i 0) (acc '()))
    (if (< i 300) (loop (+ i 1) (cons (spawn (lambda () 1)) acc)) acc)))
;; Give every spawned thread a real chance to run to completion and
;; unregister itself -- same busy-wait approach as above, no sleep-ms
;; primitive available.
(let loop ((tries 0)) (when (< tries 300) (la-busy-wait-a-bit) (loop (+ tries 1))))
(check "list-actors: no permanently-stranded entries from fast-exiting actors"
       (length (filter (lambda (a) (member a (list-actors))) la-race-actors))
       0)

;;; Issue #162: vm.c's per-Chunk global-variable inline cache (glob_cache)
;;; -- consulted by OP_LOAD_GLOBAL/OP_STORE_GLOBAL/OP_DEF_GLOBAL and the
;;; shared load_global_cached() helper behind OP_CALL_GLOBAL/
;;; OP_TAIL_CALL_GLOBAL -- read and wrote its cache[ci].slot/.version
;;; fields as plain, unsynchronized memory accesses. root->version itself
;;; was already correctly acquire/release-ordered (matching env.c's
;;; seqlock), but nothing ordered the cache entry's own two fields
;;; against each other, so two actor threads executing the SAME compiled
;;; Chunk concurrently (the normal case for N actors spawned from one
;;; shared lambda literal, exercised below) could race: one thread's
;;; write to cache[ci].slot/.version could interleave with another's
;;; read with no happens-before between them, letting a reader observe a
;;; torn pair (a fresh version stamped next to a stale slot, or vice
;;; versa) and wrongly take the fast path through a slot that doesn't
;;; correspond to the value it appears to validate.
;;;
;;; Confirmed via ThreadSanitizer (not run as part of this suite -- no
;;; TSan build target exists in this project yet): the unfixed code
;;; reliably produced 6 data-race warnings per run at vm.c's
;;; load_global_cached/OP_LOAD_GLOBAL/OP_CALL_GLOBAL call sites (the same
;;; lines the issue itself reports) under 48 actors compiled from one
;;; shared lambda, each doing 20000 iterations of global-variable
;;; traffic; the fix (gcache_load/gcache_store, giving the cache entry's
;;; own fields the same acquire/release treatment root->version already
;;; had) reduced that to 0 warnings across repeated runs.
;;;
;;; Independent code review of that first fix then found it only
;;; protects one writer against concurrent readers, not one writer
;;; against another concurrent writer: two threads racing a cache-fill
;;; for the SAME entry (e.g. both miss around a concurrent GLOBAL_ENV
;;; frame_grow, so each computes a different (slot, version) pair for
;;; the same index) could still interleave their two independent field
;;; writes into a torn pair neither of them produced. Closed with a
;;; coarse per-Chunk spinlock (Chunk.glob_cache_lock, chunk.h) that
;;; serializes WRITES only -- reads stay lock-free through gcache_load.
;;; Re-verified under ThreadSanitizer with actors mixing concurrent
;;; top-level `(eval (list 'define ...))` (forcing repeated frame_grow)
;;; against concurrent global reads: 0 glob_cache-related warnings across
;;; 5 runs (this run does surface an unrelated, already-tracked race in
;;; env.c's own seqlock -- see issue #153 -- which is why this test
;;; doesn't attempt to force a frame_grow itself, to keep this a clean
;;; signal specifically for glob_cache).
;;;
;;; This test can't reproduce a TSan report itself, but it does exercise
;;; the exact shared-chunk-under-concurrent-global-lookup shape the race
;;; depends on and asserts every actor still computes the mathematically
;;; correct result despite the contention -- a torn cache read
;;; manifesting as a wrong-value result (rather than a crash) would fail
;;; this.
(define n-gcache-workers 24)
(define gcache-results (make-vector n-gcache-workers #f))
(define (gcache-worker id)
  (lambda ()
    (let loop ((i 0) (acc 0))
      (if (< i 2000)
          (loop (+ i 1) (+ acc (modulo (+ id i) 97)))
          (vector-set! gcache-results id acc)))))
(define (gcache-expected id)
  (let loop ((i 0) (acc 0))
    (if (< i 2000) (loop (+ i 1) (+ acc (modulo (+ id i) 97))) acc)))
(define gcache-actors
  (let loop ((i 0) (acc '()))
    (if (>= i n-gcache-workers) acc
        (loop (+ i 1) (cons (spawn (gcache-worker i)) acc)))))
(define (gcache-all-done? actors)
  (or (null? actors)
      (and (not (actor-alive? (car actors))) (gcache-all-done? (cdr actors)))))
(let loop ((tries 0))
  (when (and (< tries 2000) (not (gcache-all-done? gcache-actors)))
    (la-busy-wait-a-bit)
    (loop (+ tries 1))))
(check "glob_cache: every actor computed the correct result under concurrent shared-chunk global lookups"
       (let loop ((id 0))
         (cond ((>= id n-gcache-workers) #t)
               ((not (equal? (vector-ref gcache-results id) (gcache-expected id))) #f)
               (else (loop (+ id 1)))))
       #t)

;;; Issue #153: GLOBAL_ENV's seqlock (env.c) protects frame_grow/
;;; frame_hash_rehash's structural writes with a version counter whose OWN
;;; release/acquire ordering is C11-sound, but the PLAIN fields it guards
;;; (syms/vals/hidx/cap/hcap/size) were read/written non-atomically -- a
;;; data race by the letter of the C11 standard the instant a lock-free
;;; reader's optimistic read overlaps a writer's plain write in real time,
;;; regardless of whether the reader's later version check discards the
;;; result. A separate, more direct gap: frame_set (tree-walked `set!`)
;;; and vm.c's compiled OP_STORE_GLOBAL both wrote an existing global
;;; slot's VALUE with zero seqlock coverage at all (frame_set never bumps
;;; version, by design, since it's "no structural change").
;;;
;;; Confirmed via ThreadSanitizer (not run as part of this suite -- no
;;; TSan build target exists in this project yet): the unfixed code
;;; reliably produced 3-7 data-race warnings per run at exactly
;;; frame_grow/frame_hash_rehash/frame_lookup_unlocked (env.c) under 32
;;; actors mixing `(eval (list 'define ...))` (forcing frame_grow)
;;; against plain global reads; the fix (atomic-relaxed accessors on the
;;; global-frame path, gated on frame_is_global so local frames are
;;; untouched, plus gc_wb_slot_atomic_relaxed for the value-write path)
;;; reduced that to 0 warnings at this class across 5 repeated runs.
;;;
;;; This test can't reproduce a TSan report itself, but it exercises the
;;; same shape (concurrent define-forced frame_grow against concurrent
;;; reads AND writes of the SAME global) and asserts every actor still
;;; observes internally-consistent results despite the contention -- a
;;; torn read manifesting as a wrong value (rather than a crash) would
;;; fail this.
(define n153-workers 24)
(define g153-counter 0) ; shared global -- exercises frame_set's value race too
(define (worker153 id)
  (lambda ()
    (eval (list 'define (string->symbol (string-append "g153-" (number->string id))) id))
    (let loop ((i 0))
      (if (< i 500)
          (begin (set! g153-counter (+ g153-counter 0)) ; touches the shared slot's value
                 (loop (+ i 1)))))
    (eval (list 'set! 'g153-counter '(+ g153-counter 0)))))
(define actors153
  (let loop ((i 0) (acc '()))
    (if (>= i n153-workers) acc
        (loop (+ i 1) (cons (spawn (worker153 i)) acc)))))
(define (all-done153? actors)
  (or (null? actors)
      (and (not (actor-alive? (car actors))) (all-done153? (cdr actors)))))
(let loop ((tries 0))
  (when (and (< tries 2000) (not (all-done153? actors153)))
    (la-busy-wait-a-bit)
    (loop (+ tries 1))))
(check "env.c seqlock: every actor's own top-level define is visible and correctly valued after concurrent frame_grow"
       (let loop ((id 0))
         (cond ((>= id n153-workers) #t)
               ((not (equal? (eval (string->symbol (string-append "g153-" (number->string id)))) id)) #f)
               (else (loop (+ id 1)))))
       #t)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
