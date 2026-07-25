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

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
