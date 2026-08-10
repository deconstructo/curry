# curry fibre — Stackful Coroutines for curry

*Design draft — 2026-08-10.*

## 1. What this is, and why now

A **fibre** (British spelling deliberately kept throughout — the module, if built, is `(curry fibre)`) is a *stackful coroutine*: a suspendable, resumable unit of execution with its own call stack, so it can suspend from arbitrarily deep inside nested function calls — not just from a function's own top-level body the way a generator's `yield` is normally restricted. PHP's [`Fiber`](https://www.php.net/manual/en/language.fibers.php) class is the reference point that prompted this document: `Fiber::suspend($value)` pauses the fibre and hands `$value` back to whoever called `->resume()`; that caller can later call `->resume($value)` again to continue the fibre from exactly where it suspended, handing a new value *in*.

**Why curry can't do this today.** curry's `call/cc` is explicitly upward-only escape (`setjmp`/`longjmp`; see the R7RS compliance gaps in `CLAUDE.md`) — it can jump *out* of a computation, never back *into* one. curry's only other "pause and come back to it" primitive is the actor system (`spawn`/`send!`/`receive`), which is real OS threads talking via message-passing — a correct but differently-shaped tool for what's often just "I want to write this as a generator/iterator/coroutine instead of a callback pyramid or a full actor."

**Why this doesn't need full first-class continuations.** Implementing genuine multi-shot `call/cc` is a much bigger undertaking — either a CPS-transformed evaluator or a copying/heap-allocated stack, the two standard techniques, both amounting to a rewrite of how `eval.c`/`vm.c` represent control state. A fibre only needs a *single* independent stack that control can be swapped into and out of — a narrower, much older, and much cheaper problem (this is how Lua coroutines, Ruby Fibers, Go goroutines, and now PHP's own Fiber are all implemented, in languages with no continuation-passing evaluator at all). This document is deliberately scoped to that narrower problem; full continuations remain a separate, independent, much later item and are not a prerequisite for anything here.

## 2. What curry already has that helps

Facts checked against `main` (August 2026):

| Mechanism | Where | Relevance to fibres |
|---|---|---|
| Per-thread VM state | `vm_init()`/`vm_free()` (`src/vm.c:245`), called at the top of every actor thread (`src/actors.c:114`) | A fibre needs exactly this: its own `VM` struct (`sp`, `stack`, `frame_count`, `open_upvalues`, `handler_count`) isolated from every other fibre's. |
| Thread-local exception chain | `current_handler` is `_Thread_local` (`src/eval.h:120`) | `guard`/`with-exception-handler` state is already correctly per-OS-thread — a fibre built as its own OS thread gets this isolation *for free*, with zero new bookkeeping. |
| Thread-local `current_actor` | `src/actors.c:15` | Same free-isolation argument extends to anything else keyed off "which execution context am I" that curry adds later. |
| Rendezvous mailbox | `Mailbox` (`src/actors.c:25-96`): `pthread_mutex`+`pthread_cond`, blocking push/pop | This is *exactly* the primitive a suspend/resume handoff needs — a single-slot version of it is a fibre's entire runtime mechanism (§4). |
| GC thread registration | `gc_register_thread()` called first thing in `actor_thread` (`src/actors.c:112`) | Any new OS thread (a fibre, under the thread-backed design) needs this too, and the call site already exists to copy. |

The throughline: curry's actor system already solved "give this independent thread of control its own correctly-isolated interpreter state." A thread-backed fibre design (§3, Candidate B) is almost entirely *reuse* of that, not new mechanism.

## 3. The central decision: what *is* a fibre's stack?

### The candidates

