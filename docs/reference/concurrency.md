# Concurrency in Curry

*v1.2.0 — 2026-06-06*

Curry provides three complementary concurrency models. Choose the one that
matches your problem:

| Model | Best for | Import |
|-------|----------|--------|
| **Actors** (`spawn` / `send!` / `receive`) | Independent processes communicating by message | built-in |
| **Channels** (`make-channel` / `channel-send!` / `channel-recv!`) | Producer/consumer pipelines, streaming data | built-in |
| **STM** (`atomically` / `tvar-read` / `tvar-write!`) | Shared mutable state with composable transactions | built-in; `(import (curry stm))` for macros |
| **Low-level sync** (`make-mutex` / `make-condvar` / `make-semaphore`) | C interop, building higher-level primitives | `(import (curry sync))` |

All four are available in every build — no compile flags required. Actors,
channels, and STM share the same Boehm GC heap, so values can be passed
between threads without copying.

---

## Actors

Actors are the recommended default for independent concurrent processes.
Each actor is a thread with a private mailbox; communication is exclusively
via message passing.

```scheme
; Spawn an actor from a thunk
(define worker
  (spawn (lambda ()
    (let loop ()
      (let ((msg (receive)))
        (display msg) (newline)
        (loop))))))

(send! worker "hello")
(send! worker 42)

(self)             ; => current actor handle (or #f outside an actor)
(actor-alive? a)   ; => #t / #f
```

