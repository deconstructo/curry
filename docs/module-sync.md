# Module: (curry sync)

Low-level synchronisation primitives: mutex, condition variable, and counting semaphore, built directly over POSIX pthreads. No external library required.

> **macOS note.** macOS does not implement unnamed POSIX semaphores (`sem_init` always fails). The semaphore type in this module is therefore implemented with a `pthread_mutex_t` + `pthread_cond_t` + counter — identical semantics to `sem_t`, but portable across Linux and macOS.

> **Prefer actors.** Curry's `spawn` / `send!` / `receive` actor model is the recommended concurrency abstraction. Use this module when you specifically need explicit mutex/condvar/semaphore control — e.g. to interoperate with C libraries or to build higher-level synchronisation structures.

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

## Resource management

Handles are plain pair/bytevector values with no GC finalizer. The underlying pthread resource is heap-allocated with `malloc`. Call the corresponding `*-destroy!` procedure before releasing a handle to avoid resource leaks.

`with-mutex` is exception-safe: the mutex is always unlocked, even if the thunk raises. For condition variable and semaphore usage in potentially-raising code, wrap cleanup in `dynamic-wind` or `guard` manually.