**A. True stack-switching within one OS thread** (`ucontext.h`'s `makecontext`/`swapcontext`, or a hand-rolled assembly trampoline in the style of Boost.Context/libaco) — a fibre gets a raw memory region as its stack, and switching fibres is a direct stack-pointer swap, no OS thread involved at all.

*Problems:* `ucontext` is deprecated on macOS since 10.5 (still present, still works, but emits deprecation warnings and needs `_XOPEN_SOURCE` — friction for a project that otherwise builds warning-clean on macOS/Linux both). A portable, non-deprecated version means per-architecture hand-written assembly (x86-64 and arm64, at minimum, to cover every platform curry already targets) — real, security-sensitive, hard-to-review C/asm, squarely the kind of code `CLAUDE.md`'s mandatory code+security review exists for, and a source of exactly the "array bounds vs loop bounds, off-by-one" class of bug that review is tuned to catch. Every one of curry's *other* thread-local state (`current_handler`, `current_actor`, the VM pointer) is genuinely thread-local via C `_Thread_local` — under this design, several fibres share *one real OS thread*, so all of that state would need to be manually saved and restored on every single switch, or it silently corrupts across fibres. That's new bookkeeping this candidate has to invent from scratch; nothing in curry today does it.

**B. One real OS thread per fibre, parked on a condition variable between suspend/resume** — a fibre *is* an actor thread in every respect (own stack, own `VM`, own `current_handler` chain, registered with the GC the same way), except it starts immediately blocked, and "resuming" it means signal-and-wait on a private single-slot rendezvous instead of pushing into an unbounded mailbox queue.

*Problems:* A real OS thread's default stack is heavyweight compared to a hand-sized coroutine stack — curry's actors currently request 8 MiB each (`pthread_attr_setstacksize`, `src/actors.c:204`); thousands of concurrent fibres at that size is thousands of 8 MiB reservations. (Reservation, not commit — most OSes only fault in pages as the stack actually grows — but it's still a real ceiling on how many fibres are practical, and a real difference from candidate A's few-KB-per-fibre cost.) Every fibre switch also crosses two real thread parks/wakes (`pthread_cond_wait`/`pthread_cond_signal`), non-trivial versus a few instructions for a raw stack-pointer swap.

**C. Defer to full first-class continuations** — implement `call/cc` properly (CPS or copying-stack), and let fibres (and generators, and everything else in this family) fall out as ordinary library code on top, the way most serious Scheme implementations do it.

*Problems:* This is the multi-month evaluator rewrite this whole document exists to route around. Not rejected — it's the *right* long-term answer, and it's already an independent, roadmapped, deferred item — but it answers a different, much bigger question than "can curry get stackful coroutines soon."

### Assessment

**Candidate B, ship first.** It needs zero new C code: `(curry fibre)` can be built as a pure-Scheme library directly on `spawn`/`send!`/`receive` (§4), inheriting every isolation guarantee curry's actors already have correctly (dynamic-wind, exception handlers, per-thread VM state) with no new C surface for the security/code review to even look at. Candidate A is real, and worth keeping as a named, deliberate *future* optimization once real usage shows candidate B's per-fibre weight actually matters in practice — but building it *first*, before anyone has used a fibre in anger, is optimizing a cost nobody's measured yet against a correctness/portability/review burden that's real today. Candidate C stays exactly where it already was: independent, valuable, not this document's problem.

This reverses nothing that's shipped — there is no existing fibre feature to be compatible with — so it's a green-field pick, not a breaking change, but the tradeoff is real enough (§3's table) to state plainly rather than pick silently.

## 4. v1 design: pure Scheme, actor-backed

### 4.1 The core trick

A fibre is an actor that starts parked, waiting for its first resume value. Suspending is "send my result to whoever resumed me, then block for the next value." Resuming is "send a value to the fibre, then block for its next suspend (or its final return)." This is a direct, symmetric rendezvous — exactly what `send!`+`receive` already do — with one wrinkle: the fibre needs to remember *which* actor most recently resumed it, so `fibre-suspend` knows where to send the yielded value back to. That's one mutable cell, updated on every resume message.

```scheme
(define-record-type <fibre>
  (%make-fibre actor status-box)
  fibre?
  (actor       fibre-actor)
  (status-box  fibre-status-box))   ; a mutable box: 'suspended | 'running | 'done | 'error

;; The fibre's own view of "who do I hand my next suspended value to" —
;; captured fresh on every resume, since in principle a fibre could be
;; resumed by a different caller each time (unusual, but not forbidden).
(define current-resumer (make-parameter #f))

(define (make-fibre thunk)
  (let* ((status (box 'suspended))
         (fibre-actor
           (spawn
             (lambda ()
               (let ((first-arg (receive)))  ; blocks until fibre-resume! sends the start value
                 (parameterize ((current-resumer (car first-arg)))
                   (set-box! status 'running)
                   (guard (e (#t (set-box! status 'error)
                                 (send! (current-resumer) (cons 'error e))))
                     (let ((result (thunk (cdr first-arg))))
                       (set-box! status 'done)
                       (send! (current-resumer) (cons 'return result)))))))))
         (f (%make-fibre fibre-actor status)))
    f))

;; Called from WITHIN the fibre's own thunk, arbitrarily deep in its
;; call stack -- this is the whole point, vs. a generator's yield.
(define (fibre-suspend value)
  (send! (current-resumer) (cons 'suspend value))
  (let ((next (receive)))                 ; blocks until the next fibre-resume!
    (parameterize ((current-resumer (car next))) (cdr next))))

;; Called from OUTSIDE, to start or continue a fibre. Returns one of:
;;   (yield . v)  -- the fibre suspended with value v; call again to continue
;;   (return . v) -- the fibre's thunk returned v; the fibre is done
;;   (error . e)  -- the fibre's thunk raised condition e
(define (fibre-resume! f value)
  (unless (memq (unbox (fibre-status-box f)) '(suspended))
    (error "fibre-resume!: fibre is not suspended" f))
  (set-box! (fibre-status-box f) 'running)
  (send! (fibre-actor f) (cons (self) value))
  (let ((msg (receive)))
    (case (car msg)
      ((suspend) (set-box! (fibre-status-box f) 'suspended) (cons 'yield (cdr msg)))
      ((return)  (cons 'return (cdr msg)))
      ((error)   (cons 'error (cdr msg))))))
```

(Illustrative, not final — `box`/`unbox`/`set-box!` stand in for whatever curry's own mutable-cell idiom turns out to be; `self` is the existing actor primitive. The `parameterize` around `current-resumer` gives each fibre thread its own dynamically-scoped view without a manual save/restore, reusing R7RS `make-parameter`'s existing per-thread semantics rather than inventing a new one.)

### 4.2 Public API

| Procedure | Behaviour |
|---|---|
| `(make-fibre thunk)` | Creates a fibre wrapping `thunk`. Does not start it. |
| `(fibre-start! f . args)` | Starts `f`, calling `(apply thunk args)`. Equivalent to the PHP `Fiber::start()`; kept as a separate name from `fibre-resume!` since the *first* resume is the one that supplies the thunk's actual arguments, not a value fed to a paused `fibre-suspend`. |
| `(fibre-resume! f value)` | Resumes a suspended `f`, delivering `value` as `fibre-suspend`'s own return value inside the fibre. Returns `(yield . v)` / `(return . v)` / `(error . e)` (see §4.1). |
| `(fibre-suspend value)` | Called from inside a running fibre's own call stack (at any depth). Pauses the fibre, handing `value` to whoever's `fibre-resume!` call is waiting, and returns whatever the *next* `fibre-resume!` supplies. |
| `(fibre? x)` | Predicate. |
| `(fibre-status f)` | One of `'suspended` `'running` `'done` `'error`. |
| `(current-fibre)` | The fibre the calling code is running inside, or `#f` at the top level — lets library code (e.g. a generator-style `for-each` helper) assert it's being called from fibre context. |

### 4.3 Semantics worth being explicit about

- **`dynamic-wind` across a suspend.** Because a fibre is a real OS thread parked on `receive`, a `dynamic-wind` whose body calls `fibre-suspend` behaves exactly like any other blocking call inside a `dynamic-wind` already does in curry today — the *after* thunk does not run on suspend (the thread is merely blocked, not unwound), and correctly runs on either a normal return or an escaping condition once the fibre's thunk actually finishes. This is a direct, welcome consequence of building on real threads rather than something this design has to construct — a stack-switching implementation (Candidate A) would have to solve this problem on purpose; here it's already solved.
- **What happens if a fibre is dropped mid-suspension.** A suspended fibre is a parked thread waiting on `receive` forever, if nobody ever calls `fibre-resume!` again. This is a real resource leak (an idle pthread + its stack) — no different in kind from an actor nobody ever sends the final message to, and not something this design introduces new risk for, but worth its own line in the eventual module doc's Notes section: fibres that are abandoned mid-suspend are not automatically reclaimed.
- **Errors.** A condition raised inside a fibre's thunk (uncaught by anything inside the fibre) is caught at the fibre boundary and handed back through `fibre-resume!`'s own `(error . e)` return, rather than crashing the fibre's underlying thread the way an unhandled actor error currently does (`actor_thread`'s own `fprintf(stderr, "Actor %lu died: ...")` path, `src/actors.c:134`) — a caller resuming a fibre should get the condition back as data, matching PHP's own "the exception propagates out of `resume()`" behaviour, not a silent thread death logged to stderr.
- **Nesting.** A fibre resuming another fibre is fully supported with no extra work — `current-resumer` is just whatever actor called `fibre-resume!`, fibre or not, and each fibre's own `receive`/`send!` pair is independent of every other's.

### 4.4 What v1 deliberately doesn't cover

- **Cross-fibre cancellation** (PHP has no direct equivalent either — a fibre can only unwind itself). Killing a fibre outright would need `actor-kill`-equivalent machinery this system doesn't currently expose cleanly; left for a later pass if real usage asks for it.
- **A generator/iterator sugar layer** (`(for-each-yielded proc fibre)`-style helpers, a `define-generator` macro, etc.) — valuable, but it's a thin library on top of §4.2's four core procedures, not part of the primitive itself; a natural first follow-up once the base is landed.
- **Any lightweight-stack optimization** (Candidate A, §3) — explicitly deferred, not rejected.

## 5. Summary Recommendation

| Question | Decision |
|---|---|
| Build fibres at all? | Yes — fills a real gap (stackful suspend/resume) nothing else in curry covers; doesn't block on or duplicate full continuations. |
| Underlying mechanism | An OS thread per fibre, parked on a rendezvous (Candidate B) — not stack-switching (Candidate A), not deferred to full `call/cc` (Candidate C). |
| New C code required | None. `(curry fibre)` is pure Scheme over the existing actor primitives. |
| Module name | `(curry fibre)` (singular — names the primitive type, matching `make-mutex`/`make-condvar`'s own naming shape in `(curry sync)`), pending no objection. |
| Biggest known cost | One real OS thread + its stack per live fibre — fine for tens/hundreds, a real ceiling in the low thousands at curry's current 8 MiB default actor stack size. |
| Revisit stack-switching (Candidate A) when | Real workloads actually want thousands of concurrent fibres, or per-switch latency (two thread parks/wakes) turns out to matter for something on the hot path. |

**One thing to get right before implementation starts:** the error-propagation behaviour in §4.3 (a condition raised inside a fibre surfaces through `fibre-resume!` as data, not a logged thread death) is the one place this design deliberately diverges from how curry's actors currently handle an uncaught error — worth confirming that's the intended contract (it matches PHP's own Fiber semantics, and is very likely what anyone reaching for a "fibre" instead of a bare "actor" actually wants) before the first line of the module is written.