See the [language reference](language.md#actor-model) for the full actor API.

---

## Channels

CSP-style buffered channels for producer/consumer pipelines. Unlike actors,
channels are **values** — they can be stored in variables, passed as
arguments, and shared across multiple senders and receivers.

All channel primitives are in the global environment; no import is needed.

### `(make-channel [capacity])` → *channel*

Create a buffered channel. `capacity` (default 0) is the number of values
the channel can hold before a sender blocks.

- `capacity = 0` — synchronous rendezvous: `channel-send!` blocks until a
  receiver is ready, and vice versa.
- `capacity > 0` — buffered: senders block only when the buffer is full.

```scheme
(define ch  (make-channel 8))   ; buffered, capacity 8
(define rch (make-channel))     ; synchronous rendezvous
```

### `(channel-send! ch val)` → *void*

Send `val` to channel `ch`. Blocks if the channel is full (or if it is a
rendezvous channel and no receiver is waiting). Raises an error if the
channel is closed.

### `(channel-recv! ch)` → *val*

Receive the next value from `ch`. Blocks until a value is available. Raises
an error if the channel is closed and empty.

### `(channel-close! ch)` → *void*

Close the channel. Any further `channel-send!` raises an error. Pending
receivers are woken and will raise an error once the buffer is drained.

### `(channel-closed? ch)` → *boolean*

Returns `#t` if the channel has been closed.

### `(channel? v)` → *boolean*

Returns `#t` if `v` is a channel.

### Basic pipeline example

```scheme
(define ch (make-channel 4))

; Producer actor
(spawn (lambda ()
  (let loop ((i 0))
    (channel-send! ch i)
    (loop (+ i 1)))))

; Consumer (main thread)
(let loop ()
  (display (channel-recv! ch))
  (newline)
  (loop))
```

### Parallel map with a channel

```scheme
(define (pmap f lst)
  (define ch (make-channel (length lst)))
  (for-each (lambda (x) (spawn (lambda () (channel-send! ch (f x))))) lst)
  (map (lambda (_) (channel-recv! ch)) lst))

(pmap (lambda (x) (* x x)) '(1 2 3 4 5))
; => (1 4 9 16 25)  (order may vary)
```

### Non-blocking operations

Two internal primitives support non-blocking access (used by `select` — see
below). They return `V_UNDEF` (tested with `%channel-blocked?`) when the
operation would block:

```scheme
(%channel-try-send ch val)   ; => void (success) or undef (would block)
(%channel-try-recv ch)       ; => val  (success) or undef (would block)
(%channel-blocked? v)        ; => #t if v is the would-block sentinel
```

These are low-level. Prefer `select` for multi-channel coordination.

---

## `select` — multi-channel coordination

`select` polls multiple channels non-blockingly and runs the first ready
clause. Import `(curry stm)` for the macro.

```scheme
(import (curry stm))

(select
  (recv ch1 v  (display v) (newline))
  (send ch2 42)
  (else        (display "nothing ready\n")))
```

Each clause is one of:

| Clause | Meaning |
|--------|---------|
| `(recv ch var body ...)` | Receive from `ch`; bind to `var`; run `body` |
| `(send ch val)` | Send `val` to `ch` |
| `(else body ...)` | Run `body` if no other clause is immediately ready |

If no clause is ready and there is no `(else ...)`, `select` calls
`(retry)` — it **must** be wrapped in `(atomically ...)` to block correctly
rather than busy-spin:

```scheme
(atomically (lambda ()
  (select
    (recv ch1 v (process-from-ch1 v))
    (recv ch2 v (process-from-ch2 v)))))
  ; blocks until ch1 or ch2 has a value
```

---

## Software Transactional Memory (STM)

STM is the right tool for **shared mutable state** where you need
atomicity across multiple variables without explicit locking. It implements
the TL2 algorithm (Dice, Shalev, Shavit, DISC 2006).

All STM primitives are in the global environment. Import `(curry stm)` for
the `or-else` and `select` macros.

### Transactional variables

#### `(make-tvar val)` → *tvar*

Create a transactional variable with initial value `val`.

#### `(tvar-read tv)` → *val*

Read the current value of `tv`. Inside a transaction, reads the
transaction's own pending write if one exists; otherwise snapshots the
committed value and records it in the read-set. Outside a transaction,
returns the committed value directly.

#### `(tvar-write! tv val)` → *void*

Set the pending value of `tv` to `val`. Inside a transaction, the write is
buffered and applied atomically on commit. Outside a transaction, writes
directly and notifies any waiting transactions.

#### `(tvar? v)` → *boolean*

Returns `#t` if `v` is a transactional variable.

### Transactions

#### `(atomically thunk)` → *val*

Run `thunk` (a zero-argument procedure) as an atomic transaction. The
transaction:

1. Snapshots the global version clock at start.
2. Records every `tvar-read` in a read-set.
3. Buffers every `tvar-write!` in a write-set.
4. On completion: locks write-set tvars in address order (deadlock
   prevention), validates that no read-set tvar was modified by another
   transaction since the snapshot, applies writes, then unlocks.
5. If validation fails (a conflict), the transaction restarts automatically
   from the beginning.

`atomically` is **composable**: nested calls flatten into the enclosing
transaction. Exceptions raised inside a transaction are propagated normally
after the transaction state is torn down.

```scheme
(define balance (make-tvar 100))

(atomically (lambda ()
  (tvar-write! balance (- (tvar-read balance) 30))))

(tvar-read balance)  ; => 70
```

#### `(retry)` → *never*

Block the current transaction until at least one tvar in the read-set is
modified by another transaction, then restart the `atomically` body. Must
be called inside `atomically`.

Use `retry` to express conditions: "I can only proceed when this tvar has a
certain value".

```scheme
; Blocking read: wait until balance >= amount, then deduct
(define (withdraw! balance amount)
  (atomically (lambda ()
    (let ((b (tvar-read balance)))
      (when (< b amount) (retry))
      (tvar-write! balance (- b amount))))))
```

#### `(or-else e1 e2 ...)` — macro (requires `(import (curry stm))`)

Try `e1`; if it calls `(retry)`, try `e2`; and so on. If all alternatives
retry, the combined read-set of all attempts is used to wait. Must be used
inside `atomically`.

```scheme
(import (curry stm))

; Take from whichever queue is non-empty first
(define (take-either! q1 q2)
  (atomically (lambda ()
    (or-else
      (let ((v (tvar-read q1)))
        (when (null? v) (retry))
        (tvar-write! q1 (cdr v))
        (car v))
      (let ((v (tvar-read q2)))
        (when (null? v) (retry))
        (tvar-write! q2 (cdr v))
        (car v))))))
```

#### `(%or-else thunk1 thunk2)` → *val*

C-level primitive underlying `or-else`. Takes two zero-argument procedures.
Use the `or-else` macro for more than two alternatives.

### Concurrent counter example

```scheme
(define counter (make-tvar 0))
(define done    (make-tvar 0))

(define (increment! n)
  (atomically (lambda ()
    (tvar-write! counter (+ (tvar-read counter) 1)))))

; Launch 10 concurrent actors, each incrementing 1000 times
(let loop ((i 10))
  (when (> i 0)
    (spawn (lambda ()
      (let lp ((j 1000))
        (when (> j 0)
          (increment! 1)
          (lp (- j 1))))
      (atomically (lambda ()
        (tvar-write! done (+ (tvar-read done) 1))))))
    (loop (- i 1))))

; Wait for all actors to finish
(atomically (lambda ()
  (when (< (tvar-read done) 10) (retry))))

(display (tvar-read counter))  ; => 10000
(newline)
```

### STM vs actors vs channels

| Scenario | Recommended model |
|----------|------------------|
| Independent tasks, no shared state | Actors |
| Ordered data flow, pipelines | Channels |
| Shared mutable state, compound updates | STM |
| Wait on multiple channels at once | Channels + `select` |
| Conditional blocking on shared state | STM + `retry` |
| "Transfer from A to B atomically" | STM (two tvars, one `atomically`) |

STM and channels **compose with actors**: actors can read/write tvars and
send/receive on channels freely. The GC heap is shared, so no copying is
needed.

### Correctness guarantees

- **Atomicity**: either all writes in a transaction commit, or none do.
- **Isolation**: a transaction sees a consistent snapshot; concurrent writes
  are invisible until they commit.
- **Consistency**: the global version clock ensures a total order on commits.
- **No deadlock**: write-set locks are acquired in address order; `retry`
  releases all locks before sleeping.
- **Livelock**: under very high contention, transactions may repeatedly
  conflict. Use channels or actors for high-throughput streaming; reserve
  STM for low-contention shared state.

---

## Choosing between the models

```
Need to fire-and-forget a concurrent task?
  → spawn an actor

Need ordered delivery of values from one thread to another?
  → channel (buffered or rendezvous)

Need to wait on whichever of N channels delivers first?
  → select inside atomically

Need to update multiple variables atomically?
  → atomically with tvars

Need to wait until a condition on shared state becomes true?
  → retry inside atomically

Need a mutex/semaphore for C interop or raw pthread control?
  → (import (curry sync))
```
