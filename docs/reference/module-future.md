# Module: (curry future)

Futures on the work-stealing thread pool that `map` and `for-each/par` use. A future runs a thunk on a pool worker; `future-get` waits for the result.

```scheme
(import (curry future))

(define f (pool-submit (lambda () (expensive-computation))))
;; ... do other work ...
(future-get f)                                   ; the value, or raises what the thunk raised

(define g (future-then f (lambda (v) (* v 2))))  ; runs when f is done
(future-get g)
```

Enable with `-DBUILD_MODULE_FUTURE=ON` (the default).

## Procedures

### `(pool-submit thunk)` → *future*

Queue `(thunk)` to run on a pool worker, and return at once. If every worker queue is full, the thunk runs in the calling thread before `pool-submit` returns.

### `(future-get f)` → *value*

Wait until `f` has finished and return the thunk's value, as often as it is asked. If the thunk raised, `future-get` raises the same object, as often as it is asked.

### `(future-wait f seconds)` → *boolean*

Wait for `f` for at most `seconds` (a real number). `#t` if it finished, `#f` if not. A timed wait never runs the thunk itself.

### `(future-done? f)` → *boolean*

`#t` if `f` has finished, by a value or by raising. Does not wait.

### `(future-then f proc)` → *future*

A future for `(proc value-of-f)`, queued when `f` has finished. If `f` raised, the new future fails with the same object and `proc` is not called. A chain of a million `future-then` links works (the links are walked with a worklist, not by recursion), whether the head succeeds or fails.

### `(future? x)` → *boolean*

## Waiting inside futures

A future that waits for another future does not starve the pool. `future-get` on a future nobody has started runs it in the calling thread (the pool finds it taken and skips it), and on a `future-then` future it first works on the future it waits for. So any number of futures can wait for futures they created, in a tree or a chain, with the pool smaller than the number of futures.

A future that another worker is already running is waited for. That can only hang if futures wait for each other in a cycle. A future that waits for itself is caught and raises an error; a longer cycle (A waits for B, B waits for A) is not, and hangs.

## Exceptions, continuations

The thunk runs with a handler of its own. What it raises becomes the future's result (also for `raise-continuable`: there is no handler outside to return a value to).

A thunk gets a private copy of the variables it captures, as a `spawn`ed thunk does: a thunk made in a loop sees the loop variable's value from when it was made, and a `set!` the thunk makes on a captured local is not seen by the code that made it (#448). Use a global, a box or a mutex-guarded structure to hand a value back, or just return it.

Continuations do not cross threads: a continuation captured outside the thunk and invoked from it raises `continuation-expired` if a worker runs the thunk, which fails the future. If `future-get` runs the thunk in the thread that holds the continuation, the escape works; the future is then marked failed ("the thunk was left by a continuation") so nobody waits for it for ever.

## Notes

- There is no way to cancel a future yet.
- `(exit n)` inside a thunk exits the whole process, as it does anywhere.
- A future is a Scheme vector tagged `%future`, so `vector?` is `#t` for it, and printing it shows its fields. Do not build or change one by hand. The collector frees a future when nothing refers to it: there is nothing to destroy.
- All futures share one mutex and one condition variable. Every completion wakes every waiting thread; they look at their own future and go back to sleep. Fine for hundreds of waiters, not for hundreds of thousands.
- On a pool worker, `map`, `reduce` and `for-each/par` in a thunk run sequentially, as they do in any worker (nested parallel dispatch would deadlock the pool).
- Raised objects are shared, not copied, like every value passed between threads.

See also the [concurrency reference](concurrency.md) and [parallel map/reduce](parallel.md).
