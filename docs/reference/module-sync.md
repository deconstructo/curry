# Module: (curry sync)

*v1.2.2 — 2026-06-07*

Low-level synchronisation primitives: mutex, condition variable, counting semaphore, read-write lock, once and barrier, built directly over POSIX pthreads. No external library required.

> **macOS note.** macOS does not implement unnamed POSIX semaphores (`sem_init` always fails). The semaphore type in this module is therefore implemented with a `pthread_mutex_t` + `pthread_cond_t` + counter — identical semantics to `sem_t`, but portable across Linux and macOS.

> **Consider higher-level models first.** Curry provides actors (`spawn` / `send!` / `receive`), CSP channels (`make-channel` / `channel-send!` / `channel-recv!`), and Software Transactional Memory (`atomically` / `tvar-read` / `tvar-write!`). Use this module when you specifically need explicit mutex/condvar/semaphore control — e.g. to interoperate with C libraries or to build higher-level synchronisation structures. See the [concurrency reference](concurrency.md) for guidance.

## Import

```scheme
(import (curry sync))
```

## Mutex

### `(make-mutex)` → *mutex*

Create and initialise a new mutex.

### `(mutex-destroy! mx)`

Destroy the mutex and free its memory. Do not use the handle after this call.

### `(mutex-lock! mx)`

Acquire the mutex, blocking until it is available.

### `(mutex-unlock! mx)`

Release the mutex.

### `(mutex-trylock! mx)` → *boolean*

Try to acquire the mutex without blocking. Returns `#t` on success, `#f` if the mutex is already locked.

### `(mutex? x)` → *boolean*

Returns `#t` if `x` is a mutex handle.

### `(with-mutex mutex thunk)`

Lock `mutex`, call `thunk` with no arguments, then unlock. Returns the thunk's result. The unlock is guaranteed even if `thunk` raises an exception — the exception is re-raised after the mutex is released, so the lock is never left held.

```scheme
(define mx (make-mutex))
(with-mutex mx (lambda () (display "critical section") (newline)))
(mutex-destroy! mx)

; Exception safety: mx is unlocked even if thunk errors
(guard (e (#t (display "caught\n")))
  (with-mutex mx (lambda () (error "boom"))))
```

## Condition variable

### `(make-condvar)` → *condvar*

Create and initialise a new condition variable.

### `(condvar-destroy! cv)`

Destroy the condvar and free its memory.

### `(cond-wait! cv mutex)`

Atomically release `mutex` and block until `cv` is signalled. Re-acquires `mutex` before returning.

### `(cond-wait-timeout! cv mutex seconds)` → *boolean*

Like `cond-wait!` but with a timeout of `seconds` (real number). Returns `#t` if signalled, `#f` if timed out.

### `(cond-signal! cv)`

Wake one thread waiting on `cv`.

### `(cond-broadcast! cv)`

Wake all threads waiting on `cv`.

### `(condvar? x)` → *boolean*

Returns `#t` if `x` is a condvar handle.

### Producer/consumer example

```scheme
(import (curry sync))

(define mx    (make-mutex))
(define ready (make-condvar))
(define item  #f)

(define producer
  (spawn (lambda ()
    (let loop ((n 0))
      (with-mutex mx
        (lambda ()
          (set! item n)
          (cond-signal! ready)))
      (loop (+ n 1))))))

(define consumer
  (spawn (lambda ()
    (let loop ()
      (mutex-lock! mx)
      (cond-wait! ready mx)
      (display item) (newline)
      (mutex-unlock! mx)
      (loop)))))
```

## Counting semaphore

### `(make-semaphore [initial])` → *semaphore*

Create a counting semaphore with the given initial value (default 0).

### `(semaphore-destroy! sem)`

Destroy the semaphore and free its memory.

### `(sem-wait! sem)`

Decrement the semaphore, blocking if the count is zero.

### `(sem-post! sem)`

Increment the semaphore, waking a waiting thread if any.

### `(sem-trywait! sem)` → *boolean*

Try to decrement the semaphore without blocking. Returns `#t` on success, `#f` if the count is zero.

### `(sem-value sem)` → *fixnum*

Return the current count of the semaphore.

### `(semaphore? x)` → *boolean*

Returns `#t` if `x` is a semaphore handle.

## Read-write lock

Any number of readers, or one writer. It is not recursive: a thread that takes a lock it already holds for writing gets an error on macOS and with glibc, and may deadlock elsewhere. Unlocking a lock the thread does not hold is undefined.

### `(make-rwlock)` → *rwlock*, `(rwlock-destroy! rw)`

Create a read-write lock; destroy it and free its memory.

### `(rwlock-read-lock! rw)`, `(rwlock-write-lock! rw)`

Take the lock for reading, or for writing, blocking until it is available.

### `(rwlock-try-read-lock! rw)`, `(rwlock-try-write-lock! rw)` → *boolean*

Try without blocking. `#t` if the lock was taken, `#f` if it was not available (including when this thread holds it for writing).

### `(rwlock-unlock! rw)`

Release a read or a write lock held by this thread.

### `(with-read-lock rw thunk)`, `(with-write-lock rw thunk)`

Take the lock, call `thunk`, release it, and return the thunk's value. The lock is released on a normal return, when the thunk raises (the exception is raised again afterwards) and when a continuation escapes from it.

```scheme
(define rw (make-rwlock))
(define table (make-hash-table equal?))
(define (lookup k)   (with-read-lock  rw (lambda () (hash-table-ref table k #f))))
(define (store! k v) (with-write-lock rw (lambda () (hash-table-set! table k v))))
```

### `(rwlock? x)` → *boolean*

## Once

Run an initialisation exactly once, from any number of threads.

### `(make-once)` → *once*, `(once-destroy! o)`

### `(once! o thunk)` → *boolean*

If `o` has not been used up, call `thunk` and return `#t`. Other threads that call `once!` meanwhile wait until `thunk` has finished and then return `#f`, as does every later call. If `thunk` raises, or a continuation escapes from it, `o` is **not** used up: the exception propagates to this caller, and the next caller (a waiting one first) runs its own thunk. Calling `once!` on `o` from inside its own thunk raises an error instead of deadlocking.

```scheme
(define config-once (make-once))
(define config #f)
(define (get-config)
  (once! config-once (lambda () (set! config (load-config))))
  config)
```

### `(once-done? o)` → *boolean*, `(once? x)` → *boolean*

## Barrier

### `(make-barrier n)` → *barrier*, `(barrier-destroy! b)`

A barrier for `n` threads, `n` from 1 to 1048576.

### `(barrier-wait! b)` → *boolean*

Block until `n` threads have called `barrier-wait!`, then release all of them. The barrier can be used again. One of the `n` threads gets `#t` (it may do work that must be done once per round), the others `#f`. Built on a mutex and a condition variable, because macOS has no `pthread_barrier_t`.

```scheme
(define bar (make-barrier 4))
;; in each of 4 threads:
(compute-my-part)
(barrier-wait! bar)        ; nobody continues until all four have finished
(use-everybody's-parts)
```

### `(barrier-parties b)` → *fixnum*, `(barrier? x)` → *boolean*

## Resource management

Handles are plain pair/bytevector values with no GC finalizer. The underlying pthread resource is heap-allocated with `malloc`. Call the corresponding `*-destroy!` procedure before releasing a handle to avoid resource leaks.

`with-mutex`, `with-read-lock` and `with-write-lock` are exception-safe: the lock is always released, even if the thunk raises or a continuation escapes from it. For condition variable and semaphore usage in potentially-raising code, wrap cleanup in `dynamic-wind` or `guard` manually.
