# Changelog

### Unreleased

**New — `(srfi srfi-N)` shims (SRFI-261)**

[SRFI-261](https://srfi.schemers.org/srfi-261/) (finalized 2025-12-07)
specifies `(srfi srfi-N)` as the primary portable library-name form for
referring to SRFI N, distinct from both the bare `(srfi N)` form curry
already shipped and curry's own descriptive `(srfi sN name)` naming.
Added a `(srfi srfi-N)` shim for all 24 existing numbered SRFIs
(`lib/curry/modules/srfi/srfi-N.scm`), identical in shape to the existing
`(srfi N)` shims. See `docs/reference/srfi/index.md`.

### 1.14.1 - 2026-08-01

**Core — `number->string` printed `#<number>` for rationals/complex/quaternion/etc.**

`num_to_string()` (`src/numeric.c`) only special-cased fixnum/bignum/flonum/
mpfr and fell back to the literal string `"#<number>"` for everything
else, including plain rationals — `display`/`write` were unaffected since
they go through the separate, already-correct `scm_write()` path. Fixed
by delegating the fallback to that same writer via a string port, with a
dedicated `vis_rational` branch kept separate to avoid a real
infinite-recursion crash this initially introduced (see below).

**Core — cuneiform notation now covers the whole numeric tower**

`(number->string _ 'cuneiform)`/`(string->number _ 'cuneiform)` previously
only handled integers, and even there the writer's fractional `·`
separator wasn't understood by the reader (`(string->number (number->string
3/2 'cuneiform) 'cuneiform)` returned `#f`). Both are fixed, and the
notation is extended to complex numbers (new 𒄿 imaginary-unit marker),
quaternion/octonion/Clifford-multivector (new 𒂊 basis-blade marker,
`𒂊<digit>` per index, matching `mv-write`'s ASCII `e13` convention), and
surreal/symbolic expressions (writer-only, reusing existing Akkadian
operator aliases for `+`/`-`/`*`/`/`/`sin`/etc. via `lang_pr_lookup`). A
general multivector's metric signature isn't recoverable from the
notation and is reconstructed as Euclidean on read-back (documented
limitation).

An 8-angle code review plus a dedicated security-review pass on this work
found and fixed two real crashes — a stack-overflow via infinite
recursion between `num_to_string`/`scm_write`/`write_number_notation`/
`sex_to_cuneiform` for a rational overflowing the base-60 digit cap under
`current-number-notation`, and a UTF-8-corruption segfault in
`src/reader.c`'s cuneiform-token trailing-glyph loop (it truncated a
multi-byte codepoint to a single raw byte) — plus three fail-open
validation gaps in the new extended-notation parser (a term-count cap too
small for large multivectors, a duplicate-blade term silently overwriting
instead of being rejected, and an unrejected double-signed magnitude).

**New module — `(curry babylonian-astronomy)`**

Pure-Scheme Babylonian mathematical-astronomy toolkit: the System-A
"zigzag" ramp-and-reflect function, the System B mean synodic-month
constant (`29;31,50,8,20` days, entered as an actual Neugebauer literal)
with a Saros-period (223 synodic months) eclipse-window predictor, and
the twelve Babylonian civil calendar month names — not a full MUL.APIN
star-almanac implementation. Three procedures get Akkadian aliases
(transliterated + cuneiform) via `(curry private lang-aliases)`. See
`docs/reference/module-babylonian-astronomy.md` and
`examples/mul-apin-akkadian.scm`.

### 1.14.0 - 2026-07-31

**Core — intermittent crash/hang in actor + STM code (`tests/actors_tests.scm`)**

`make test` was failing ~25-40% of the time in the STM section (4 actors
hammering one shared `tvar` via `atomically`), either segfaulting or
hanging on a semaphore that never got posted because the actor died first.
Root cause: `stm_atomically`'s retry loop (`src/stm.c`) longjmps to its own
`retry_jmp` whenever `stm_retry()` fires (very frequent under real
contention — every conflicting read triggers one), completely bypassing
the VM-state save/restore that `SCM_PROTECT` provides for the exception
path. Every retry left `vm->sp`/`vm->frame_count` wherever the retrying
`tvar-read` happened deep inside `vm_run()`, and the next retry's
`apply_arr` call captured that already-drifted `vm->sp` as its new
baseline — silently leaking VM stack/frame slots on every single retry
until the VM's stack was corrupted enough that a later `OP_CALL` read a
stale slot as the callee, invoking an unrelated live closure with a bogus
argc (surfacing as "too many arguments" on some unrelated procedure) or
segfaulting outright. Fixed by saving/restoring the same state
(shadow stack, GC-inhibit count, JIT call depth, VM frame/stack pointers)
around `stm_atomically`'s and `stm_or_else`'s retry longjmp points that
`SCM_PROTECT` already does for exceptions. Confirmed via gdb and an
LD_PRELOAD `SIGSEGV` trap; 0 failures across 500+ stress runs after the
fix, versus a consistent ~25-40% failure rate before.

While chasing the above, also found and fixed a real (if narrower) latent
race in `GLOBAL_ENV`: `frame_grow`/`frame_hash_rehash` (`src/env.c`)
reallocate the frame's `syms`/`vals`/`hidx` arrays with zero
synchronization against concurrent readers — actors are the only thing
that ever shares one `EnvFrame` across threads (every other frame is
owned by exactly the C call stack that created it), so a `(define ...)`
racing another actor's global lookup could read a hash bucket computed
against one generation of the table applied to a different one, indexing
out of bounds. Wasn't the trigger for the crash above (that repro never
grows `GLOBAL_ENV` while the actors run), but is real and independently
fixed: `EnvFrame.version` now doubles as a seqlock (odd = writer in
progress) with a mutex serializing writers; lock-free readers retry on a
version mismatch. Local (non-global, single-thread-owned) frames take an
unchanged, zero-overhead path.

**Core — `qt6.so` failed to load; MPFR/interval values displayed as `#<object N>`**

`vm_exn_state_save`/`vm_exn_state_restore` (`src/eval.h`) lacked `extern
"C"` linkage, so `qt6.cpp` (compiled as C++) name-mangled the
declarations while `vm.c` (compiled as C) exported them unmangled —
`qt6.so` failed to `dlopen` with an undefined symbol error at runtime.
Fixed by wrapping the whole header in one outer `extern "C"` block rather
than patching declarations one at a time, so a future C++ module can't
reintroduce the same bug by including `eval.h` unwrapped. Separately,
`scm_write`/`scm_display` (`src/port.c`) had no case for MPFR or Interval
values, so displaying one fell through to the generic `#<object N>`
placeholder instead of its numeric value.

### 1.13.0 — 2026-07-30

**Breaking: `(surfage sN name)` renamed to `(srfi sN name)`**

Every existing SRFI compatibility library (`s1`, `s19`, `s27`, `s64`, `s69`, `s90`, `s112`, `s170`, `s174`, `s215`, `s238`) moved from `lib/curry/modules/surfage/` to `lib/curry/modules/srfi/`, and every `(surfage sN name)` import must become `(srfi sN name)` — there is no compatibility shim for the old name. `surfage` shipped in every release from 1.1.1 through 1.12.0, so this breaks existing code that imports it; there was no reasonable way to keep both names live indefinitely without permanently carrying two identical copies of every library. All future SRFI ports go under `srfi`.

**14 new SRFI compatibility libraries, plus bare-number `(srfi N)` shims for all of them**

`(srfi s8 receive)`, `(srfi s145 assume)`, `(srfi s227 optional-arguments)` (spelled `#:optional`/`#:rest`, not `#!optional`/`#!rest` — curry's reader treats `#!` as a shebang-style line comment anywhere in the source, not just at file start, so the SRFI's own syntax can't be read at all here), `(srfi s128 comparators)`, `(srfi s125 hash-tables)` / `(srfi s126 hashtables)` (comparator-keyed and R6RS-style hash tables, both layered on curry's native eq?/eqv?/equal? table), `(srfi s132 sorting)` / `(srfi s133 vectors)`, `(srfi s113 sets-and-bags)` (sets wrap curry's native set; bags are a pure-Scheme multiset on a comparator-adapted hash table), `(srfi s158 generators-and-accumulators)`, `(srfi s18 multithreading)`, `(srfi s98 os-environment-variables)`, `(srfi s59 vicinity)`, `(srfi s194 random-data-samples)`. SRFI-112 was already fully covered by the existing `(curry posix)`-backed library; no new work needed there.

`(srfi s158)`'s `make-coroutine-generator` and `(srfi s18)`'s threads are the one place genuinely new capability was needed rather than a wrapper: turning an arbitrary producer procedure into a generator needs real suspend/resume, which curry's escape-only `setjmp`/`longjmp` continuations can't provide, so both are built on a real OS thread (curry's actor `spawn`) handed off through a `(curry sync)` mutex/condvar rendezvous.

Independent review found 5 real bugs across this batch, all fixed: `let-optionals*`/`opt-lambda` (s227) had no matching clause for a dotted tail-var after the option specs (`(x 0) (y 0) . rest`) — a plain syntax-rules match failure; a coroutine producer or thread thunk that raised (s158/s18) skipped the "mark done and wake the other side" step, deadlocking every later generator call or `thread-join!` forever; `set-fold` (s113) re-exported curry's native `set-fold` as-is, but the native primitive calls `(proc acc elem)` while SRFI-113/SRFI-1 `fold` convention is `(proc elem acc)` — a silently-wrong reduction order, not a crash; and `hashtable-hash-function` (s126) returned a hash procedure even for eq?/eqv? hashtables, where R6RS specifies `#f`.

Every implemented SRFI also gets a bare-number `(srfi N)` shim — `(import (srfi 1))` alongside `(import (srfi s1 lists))` — matching the naming convention SRFI 97 itself specifies (and what Chibi-Scheme/Gauche actually implement), rather than only curry's own `sN name` convention. Combining two numbered imports that export the same generic name (`(srfi 69)`+`(srfi 125)` both touch `make-hash-table`, for instance) has the usual colliding-import ambiguity any Scheme has; `(srfi 125)`+`(srfi 126)` avoid this with each other and are safe to combine.

**Core — crash on a non-symbol R7RS library-name component, e.g. `(import (srfi 1))`**

Adding the `(srfi N)` shims above surfaced a real segfault: `name_to_path()` (`src/modules.c`, turns a library name into a module-search filesystem path) called `sym_cstr()`/`as_sym()` unconditionally on every name component, assuming a symbol object. R7RS library names may also contain exact non-negative integers (that's exactly what `(srfi 1)` is), and a bare fixnum component crashed immediately. Fixed by rendering a fixnum component as its decimal digits; independent review then found the fix was incomplete for the same bug class — any *other* non-symbol component (a bignum, a string, ...) still hit the same unchecked cast — so anything that isn't a symbol or a fixnum now raises a clear Scheme error instead of crashing.

**5 GitHub CodeQL "security-and-quality" alerts fixed**

Two (`cpp/wrong-type-format-argument`) and one (`cpp/wrong-number-format-arguments`) turned out to share a real root cause once actually investigated: several error messages for curry's own `%`-prefixed internal primitive names (`%record-ref`, `%define-algebra!`, and — found by sweeping for the same pattern, though not itself flagged — 8 occurrences of `%ffi-*` in `src/ffi.c`) were passed straight into `scm_raise`/`scm_raise_code` as printf-style format strings, with the literal `%` parsed as a bogus conversion specifier. Confirmed by adding `__attribute__((format(printf, 2, 3)))` to both functions' declarations (previously undetected, since they're custom vararg functions rather than a compiler-recognized `printf` family member) and rebuilding: real `-Wformat` warnings pointed at the exact broken strings. Fixed by escaping to `%%` at every site; zero format warnings anywhere in the codebase after a full clean rebuild.

A fourth (`cpp/toctou-race-condition`) was genuine: `src/scc.c`'s `src_hash()` (keys the `.scc` bytecode cache) did `stat()` then a separate `fopen()` on the source path, leaving a window where the file could be swapped between the check and the read. Switched to `open()` then `fstat()` on the resulting descriptor — the exact same file is checked and read, no window — with `O_NONBLOCK` on open (cleared via `fcntl` once `S_ISREG` is confirmed) to preserve the original code's behavior of never blocking if the path names an empty FIFO.

The fifth (`cpp/cleartext-transmission`) was real and the most consequential: `(curry neo4j)` had no encryption option at all — `neo4j-connect` sent Bolt HELLO/LOGON credentials over a plain TCP socket. Added `neo4j-connect-tls`, modeled closely on the existing `(curry redis)` TLS pattern, which wraps the socket in a TLS session before a single Bolt byte (including credentials) goes out, auto-detected at configure time via OpenSSL the same way redis's `HAVE_REDIS_TLS` is.

**Security fix to already-shipped code: `redis-connect-tls` didn't verify the server hostname**

Building the neo4j TLS support above surfaced a real gap in the redis TLS code it was modeled on, present in every release that has shipped `redis-connect-tls`: `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL)` alone confirms the peer's certificate chains to a trusted CA, but never checks that the certificate was actually issued for the host being connected to — a MITM holding *any* valid certificate for *any* domain would still pass. An automated post-commit review of the new neo4j code caught the same gap there before it shipped at all. Fixed in both modules with `SSL_set1_host(ssl, host)` before `SSL_connect`, so the handshake itself now fails on a hostname mismatch.

### 1.12.0 — 2026-07-28

**`(curry aviation-weather)` — METAR / TAF / ATIS aviation weather report parsing**

New pure-Scheme module using `(curry regex)` (POSIX extended regex, already always built in) — no new C dependency. Tokenizes a report on whitespace, then classifies each token independently against a small anchored regex per field (wind, visibility, RVR, weather phenomena, cloud layers, temp/dewpoint, altimeter), shared across all three formats; only the message envelope (station, time, report-type keywords) differs per format. Produces plain immutable records with `->alist` converters, not a baked-in serialization method — hand the alist to `(curry json)`/`(curry yaml)` for a string. Both North American (statute-mile visibility, inHg altimeter) and ICAO international (metre visibility, hPa/QNH altimeter) conventions are recognized in the same report if mixed.

Independent review found two more real bugs in the same class as two found during development (a `P`/`M`-prefixed visibility qualifier, and a TAF group-boundary guard, both already fixed by the time review started): `CAVOK` ("ceiling and visibility OK") and `NSW` ("no significant weather") — both very common in real international traffic, `CAVOK` especially — weren't recognized by any classifier, so hitting one silently dropped every field after it (temperature/dewpoint/altimeter for METAR/ATIS; every subsequent forecast group for TAF). Fixed by recognizing both as boolean flags (`metar-cavok?`/`taf-group-cavok?`/`atis-cavok?` and the `nsw?` equivalents) rather than fabricating a visibility/cloud/weather value the report never actually gave.

**Core — `(curry json)`'s `json-stringify` silently converted bignums and exact rationals to `null`**

Any value `json_write_value` didn't explicitly recognize — including bignums and exact rationals, both ordinary numeric-tower values — fell through to the generic "unrecognized value" `null` case, indistinguishable from a genuinely absent field. Found while building the module above: an altimeter field (`2993/100`, an exact rational) silently became `null` in JSON output. Fixed by adding two new public embedding API functions — `curry_is_number` (true for any numeric-tower value) and `curry_number_to_double` (converts via the existing internal `num_to_double`, itself unchanged, just newly exposed) — and using them in `json_write_value` as a fallback after the existing exact fixnum/`%g` float paths, which still take priority. `curry_number_to_double` returns `NAN` rather than raising on a non-number, matching every other accessor in `include/curry.h` (`curry_fixnum`, `curry_car`, etc., all of which assume the caller already checked the type) — an earlier version of this fix raised via the normal exception path, which independent review flagged as inconsistent with the rest of the header and, worse, would `abort()` the whole process if called from embedder C with no exception handler installed, rather than just corrupting Scheme-level state.

**Compiler — `compile_guard` silently truncated multi-expression `guard` clause bodies to their first expression**

`(guard (e (#t (display "a") (display "b") (display "c"))) ...)` in *compiled* code (the common case — anything not forced through the tree-walker) ran only `(display "a")`; `(display "b")`/`(display "c")` were never even compiled in, not merely skipped at runtime. Found via independent review while auditing an unrelated fix. Also fixed a body-less `(test)` clause returning `V_VOID` instead of the test's own value (now uses `cond`'s `=>` arrow form internally). Independent review stress-tested 20k iterations, nested guards, and non-matching fallthrough/re-raise with no issues found.

**`(curry posix)` — SRFI-170 filesystem/process bindings, plus SRFI-112 environment inquiry**

New C module (pure libc, no extra dependency, `-DBUILD_MODULE_POSIX=ON` by default): `file-info`/`stat`/`lstat` with type predicates, directory create/list/remove/open/read/close, symlinks/hardlinks/rename/realpath, file mode/owner/times/truncate, process state (cwd/umask/pid/nice/uid/gid), user/group database lookups, wall-clock/monotonic time, environment-variable mutation, `terminal?`, and (SRFI-112) `implementation-name`/`-version`/`os-name`/`-version`/`cpu-architecture`/`machine-name` via `uname(2)`/`gethostname(2)`. Portable re-exports as `(surfage s170 posix)` and `(surfage s112 environment-inquiry)`.

Independent review caught two real bugs before release: a directory-stream double-close handed a stale `DIR*` back to libc (confirmed reproducible segfault from pure Scheme, no C needed) — fixed by zeroing the packed pointer on close and checking for it; and nearly every numeric argument (mode, uid, gid, length, time values) went through `curry_fixnum()` unchecked, which silently returns garbage for a non-fixnum (e.g. a bignum) instead of erroring — confirmed a bignum mode argument silently created a directory with garbage permission bits. Fixed with a `req_fixnum()` helper that raises a clean Scheme error instead. Also hardened `read-symlink` to grow-and-retry rather than trust a fixed buffer size.

**`(curry yaml)` — YAML reader and writer, pure Scheme**

Hand-written recursive-descent parser and writer: block/flow mappings and sequences, all scalar styles (plain, quoted, literal `|`/folded `>` block scalars), comments, `---` document markers and multi-document streams, implicit scalar typing, the core explicit tags (`!!str` etc.), anchors/aliases, and merge keys (including multi-source `<<: [*a, *b]` with first-source-wins dedup). Unlike `(curry json)`, `null` is a distinguished sentinel (`yaml-null`/`yaml-null?`) rather than collapsed into `#f` — matching SRFI-180's approach for JSON, and the right call here since config files often rely on null-vs-false being distinguishable.

Independent review found five real bugs, all fixed: the writer misidentified any plain list of mappings as one giant mapping (crash or silent corruption on a sequence-of-mappings — exactly what `yaml-parse` itself produces for `"- name: a\n- name: b"`); a block scalar (`|`/`>`) as a sequence item used the wrong indent baseline, confirmed against a realistic Kubernetes manifest to fragment 2 documents into 7 (several garbage) when a `command: - |` shell-script block was present; multi-source merge keys didn't flatten; a sequence item with a nested mapping/sequence value got jammed onto the same output line, dropping content on reparse; and root-level multi-line plain scalars were truncated and fabricated bogus extra documents.

**Core — `val_hash()` didn't structurally hash pairs, vectors, bignums, and other compound types**

`equal?`/`eqv?`-mode hash tables and sets (`(curry sets)`, any hash table using the default `equal?` comparator) fell back to pointer-address hashing for every type except fixnums/flonums/strings/symbols, while `scm_equal()` correctly implements structural equality for all of them — breaking the fundamental invariant that equal objects must hash equal. A list or vector key almost never landed in the same bucket as an `equal?` key inserted via a different allocation, so lookups silently missed (confirmed: 2/200 hits for list keys, 0/200 for vector keys, before the fix). Found via independent review of `(surfage s69 hash-tables)`, which defaults to `equal?`-mode tables. Fixed with proper recursive structural hashing for pairs/vectors/bytevectors/tuples/f64vectors (`SET_CMP_EQUAL`) and bignums/rationals/complex/quaternions (`SET_CMP_EQUAL` and `SET_CMP_EQV`, since `scm_equal` delegates to `scm_eqv` for these regardless of comparator mode). Symbolic (CAS) expression trees remain a known, documented gap (not currently exercised by any use of `val_hash` in the codebase).

**`(surfage s69 hash-tables)` / `(surfage s90 hash-tables)` — SRFI-69 basic hash tables + SRFI-90 `make-table`**

Full SRFI-69 API, pure Scheme, wrapping curry's built-in hash table. Reimplements `hash-table-ref` with correct SRFI-69 thunk/error-on-miss semantics (curry's own builtin treats the 3rd argument as a plain default value instead — available here as `hash-table-ref/default`), and restricts `make-hash-table`'s equivalence predicate to `eq?`/`eqv?`/`equal?` (curry's underlying table only supports those three comparator modes), raising a clear error for anything else. `(surfage s90 hash-tables)` adds `make-table`, a Gambit-style keyword-argument constructor (markers need quoting — curry has no self-evaluating colon-keyword reader syntax).

Independent review found `(surfage s90 hash-tables)` imported both `(surfage s69 hash-tables)` and `(scheme base)`, both exporting `make-hash-table` — curry's import merge doesn't error on the collision, and which one won depended on unrelated import-order details, silently bypassing s69's comparator validation under the wrong order. Fixed by excluding `make-hash-table` from the `(scheme base)` import. (The `val_hash` core bug above was also found during this review.)

**`(surfage s174 posix-timespecs)` — SRFI-174 POSIX timespecs**

Small immutable `(seconds nanoseconds)` time-instant record type, pure Scheme, no dependency on `(curry posix)` or anything else.

**`(surfage s19 time)` — SRFI-19 time/date objects and calendar conversions**

Time objects, date objects, Julian Day / Modified Julian Day conversion via the standard Fliegel & Van Flandern algorithm, arithmetic with nanosecond carry/borrow, and `date->string`/`string->date` with `strftime`-style format directives. `time-tai` raises rather than faking leap-second math (no maintained leap-second table); `time-monotonic` is numerically identical to `time-utc`; `current-date` has no local-timezone auto-detection (no `localtime()` binding exists yet).

Independent review — warranted here more than most, since calendar math is a notorious bug magnet — found three real bugs, all fixed and verified against Python's `datetime`/`calendar` as ground truth: `~U`/`~W` week-number directives were silently off by one for ~98% of the year (missing the standard partial-first-week adjustment); `~V` claimed to be the ISO 8601 week number but was just an alias for `~W`'s plain count, missing the actual "week containing the first Thursday" rule and its year-boundary spillover; and `string->date`'s `~z` directive couldn't parse the `+` sign that `date->string`'s `~z` always emits for a zero/positive offset, so the module's own default (UTC, which prints as `"+0000"`) crashed on round-trip.

**`(curry codesets)` — SRFI-238 codeset lookup (errno/signal/http-status)**

Unified numeric-code ⟷ symbol ⟷ human-message lookup: `codeset?`, `codeset-symbols`, `codeset-symbol`, `codeset-number`, `codeset-message`. `errno`/`signal` symbol names and numeric values come straight from this platform's own `<errno.h>`/`<signal.h>` macros; `http-status` is a static RFC-sourced table. Plus `(surfage s238 codesets)`. Independent review found no bugs — verified the platform-aliasing guards (`EOPNOTSUPP`/`ENOTSUP`, `EWOULDBLOCK`/`EAGAIN`) against the actual macOS headers rather than by inspection, and spot-checked the full HTTP status table against the relevant RFCs.

### 1.11.1 — Fix VM stack corruption across guard/longjmp; add SRFI-64 and SRFI-215 surfage libraries

**Exception handling — `guard`/`with-exception-handler`/`handler-bind` could corrupt the VM's operand stack**

`ExnHandler` (the setjmp/longjmp-based frame installed by the `SCM_PROTECT` macro and by `guard`'s hand-rolled equivalent in `eval.c`) saved and restored `g_jit_call_depth`, the GC shadow stack, and the GC inhibit counter across a catch, but not the VM's operand stack pointer, frame count, or open-upvalues list. `apply()`/`apply_arr()`'s bytecode-closure call path (`src/runtime.c`) only restores `vm->sp` on a *normal* return from `vm_run()`; a longjmp past that point — which is exactly what happens when a tree-walker-evaluated closure (anything defined via `load`, or inside a `define-library` body, both of which are always tree-walked, never compiled) uses `guard` to catch an exception raised from a nested call into VM-compiled bytecode — skips that restore entirely. The corrupted `vm->sp` then made the *next* VM opcode read garbage stack slots as if they were arguments, surfacing as nonsensical "too few arguments" or type errors in unrelated code running after the catch.

Found while implementing `(surfage s64 testing)` below: `test-error`/`test-assert` need exactly this pattern (catching an exception from an arbitrary, usually VM-compiled, test expression from inside a tree-walked library function), so the bug made the module unusable for its core purpose until fixed. Fixed by adding `saved_vm_frame_count`/`saved_vm_sp`/`saved_vm_open_upvalues` to `ExnHandler` and two new functions, `vm_exn_state_save`/`vm_exn_state_restore` (declared with opaque `void *` parameters in `eval.h` so translation units without `vm.h` can still use `SCM_PROTECT`; defined in `vm.c`), that mirror the VM's own already-correct `VmHandlerInfo` save/restore used by the native `OP_PUSH_HANDLER`/`OP_POP_HANDLER` opcodes. `SCM_PROTECT` and `guard`'s hand-rolled setjmp block (which was *also* separately missing the GC shadow-stack/inhibit-counter save/restore that `SCM_PROTECT` already had) both call these now. All 43 ctest suites pass; independent review per this project's mandate is in progress as of this writing.

**`(surfage s64 testing)` — SRFI-64 test-suite API**

New pure-Scheme library following the existing `(surfage sN name)` convention (see `s1 lists`, `s27 random-bits`): `test-begin`/`test-end`/`test-group`/`test-group-with-cleanup`, `test-assert`/`test-equal`/`test-eqv`/`test-eq`/`test-approximate`/`test-error`, `test-skip`/`test-expect-fail` with `test-match-name`/`-nth`/`-any`/`-all` specifiers, full test-runner objects (`test-runner-null`/`-simple`/`-create`/`-current`/`-factory`, all `test-runner-on-*` callbacks, `test-result-ref`/`-set!`/`-remove`/`-clear`/`-alist`), `test-apply`, `test-with-runner`, `test-read-eval-string`. The default ("simple") runner's final summary prints `N passed, M failed[, ...]` and exits nonzero on failure, matching the `(if (> fail 0) (exit 1) (exit 0))` convention every file in `tests/*.scm` already hand-rolls — so migrating an existing test file to it is a drop-in replacement for ctest purposes, not a new convention. Motivated by 37 of the 53 files in `tests/*.scm` each independently defining their own copy-pasted pass/fail-counting `check` helper.

While implementing this, discovered that curry's `syntax-rules` macros are not hygienic for free identifiers when a macro defined inside a `define-library` body is used from outside that library: a macro template resolves helper-procedure names in the *use-site* environment, not the definition-site environment. Worked around by exporting the handful of otherwise-private helpers (`%run-assert`, `%run-compare`, `%run-approx`, `%run-error`, `%run-error-2`, `%run-group`) that public macro templates expand to directly — documented inline as internal, not part of the SRFI-64 API. This is a real limitation of curry's macro system worth fixing properly at some point; not attempted here.

**`(surfage s215 log)` — SRFI-215 central log exchange**

New pure-Scheme library: `send-log`, severity constants (`EMERGENCY` through `DEBUG`), `current-log-fields` and `current-log-callback` parameters. The default callback buffers up to 100 messages and replays them once the application installs its own callback, so log calls made before logging is configured aren't lost. Fills a real gap — curry had no logging facility at all before this.

### 1.11.0 — Add `(curry oop)`: Slim CLOS Layer 1 — classes, generics, multiple dispatch

New pure-Scheme library, `lib/curry/modules/curry/oop.scm`, no C changes: Layer 1 of the object-system design in `docs/thoughts/oop.md`.

- **`define-class`** with single or multiple inheritance and per-slot `#:init`/`#:accessor`/`#:mutable`/`#:type` options. Slots are **immutable by default** — the one deliberate departure from standard CLOS, chosen because an object whose state can be accidentally mutated is a worse fit for curry's physics/CAS domain than one that can't be, and immutable objects share freely across actors with no synchronization.
- **`define-generic`/`define-method`** with real multiple dispatch: method applicability and specificity ordering both go through a proper C3 linearization of the class hierarchy (the same algorithm Python, Dylan, and Raku use for multiple inheritance), not just a first-argument check. `call-next-method` walks the linearized method chain.
- **`make`/`slot-ref`/`slot-set!`**, plus introspection: `is-a?`, `class-of`, `subclass?`, `class-name`, `class-slots`, `class-precedence-list`.
- A **built-in type hierarchy** so generic functions dispatch on plain numbers, strings, symbols, etc. without any wrapper — adapted pragmatically to what's actually distinguishable via predicates that exist in this codebase today (there's no Scheme-visible `fixnum?`/`bignum?`/`flonum?` split, only at the C `val_t` tag level, so those collapse into `<integer>`/`<inexact-real>` here rather than literally reproducing the design doc's more granular aspirational tree).
- **Extending an existing procedure** (e.g. the primitive `+`) with methods for a user-defined type, while its original behavior is preserved as the fallback for every case no user method matches — verified specifically that plain `(+ 1 2 3)` keeps working after `+` has been extended for a user-defined `<poly>` type.

Verified against all six of the design doc's own suggested acceptance tests, plus a 31-assertion test file (`tests/oop_tests.scm`). Independent review (per this project's mandate for non-trivial Scheme code) found four real correctness bugs before this was called done, all fixed and covered by regression tests:

- Redefining a method appended a duplicate instead of replacing it, so re-evaluating a `define-method` — the ordinary REPL/script-reload workflow — left the *old* definition as the one dispatch actually picked, with the new one reachable (if at all) only via `call-next-method`. Fixed by comparing specializer lists via `eq?` identity (not `equal?` — classes are singletons, and structural comparison of two *different* class records could spuriously call them "equal") and replacing on a match.
- `#:init` expressions were evaluated once at class-definition time and shared `eq?` across every instance of the class — a mutable default like `#:init (make-hash-table)` would be the *same* table for every instance. Fixed by wrapping `#:init` in a thunk, re-evaluated fresh on every `make` call, matching how CLOS re-evaluates `:initform` per instance.
- The internal unbound-slot sentinel was directly importable (the module has no export list) and `slot-set!`-able, which would make `slot-ref` wrongly report an explicitly-set slot as still unbound. Fixed with an explicit rejection in `slot-set!`.
- `define-method`/`define-generic` on a name already bound to a non-procedure value (e.g. a plain number) silently clobbered it with a fresh generic function instead of raising a clear error.

Layers 2 (a VM-level polymorphic inline cache for hot dispatch paths) and 3 (wiring the numeric tower's existing C-level type dispatch through this same generic-function machinery) remain future work, not attempted here. `ctest` 45/45 (new `oop` test), `test_cli.sh` 67/67.

### 1.10.4 — Full-codebase security/correctness audit: memory safety, concurrency, numeric-tower bugs

A systematic bug-hunting audit (9 parallel independent reviews, each required to build and reproduce findings rather than just pattern-match) turned up 23 findings. This release fixes all of them, working through the list in priority order across many iterations, with every non-trivial fix reviewed independently at least once (several caught a flawed first attempt and required a second iteration — noted inline below where that happened) per this project's own review policy.

**`(curry mcp)` JWT auth — check-after-write stack buffer overflow in base64url decoding**

`b64url_decode()` (`modules/mcp/mcp_auth.c`, used by JWT bearer-token authentication for the MCP server's stdio/SSE transports) called `EVP_DecodeBlock()`, which writes exactly `(input_len)/4*3` bytes into the caller's destination buffer regardless of that buffer's real size, and only checked the result against the caller's capacity *after* the write had already happened. Every caller passes a small fixed-size stack buffer (e.g. a 256-byte signature buffer), and the decoded segment's length is fully attacker-controlled (it's a raw substring of the incoming, not-yet-authenticated bearer token) — a long enough token overflows the stack buffer before the size check ever runs. Confirmed with a standalone AddressSanitizer harness reproducing the old function's exact logic: a 4000-character token against a 256-byte destination SEGVs under the old code, and is cleanly rejected with the destination untouched under the fix.

Found in the same full-codebase audit as the other fixes in this file (flagged there as "not currently reachable given how it's called, but fragile — one buffer-size change away from exploitable"). Independent review confirmed that characterization precisely: the one live call path into this function (`mcp_auth_validate` ← the incoming `Authorization` header) already truncates to 255 bytes upstream in `mcp.c` before ever reaching `b64url_decode`, well under the 256-byte buffer's decode capacity — so this is not currently remotely exploitable through the live server, but is a real bug in a general-purpose auth-path helper that becomes exploitable the moment that upstream truncation length changes, moves, or a new call site is added without knowing to preserve the same invariant.

Fixed by computing the exact output size `EVP_DecodeBlock` will use and rejecting up front, before it's ever called, rather than after. Also tightened the scratch `malloc` to its exact required size (was a slightly-loose-but-always-sufficient `slen + 4`), added a missing `malloc` failure check, and a defensive underflow guard on the final length computation. Applied the same missing-`malloc`-check fix to a sibling code path in the same function (`jwt_validate`'s header/payload copies) that the review noted for consistency. ctest 44/44 (`mcp` 22/22, unaffected — the existing suite doesn't exercise JWT auth mode specifically, a coverage gap acknowledged rather than closed here), test_cli.sh 67/67.

**STM — missing post-read version re-check could let a transaction body observe a torn value**

`stm_tvar_read()` (`src/stm.c`, TL2-algorithm software transactional memory backing `atomically`/`tvar-read`/`tvar-write!`) checked a tvar's version before reading its value, but never re-checked it afterward — the standard TL2 read protocol is version-read, value-read, version-*re-read-and-compare*, and the third step was missing. `tx_commit()` applies a committing write by setting a tvar's value, then its version, while holding that tvar's lock — but readers never take the lock, so a commit landing in the exact window between a reader's version-load and value-load was invisible to it: the reader would see the pre-commit version paired with the post-commit value. Read-set validation at the reading transaction's own eventual commit would still catch that the tvar changed and force a retry — but only after the transaction body had already executed on the torn snapshot, which is exactly what per-read validation exists to prevent. Fixed by re-reading the version immediately after the value and retrying right away on any mismatch, closing the window (confirmed: version numbers for a given tvar are strictly increasing — serialized by that tvar's own lock, sourced from a monotonic global counter — so a mismatch can only mean an intervening commit, never a same-value coincidence short of 2⁶⁴ commits).

Found in the same full-codebase audit as the other fixes in this file. Reviewed independently, which confirmed the fix's correctness by close reading but also — honestly reported and preserved in the added test's comment rather than glossed over — found that the accompanying stress test (a shared `tvar` incremented by several actors, checking the final total) doesn't actually distinguish pre-fix from post-fix behavior: a transaction that's a single read-modify-write can only ever have a torn read discard itself via commit-time validation, never corrupt the final total. A test that truly isolates this specific window would need either a transaction body whose control flow diverges on torn data before ever reaching commit validation, or direct instrumentation of `stm_tvar_read` — both judged out of proportion for this fix. Kept as a concurrent-load correctness smoke test regardless. `actors` ctest 14/14 (new case added), full suite 44/44, test_cli.sh 67/67.

**Concurrency — work-stealing pool started worker threads before all deques were initialized**

`pool_init()` (`src/workpool.c`, the persistent Chase-Lev work-stealing thread pool backing parallel `map`/`reduce`/`for-each/par`) initialized each deque and started its worker thread in the same loop iteration — but every worker's steal loop scans *all* deques looking for work, not just its own, so a thread already running could race a steal attempt against `deque_init()` for a not-yet-reached index. TSan-confirmed real per C11's non-atomic `atomic_init` semantics, though not currently exploitable: the deque array is zeroed on allocation before any `deque_init` runs, and `deque_steal` checks `top >= bottom` before ever dereferencing the (`NULL` until initialized) backing buffer, so a steal reaching an uninitialized deque sees "empty" rather than corrupting anything — fragile, since that safety net depends on an implementation detail of the allocator rather than a documented contract, but not presently reachable as a wild read. Fixed by splitting into two sequential loops: initialize every deque first, only then start any thread — `pthread_create`'s happens-before guarantee (a universal pthreads/C11 requirement, the same one every program relies on to safely pass a thread's argument struct) means every worker thread sees every deque fully initialized by transitivity. Reviewed independently; confirmed no other order-dependent assumption in `pool_init`/`worker_loop` needed the same fix. TSan-clean across repeated parallel-`map` runs on a 200k-element list; ctest 44/44, test_cli.sh 67/67.

**Vectors/bytevectors/misc — the same missing-bounds-check bug class found in strings, chased down across three review rounds**

The independent reviews of the string bounds-checking fix (above) kept turning up the identical unguarded-index-cast pattern in sibling primitives, each round finding the next one. Rather than stop at "strings are fixed," followed it to ground:

- **Vectors/bytevectors** (live SIGSEGVs reproduced): `vector-copy`, `vector->list` (an information-disclosure read, not a crash — returned garbage heap contents as list elements instead of erroring), `vector-fill!`, `vector-copy!`, `bytevector-copy`, `bytevector-copy!`, `read-bytevector!`, `write-bytevector` all cast a possibly-negative or possibly-huge fixnum index straight to `uint32_t` with no check. Fixed via a new shared `validate_index_range()` helper (the non-UTF-8 sibling of the string fix's `string_range_to_bytes`) plus, for the two `-copy!` primitives' destination index, a parallel direct check — same technique throughout: compare the *widened* index against the length rather than the length narrowed to match a cast index, so an oversized fixnum can't wrap past the check.
- **Constructors** (live SIGSEGVs reproduced): `make-string`, `make-bytevector`, and `read-bytevector` had no size validation at all — `(make-string -1)` and `(make-bytevector -1)` both segfaulted; `make-vector` already had the right guard, these were missed. Fixed with the same negative/`> UINT32_MAX` rejection.
- **`integer->char`** (not memory-unsafe on its own, but produced an invalid Unicode value that could corrupt UTF-8 output wherever later encoded): now rejects negative, beyond-`0x10FFFF`, and surrogate-range (`0xD800`-`0xDFFF`) code points instead of silently accepting them.
- **`octonion-ref`** (live SIGSEGV reproduced, plus a bignum-index information leak): had no type check on its argument and no bounds check on its index, directly indexing a fixed 8-element array. Now type- and bounds-checked like the rest of the numeric-ref family.

One item flagged at low confidence and deliberately left alone: `src/vm.c`'s `OP_FIXTOCHAR` bytecode opcode has the same unguarded cast `prim_int_to_char` was just fixed for, but no compiler emission site for it was found anywhere in the tree — appears to be a dead/reserved opcode, not currently reachable from Scheme source.

Reviewed independently three times in sequence per CLAUDE.md's callout on array-bounds/off-by-one specifically — each round is what surfaced the next primitive family still exposed, until a final pass came back with only the one low-confidence dead-code item. Added regression coverage to `tests/r7rs_tests.scm` and `tests/numeric_ext_tests.scm` for every primitive fixed across all rounds. `scheme_r7rs` ctest 235/235 (was 208 before this whole investigation started), `numeric_ext` 363/363, full suite 44/44, test_cli.sh 67/67.

**Strings — missing bounds checks; one live SIGSEGV, one silent corruption bug**

`string-ref`/`string-set!` (`src/builtins.c`) had no index bounds checking at all, found in the same full-codebase audit as the other fixes in this file. `string-ref` could read past the end of a string's byte buffer (a leftover dead loop plus a second loop that could land its pointer exactly at the buffer end before unconditionally dereferencing it to decode a codepoint there); `string-set!` cast a negative index straight to `uint32_t` with no check first, wrapping it to a huge offset that `utf8_char_offset` — a helper that clamps its own internal walk safely, but doesn't tell its caller anything went out of range — then silently clamped to the buffer end, splicing the new character in at the wrong place instead of raising.

An independent review of the initial fix (scoped to just those two primitives) found the identical unguarded pattern in six sibling primitives, and reproduced a live crash: `(string-copy "abc" 1000 2)` segfaulted (an out-of-range start clamped to the buffer end while a smaller explicit end didn't, underflowing a length computation to ~4GB and driving a massive OOB `memcpy`), and `(string-copy! s -1 f)` produced silent data corruption (a negative index wrapped past a `uint32_t`-only overflow check). Fixed `string-copy`, `string->list`, `string-fill!`, `write-string`, `string->utf8`, `substring`, and `string-copy!` the same way, via a new shared `string_range_to_bytes()` helper that validates a `[start,end)` character range against the string's real character count — comparing the end index *widened* against the count rather than the count *narrowed* to match the index, specifically so an index larger than `UINT32_MAX` can't wrap to something small and slip past the check. `utf8->string` (byte-indexed, not character-indexed, so it doesn't go through the shared helper) and `string-copy!`'s destination index got the equivalent direct fix.

A second independent review of the widened fix confirmed it correct end to end, but flagged that the identical bug class is still live, untouched, in the vector and bytevector primitives (`vector-copy`, `vector->list`, `vector-copy!`, `vector-fill!`, `bytevector-copy`, and likely `bytevector-copy!`/`read-bytevector!`/`write-bytevector`) — reproduced further live SIGSEGVs and an information-disclosure read there. That family is fixed separately, see below.

Added regression coverage to `tests/r7rs_tests.scm` for every primitive touched (negative/out-of-range/multibyte cases, plus confirming the original string is left uncorrupted after a rejected `string-set!`). Reviewed independently twice per CLAUDE.md's callout on array-bounds/off-by-one specifically — round two is what caught the sibling-primitive gap round one left open. ctest 220/220 (`scheme_r7rs`, was 208), full suite 44/44, test_cli.sh 67/67.

**Surreal numbers — non-canonical construction and silent multiplication truncation**

Two bugs in `src/surreal.c` from the same full-codebase audit, both violating the canonical-form invariant (sorted descending by exponent, no duplicate exponents, no zero coefficients) the rest of the arithmetic — `sur_add`'s merge in particular — assumes every live `Surreal` object already has:

- `make-surreal` (and every other constructor) funneled through `from_arrays()`, which only filtered zero coefficients — it never sorted or merged duplicates. `(make-surreal (list (cons 0 1) (cons 2 3) (cons 0 4) (cons 1 5)))`, built directly from user-controlled Scheme data, produced a non-canonical object that would silently corrupt any later `sur_add` involving it. Fixed by having `from_arrays` always sort + merge before allocating, so every `Surreal` that exists is canonical by construction, not by caller discipline.
- `sur_mul` allocated a fixed `MAX_TERMS*MAX_TERMS` (4096)-sized scratch buffer and silently stopped writing cross-terms past that cap — multiplying two 70-term surreals (4900 cross-terms) returned a mathematically wrong, silently truncated product with no error. An initial fix removing the cap entirely was caught by independent review as introducing two new problems: a 32-bit `int` overflow in the `na*nb` size computation that decoupled the allocation size from the actual write count (a heap-buffer-overflow risk for large operands), and an O(n⁴) hang risk from unleashing the file's O(n²) insertion sort on an uncapped result with nothing else bounding term counts. Landed fix instead computes the product size widened to 64 bits and raises a clear error if it would exceed the cap, rather than either silently truncating (the original bug) or growing unbounded (the review-caught regression).

One narrower, out-of-scope gap independent review also flagged: `num_cmp` has no real total order for quaternion/octonion values (documented in its own code as "no total order — only equality is meaningful"), so a surreal built with quaternion-typed exponents could have duplicate exponents survive `sort_terms`'s insertion sort non-adjacent and go unmerged. This is a pre-existing `num_cmp` limitation affecting anything that sorts by number generally, not something this fix introduced; not addressed here.

Added regression coverage to `tests/numeric_ext_tests.scm`: canonicalization of out-of-order/duplicate input, zero-coefficient dropping after merge, a correct in-cap multiplication, and confirmation that an over-cap multiplication now raises instead of silently truncating. Reviewed independently twice (the term-cap removal was itself caught and revised by the second review) per CLAUDE.md's callout on numeric code specifically. `numeric_ext` ctest 359/359 (was 355), full suite 44/44, test_cli.sh 67/67.

**Sexagesimal — undefined behavior converting large-magnitude flonums**

`sex_get_digits()` (`src/numeric.c`, backs `number->string`'s `'neugebauer`/`'cuneiform` notation and the `(curry sexagesimal)` module) cast a flonum's integer part — still a `double` at that point — directly to `long` before handing it to GMP. A flonum whose magnitude exceeds `LONG_MAX`/`LONG_MIN` (e.g. `1e19`) makes that cast undefined behavior per the C standard. Found in the same full-codebase audit as the other fixes in this file. Fixed by using GMP's `mpz_init_set_d` directly on the double instead, which is documented to accept any finite magnitude with no intermediate fixed-width-integer cast. NaN/Inf are already rejected earlier in the function and the value is already forced non-negative before this point, so there's no NaN/Inf/`-0.0` edge case reaching the fix, and no behavior change for values that were already in-range. Reviewed independently per CLAUDE.md. Added regression cases to `tests/sexagesimal_tests.scm` spanning just past `LONG_MAX`, right at its boundary, and the pre-existing (unrelated, unaffected) 64-digit overflow fallback for extreme values like `1e300`. ctest 82/82 (sexagesimal), 44/44 (full suite).

**Security/correctness — public C module API silently truncated strings at embedded NUL bytes**

Curry strings are length-prefixed, not NUL-terminated at the language level — `(string #\a (integer->char 0) #\b)` is a legitimate 3-byte string. The public embedding API (`include/curry.h`, used by every loadable module) only exposed a strlen-based `curry_make_string(const char*)` and a bare-pointer `curry_string()` with no length accessor, so any module round-tripping binary or attacker/user-controlled data silently dropped everything after the first embedded NUL instead of erroring — a correctness bug in the ordinary case, and a data-integrity/security bug for any module treating the truncation point as significant. Found in the same full-codebase audit as the other fixes in this file. Added `curry_string_length()` and `curry_make_string_n(const char*, uint32_t len)` to the public API; `curry_make_string()` is now defined in terms of the latter. Fixed every call site across the tree that read or wrote data of a known byte length through the old strlen-based functions instead:

- **`(curry redis)`** — every RESP command argument built from a Scheme value (write side) and the bulk-string reply parser (read side) now carry the real length end to end. Verified against a live `redis-server`: a `redis-set!`/`redis-get` round trip of a string with an embedded NUL now round-trips exactly instead of truncating.
- **`(curry sqlite)`** — `TEXT` column reads now use `sqlite3_column_bytes` instead of strlen, and `sqlite-bind` now passes the real string length to `sqlite3_bind_text` instead of `-1` (strlen). Verified with an in-memory DB round trip.
- **`(curry ldap)`** — attribute values were already read via the length-aware `ldap_get_values_len`/`berval` (specifically because LDAP values, e.g. binary ones like `jpegPhoto`, aren't NUL-terminated), but the `bv_len` was then discarded by a strlen-based string construction. Now uses the real length.
- **`(curry mqtt)`** — a received message payload's tracked length (`paylen`, already captured at enqueue time) was discarded by a strlen-based string construction; MQTT payloads are explicitly allowed to be arbitrary binary. Now uses the real length.
- **`(curry http)`** — an HTTP response body's tracked length was discarded the same way; response bodies are frequently binary content types. Now uses the real length.

Reviewed independently per CLAUDE.md, given the change touches public API surface shared by every module. Added a regression test to `tests/redis_tests.scm` (live-server round trip) and a new `tests/sqlite_tests.scm` (registered in `tests/CMakeLists.txt`); ctest 44/44, test_cli.sh 67/67.

**Concurrency — `spawn` could silently hand an actor a wrong/stale captured value**

A correctness bug from the same full-codebase audit as the security fixes below, now fixed: a closure passed to `spawn` could still have an upvalue open into the *spawning* thread's live stack at the moment the new actor thread started running it. If the spawning thread was a tail-recursive loop reusing that exact stack slot for its next iteration (which TCO does by design), the actor could read the wrong value — no crash, no error, just silently wrong data. Confirmed on a plain release build with no sanitizer: 2000 actors spawned from a loop capturing the loop variable produced several actors reading a duplicated/skipped value across repeated runs.

Fixed in two iterations, the first of which review caught as itself wrong: closing the shared `Upvalue` object in place (the first attempt) froze the value for *every* closure sharing that same variable from the same still-live scope, not just the one being spawned — breaking ordinary same-thread code (e.g. a sibling closure over the same loop variable) that should still observe a later `set!`. The landed fix instead builds the actor a private, independent snapshot closure (`vm_snapshot_closure_for_escape`), leaving the original closure and anything else sharing its upvalues completely untouched. TSan-confirmed clean on both the original race and the sibling-closure regression; regression tests added to `tests/actors_tests.scm`.

One related, pre-existing, deliberately out-of-scope gap noted during review: a closure with open upvalues sent to another actor via a mailbox message *before* being spawned would have its upvalues force-read by the *receiving* thread, not the thread that opened them — this fix's synchronous-snapshot-in-the-spawning-thread approach assumes `spawn`'s caller is that owning thread, which mailbox-relayed closures violate. Not currently known to be reachable in practice; would need broader design work on what value kinds a mailbox should accept.

**Security — six memory-safety bugs from a full-codebase audit**

A systematic bug-hunting audit (9 parallel independent reviews, each required to build and reproduce findings rather than just pattern-match) turned up 23 findings ranging from confirmed crashes to minor UB. This release fixes the six most severe: memory-unsafe bugs reachable from ordinary Scheme code with no special build flags, several confirmed with ASan/a live crafted-input reproduction. The rest of the audit's findings (a real correctness bug in `spawn`+closures, GC/STM/workpool issues in experimental backends, `surreal.c`/sexagesimal/`string-ref` correctness bugs, and more) remain open — see the audit for the full list.

- **`bytevector-u8-ref`/`bytevector-u8-set!`** (`src/builtins.c`) had no index bounds check at all — `(bytevector-u8-ref (make-bytevector 4 0) 1000000)` segfaulted instantly. Now bounds-checked like `vector-ref`/`vector-set!`, raising `index-out-of-range`.
- **`%record-ref`/`%record-set!`/`%record-ctor`** (`src/builtins.c`) — the primitives `define-record-type`'s generated accessors compile down to — had no type check on the record/RTD argument and no field-index bounds check; both `(%record-ref 5 0)` and `(%record-ctor 5 1 2 3)` segfaulted. Now type- and bounds-checked. (Field immutability is still not enforced at this layer — see the code comment for why that's a separate, deliberately out-of-scope gap.)
- **`(curry vecdb)`** `vecdb-add`/`vecdb-search` never validated that a vector's length matched the database's declared dimensionality before reading exactly that many floats from it — an ASan-confirmed heap-buffer-overflow read. Both now raise a clear error on a dimension mismatch.
- **`(curry image)`**'s GIF LZW decoder used a fixed 256-byte-per-row dictionary buffer, but a chained dictionary entry's length is unbounded up to the table size (4096) — an ordinary large flat-color GIF (not just a crafted one) could overflow it. ASan-confirmed on a 4000×4000 solid-color GIF. Rows widened to the safe bound, plus an explicit clamp as defense in depth.
- **`(curry neo4j)`**'s PackStream decoder allocated (and immediately `memset`-committed) a bytevector/list sized from an attacker-controlled length header *before* validating it against the actual remaining message buffer — unlike the adjacent string-decode path, which already validated first. Crashed against a fake malicious Bolt server during the **unauthenticated** HELLO handshake; fixed to validate first, matching the existing string path.
- **`(curry mcp)`**'s JSON parser had no recursion-depth limit — unbounded stack recursion on deeply nested input, crashable via `mcp-serve`'s stdio transport (and pre-authentication over HTTP on some SSE-enabled deployments). The sibling `(curry lsp)` module was specifically hardened against exactly this; `mcp.c` now has the same `JSON_MAX_DEPTH` guard.

**Two audit items deliberately left open**

- **The interactive debugger's `next`/`finish` behavior around tail calls** (`src/debug.c`/`src/vm.c`) was investigated and a fix was built (tagging each `CallFrame` with a monotonic call-sequence number, bumped on every `OP_TAIL_CALL` frame reuse as well as every `OP_CALL` push, so `next` could distinguish "still the same activation" from "a tail call just replaced it"). Testing it against the existing named-let-loop debugger test, though, showed it broke correct, already-tested behavior: `next` stepping through a self-recursive loop (`count` → `lp` → `lp` → …) relies on exactly the same tail-call-frame-reuse mechanism as an ordinary cross-function tail call, and no rule was found that cleanly separates "should stop" (loop iteration) from "should skip" (a plain tail call elsewhere) without more clarity on the intended semantics than the audit finding provided. Reverted rather than ship a regression in working, tested behavior. Left as an open, documented question rather than a fix.
- **The generational GC backend's (`--gc generational`, experimental, non-default) write barrier not tracking actor-thread nurseries** was not attempted this release — lower priority since it's an opt-in backend, not the one any user gets without explicitly asking for it.

### 1.10.3 — Fix `.scc` cache never being written for GUI/`(exit)`-ending scripts

**`.scc` cache — GUI/`(exit)`-ending scripts now actually get cached**
- Same root cause as the `--timings` fix below, for the transparent
  `.scc` cache itself: the positional-script-file cache-miss loop only
  called `scc_write()` once, AFTER its read/compile/run loop returned —
  but a form whose `vm_run()` never returns to that loop (a `(curry qt6)`
  GUI script's `run-event-loop`, or any script ending in a plain `(exit)`)
  meant that line was never reached. Every GUI script, and every script
  ending in `(exit)`, was a full cache MISS on **every single run**,
  forever — exactly the class of program (slow GUI/script startup) the
  transparent cache exists to speed up.
- Fixed with the same `atexit()` pattern as the `--timings` fix: the loop
  now arms a pending-write fallback with everything compiled so far
  (including the in-flight chunk) right before that chunk's `vm_run()`
  call, so it's correct even if that call never returns. Explicitly
  disarmed after the loop's own normal-completion write (no double-write)
  and in the error handler for this block (so a genuine compile/runtime
  error never leaves behind a partial cache that a later
  unchanged-content run would wrongly treat as a complete, valid HIT).
- Regression coverage added to `tests/test_cli.sh`: a script ending in
  `(exit)` gets a working `.scc` written and hits on the next run, and a
  script that errors mid-execution leaves no `.scc` behind at all.

### 1.10.2 — Fix `--timings` never reporting for `(curry qt6)` apps

**CLI**
- `run-event-loop` (`modules/qt6/qt6.cpp`) calls C's `exit(3)` directly
  once the Qt event loop returns, deep inside `vm_run()` while a script is
  still executing — bypassing `main()`'s normal return path entirely.
  `--timings` on a GUI script (e.g. `examples/mandelbrot/mandelbrot.scm`)
  silently never printed its report, regardless of how the window was
  closed, since the report was only ever triggered explicitly at the end
  of `main()` and in the REPL's `,quit` handler.
- Fixed by registering `curry_timings_report` via `atexit()` once, early
  in `main()`, instead of calling it explicitly at each known exit point.
  `exit(3)` (unlike `_exit(2)`) always runs `atexit` handlers, so this
  catches every exit path uniformly — including ones inside C extension
  modules `main.c` has no visibility into — without needing to track down
  each one individually. Verified against `mandelbrot.scm`: closing the
  window now correctly prints the read/expand/compile/execute report.

### 1.10.1 — Transparent `.scc` cache: content-hash keyed, with HIT/MISS visibility

**`.scc` cache — content-hash keyed, with HIT/MISS visibility**
- The `.scc` bytecode cache (transparent — no `-c` needed, just
  `curry script.scm`) now validates against a content hash (FNV-1a 64 over
  the full source) instead of mtime + size. mtime survives things that
  don't change content (`git checkout`, `cp -p`, some editors'
  save-in-place), which could either falsely invalidate a good cache or,
  worse, look "unchanged" after a real edit — a content hash can't be
  fooled either way. On-disk format bumped to v4; an old-format `.scc` is
  cleanly treated as a miss and recompiled, not misread.
- HIT/MISS is now visible: `--timings` prints an extra `cache: HIT`/`MISS`
  line whenever a script run actually made a cache decision (not shown for
  `-e`/REPL, which don't use this cache at all) — addresses the exact
  failure mode Kaappi's postmortem flagged ("an invisible bytecode cache
  cost them real debugging hours").
- A real regression was found and fixed before landing: hashing requires
  reading the whole file, which drains a non-seekable, one-shot source
  like bash process substitution (`curry <(...)`, used by
  `tests/test_mcp.sh`) before the real compile pass gets to read it. Fixed
  by refusing to hash (and therefore never caching) anything that isn't a
  regular file, checked via `stat()` up front — which doesn't touch
  content — rather than via a doomed `fopen`+`fread`.
- Regression coverage added to `tests/test_cli.sh`: content-hash-not-mtime,
  changed-content invalidation, HIT/MISS line presence/absence, the
  process-substitution non-regression, and a hand-crafted stale/wrong-
  version `.scc` rejection case.

### 1.10.0 — Eval-elimination phase 3 complete, `--timings`, benchmark CI

**CLI — `--timings` pipeline report**
- New `--timings` flag prints a read/expand/compile/execute breakdown (ms)
  to stderr on exit, covering the REPL, `-e`, `-c`, and positional
  script-file (`.scc` cache-hit, cache-miss, and direct-run) paths.
  "expand" (macro-transformer application, timed at its `apply()` call
  site inside `compile()`'s dispatch) is a subset of "compile" time, not
  additional to it, so the report subtracts it out before printing —
  the four lines sum to the real total instead of double-counting nested
  work. Zero overhead when disabled (single branch per call site, same
  discipline as the existing closure profiler). See `tests/test_cli.sh`.
- A review finding was fixed before landing: the `-c` (compile-only) path
  was missing read/compile instrumentation entirely — only "expand" was
  wired up there, so `--timings -c` on a file using a macro silently
  showed `read`/`compile`/`execute` all at `0.000` while `expand` reported
  real nonzero time, misleadingly implying no compiler work happened.

**CI — benchmark suite with per-commit trends and a PR regression gate**
- `tests/bench_ci.scm` (previously written but never wired up) now runs in
  a new `.github/workflows/benchmark.yml` job on every push to `main` and
  every PR, via `benchmark-action/github-action-benchmark`. Pushes to
  `main` publish a new baseline point to the `gh-pages` branch; PRs compare
  against the stored baseline and fail (with a PR comment) if any
  benchmark regresses past 130% of baseline.

**Compiler — eval-elimination phase 3 complete**
- `with-assumptions` now compiles to native bytecode instead of punting to
  `(tree-eval '<form>)`. `(with-assumptions ((var assumption...) ...)
  body...)` desugars to a `let`/`dynamic-wind` nest mirroring the
  pre-existing `parameterize` codegen (same capture-old/set-new/restore
  shape), backed by three new primitives — `%assumption-flags`,
  `%assumption-set!`, `%assumption-restore!` — that read/OR-in/overwrite a
  `SymVar`'s assumption bits directly. Each clause's assumption keywords
  are resolved to a flag bitmask at compile time, so there's no runtime
  keyword-lookup cost.
  - One deliberate, documented behavioral improvement over the tree-walker
    found during review: if the same `SymVar` appears in two clauses of one
    `with-assumptions` form, the tree-walker's interleaved snapshot-then-set
    left a residual flag set after the form exited; the native codegen
    snapshots all clauses' original flags upfront, so a repeated variable
    is restored to its true original state. Locked in by a regression test.
- `define-rule`, `define-ruleset`, and `define-algebra` also now compile
  natively, completing eval-elimination phase 3 (`import`, `define-library`,
  and `library` remain permanently tree-walked by design — those bodies are
  load-time-only and never compiled). This closes two real, demonstrated
  bugs, not just a performance gap:
  - `define-algebra`'s auto-bound operator procedure always leaked into the
    global environment even when used inside a function body (e.g.
    `(define (f) (define-algebra 'myop ...) ...)` left `myop` callable at
    top level after calling `f` once), because `tree-eval` always evaluates
    against `GLOBAL_ENV`. When the operator name is a compile-time literal
    — `(define-algebra 'sym ...)`, the form used everywhere in practice —
    it now gets a real lexical binding, correctly local when used inside a
    function. A genuinely runtime-computed operator name has no way to get
    a real lexical binding in a slot-based compiled VM (the same
    fundamental limit as any `(define <computed-name> ...)`), so that rare
    case intentionally stays on the pre-existing `tree-eval` path.
  - `define-rule`/`define-ruleset` guard and template expressions
    referencing an enclosing lexical variable raised spurious
    `unbound-variable` errors, for the same `GLOBAL_ENV`-always reason.
  - A third, independent latent bug was found and fixed along the way in
    `compile_lambda`'s internal-define prescan: all three forms previously
    fell through to a generic fallback that blindly treated the form's
    second element as a bound variable name — the *pattern* for
    `define-rule`, the ruleset *name* for `define-ruleset`, the *operator
    expression* for `define-algebra` — reserving a bogus, permanently-
    uninitialised local slot (confirmed: for a quoted operator like `'sym`,
    this reserved a slot literally named `quote`, so a later bare
    reference to the special form `quote` inside the same body silently
    read back void instead of raising `unbound-variable`).
  - Review also caught that the compile-time-literal detection must compare
    via `akk_translate`, not a raw symbol check, so the Akkadian spelling
    of `quote` (`kīma`) takes the same fast path as `'`/`(quote ...)`
    instead of silently falling back to the always-global `tree-eval` path.
- Regression coverage added to `tests/sx_algebra_tests.scm` (49 assertions,
  up from 26): lambda-body/tail-position usage for all four forms
  (`with-assumptions`, `define-rule`, `define-ruleset`, `define-algebra`),
  independent and duplicate-clause restore, malformed-clause skipping in
  `define-ruleset`, the `quote`-slot corruption case, the Akkadian-`quote`
  case, and confirmation that the dynamic-operator-name fallback path is
  unchanged (not regressed) for both global- and local-referencing cases.

### 1.9.1 — Fix a 1.9.0 regression: segfault calling the actor-mailbox `receive` primitive

**Compiler**
- `(receive ...)` is ambiguous: it names both the R7RS special form
  (`(receive formals producer-expr body...)`) and — pre-existing in this
  codebase — the actor-mailbox primitive `receive` (arity 0–1, optional
  timeout). 1.9.0's native `receive` codegen (see 1.9.0 below) unconditionally
  compiled every `(receive ...)` call as the special form, so
  `compile_receive`'s unchecked `vcar(args)`/`vcar(vcdr(args))` segfaulted on
  `(receive)` and `(receive timeout)` — real code, e.g.
  `examples/solar-system-qt6.scm`'s actor loops.
- The two are fully disambiguated by argument count alone (the special form
  needs at least 2 forms after the keyword; the primitive takes 0–1), so the
  compiler now only takes the special-form path when at least two argument
  forms are present, falling through to an ordinary call — resolving
  `receive` to the primitive — otherwise.
- Regression coverage added to `tests/actors_tests.scm` for both the
  zero-arg and one-arg mailbox forms.

### 1.9.0 — Eval-elimination phase 3: native codegen for receive, define-record-type, define-syntax, and symbolic

**Compiler — retiring the tree-walker punt, one special form at a time**
- `receive`, `define-record-type`, `define-syntax`/`let-syntax`/`letrec-syntax`, and
  `symbolic` now compile to native bytecode instead of desugaring to
  `(tree-eval '<form>)` at runtime. Each of these punts had a real correctness
  bug behind it, not just a performance cost:
  - `receive` used inside compiled code resolved to the unrelated
    actor-mailbox `receive` primitive instead of its own special form, since
    the tree-walker had no `S_RECEIVE` case — well-formed R7RS `receive` was
    silently wrong or raised unbound-variable errors.
  - internal `define-record-type` always leaked its
    constructor/predicate/accessor/mutator bindings into the global
    environment instead of staying lexically local, because `tree-eval`
    always evaluates against `GLOBAL_ENV`.
  - `define-syntax` inside a compiled unit could fail with "apply: not a
    procedure" (macro not yet registered when the use site compiled) or leak
    into the global environment when used internally.
  - `symbolic` had the same internal-define leak as `define-record-type`.
- Fixing `define-syntax` surfaced a chain of deeper bugs along the way:
  reentrant-VM stack corruption in compile-time macro evaluation (reachable
  from the debugger's `,debug`/`p`), a `gc_inhibit_count` leak on a bad
  transformer expression, and `.scc` cache-hit replay silently losing
  top-level macros — now fixed by reconstructing `syntax-rules` transformers
  from serializable pattern/template data at runtime instead of re-evaluating
  source.
- Also fixed a real `.scc` identity bug surfaced while native-compiling
  `symbolic`: `define-record-type`'s record type descriptor (RTD) was
  serialized independently at each of its four use sites, so a `.scc`
  cache-hit reload produced four non-`eq?` RTDs and broke `%record-pred?`.
  `scc.c` gained explicit `T_RECORD_TYPE` (de)serialization, and the compiler
  now shares one RTD binding across all four generated closures.
- `eval.c` was split into `runtime.c` (exception/condition system,
  `dynamic-wind`, the JIT call-depth guard, and the `apply`/`apply_arr`
  trampoline shared by the VM/FFI/stdlib) and a slim tree-walker retained
  only for `library`/`define-library` bodies, which are never compiled.
- New internal primitives `%make-record-type` and `%rebuild-syntax-rules` are
  ordinary discoverable globals and now validate their arguments rather than
  crashing on misuse.
- Extensive regression coverage added across `tests/r7rs_tests.scm`,
  `tests/syntax_rules_tests.scm`, `tests/numeric_ext_tests.scm`,
  `tests/test_cli.sh`, and `tests/test_debugger.sh`.

**Qt6**
- Fixed a macOS regression where mouse press/release (drag-pan, double-click
  zoom) stopped responding while scroll-wheel events kept working:
  `QScrollArea`'s native `NSScrollView` doesn't get backed by a real native
  view until the window is actually shown, so a one-time `raise()` at
  window-creation time couldn't reliably fix the canvas's native z-order.
  The canvas is now re-raised on every `showEvent`.

### 1.8.4 — Language Server Protocol module, SRFI 149 macro extensions, module export enforcement

**Language Server Protocol** — new `(curry lsp)` module
- **`(lsp-serve)`** drives a Content-Length-framed JSON-RPC stdio
  server — the transport every LSP editor client speaks (VS Code,
  Neovim, Vim, Kate, Emacs). First milestone: real-time diagnostics
  (runs the actual reader over the buffer on `didOpen`/`didChange`),
  hover (special forms, builtins, Akkadian synonyms, from a table
  generated by `tools/gen-editor-syntax.py`), and completion (the same
  table plus local bindings found by structurally walking the reader's
  parsed output — not text scanning, so a comment or string can never
  produce a phantom binding).
- Hardened against a hostile or merely broken client: bounded
  `Content-Length` with a checked allocation, bounds-checked `\u`
  escape parsing (including UTF-16 surrogate pairs for astral
  cuneiform codepoints), a JSON-RPC nesting depth cap, and a guard
  against the reader's unbounded per-paren recursion — itself careful
  to recognize `#\"` / `#\;` character literals so they can't desync
  the guard's scan into skipping real nesting uncounted.
- `tests/test_lsp.sh` (27 assertions, wired into `ctest`) drives the
  module over its real stdio transport the same way `test_mcp.sh`
  already does for `(curry mcp)`. See `docs/reference/module-lsp.md`.

**Macros — SRFI 149 template extensions**
- **`(... template)` ellipsis escape** — lets a macro emit literal
  `...` (e.g. a macro that expands into another `define-syntax`).
- **Custom ellipsis identifiers** —
  `(syntax-rules ellipsis (literal ...) rule ...)`, freeing `...`
  itself to appear as ordinary literal data.
- **Vector patterns and templates** — `#(a b)`, `#(a ...)` match and
  generate vectors the same way list patterns already do, including
  ellipsis sub-patterns.
- 13 new assertions in `tests/syntax_rules_tests.scm` (42/42 passing).
  Documented in `docs/reference/language.md` under Special forms,
  alongside a note that `syntax-rules` remains intentionally
  unhygienic — this SRFI doesn't change that.

**Modules — real export enforcement**
- `define-library`/`library` export clauses were parsed and then discarded,
  so every binding in a library environment was importable regardless of
  what it declared exported — there was effectively no module
  encapsulation. `modules_import` now filters against the declared export
  list when one exists, falling back to export-everything for C modules and
  plain `.scm` files with no `define-library`/`library` wrapper, matching
  prior behavior there.

### 1.8.3 — Editor syntax highlighting, full Akkadian coverage, three bug fixes

**Editor support**
- **Syntax highlighting for Vim/Neovim, Kate (and every
  KSyntaxHighlighting consumer: KWrite, KDevelop, Qt Creator), and VS
  Code.** Generated from the actual runtime tables
  (`src/symbol_list.h`, `src/akkadian_names.h`,
  `DEF`/`cond_def`/`curry_define_fn` registrations across `src/*.c` and
  `modules/*/*.c`) by `tools/gen-editor-syntax.py`, so it stays in sync
  as builtins are added. Covers special forms, all builtins, Akkadian
  transliterated + cuneiform synonyms, Neugebauer sexagesimal literals,
  and cuneiform numerals — matching the reader's own priority rule that
  a lone cuneiform glyph resolves as a synonym while a
  single-space-joined multi-glyph run is a number. See `editors/README.md`.

**Complete Akkadian/cuneiform coverage**
- **Every C-level procedure and special form now has a working
  Akkadian and cuneiform synonym** — all 970 names across every
  `src/*.c` core file and every `modules/*/*.c` C extension (previously
  only ~25%, concentrated in the original R7RS core). New import-time
  aliasing mechanism (`akk_pr_lookup()` called from `modules_import()`)
  makes this possible for the first time: names that only exist inside
  a lazily-loaded module's own environment (every optional C module,
  and every Scheme `(curry ...)` library) get their synonyms installed
  at the point of import instead of never, since the previous
  mechanism only ever saw names already bound at global-startup time.
- Fixed a registration-order bug that silently dropped aliases for any
  core file registered late in `builtins_register()` (numtheory,
  condition, and any future addition), and 8 pre-existing cuneiform
  collisions where two unrelated procedures shared one cuneiform
  spelling, silently shadowing one of them.
- New `tools/check-akkadian-table.py` validates the whole table after
  every change (no duplicate translit/cuneiform strings, no embedded
  whitespace, all cuneiform codepoints in-block); 1011 rows, zero
  collisions.

**Bug fixes** (found incidentally while writing coverage tests, fixed on request)
- `json-stringify` segfaulted on a plain list (it treated any Scheme
  pair as a `(key . val)` alist entry unconditionally); now correctly
  distinguishes genuine alists (string/symbol-keyed) from plain and
  nested lists, rendering the latter as JSON arrays.
- `image-format` (plus `image-load`/`image-save`) segfaulted when
  passed the wrong argument type (e.g. an image object where a path
  string was expected) via an unchecked `curry_string()` call; all
  three now raise a clean, catchable error instead.
- `(eof-object)` returned void instead of the actual EOF value, so
  `(eof-object? (eof-object))` was `#f` even by its own English name;
  the constructor was still wired to a copy-paste placeholder.

### Unreleased

**Interactive debugger**
- **gdb-style debugger for VM-compiled code.** Breakpoints by function
  name (fire at frame entry) or `file:line`; set via `,break` /
  `,unbreak` / `,breaks` in the REPL, a repeatable `-b SPEC` CLI flag
  for scripts, or the new `(breakpoint)` procedure from source. At a
  stop: `step` / `next` / `finish` / `continue`, `bt`, `locals` (by
  name, including captured upvalues), `p <expr>` (live locals by name,
  anything else evaluated globally with full VM-state protection), and
  `q` to abort. `,debug <expr>` single-steps an expression. Costs one
  predicted-not-taken branch per dispatch when idle — same pattern as
  the minor-GC safepoint. The JIT tier is bypassed while armed so
  breakpoints always fire. Tree-walker code (`load`, `tree-eval`
  passthrough) is invisible to it; main thread only. See
  `docs/reference/debugger.md`.
- **Per-statement source lines.** 1.7.0's known limitation ("a bug on
  line 5 of a 10-line function reports the `define`'s line") is fixed:
  the reader stamps each cons cell with the line its datum starts on
  and the compiler consumes it, so error backtraces and the debugger
  both resolve to the exact sub-form. Plain-`let`/`let*` frames are now
  named after their enclosing function instead of `<anonymous>`.
- **`.scc` format v3.** Compiled bytecode now persists local-variable
  scope tables and upvalue names, so scripts running from the
  transparent cache debug identically to fresh compiles. Old caches are
  invalidated automatically (format version bump).
- **Fixed a nested-`vm_eval` stack corruption.** `vm_eval` never pushed
  its closure as the callee, so `pop_frame`'s `sp = slots - 1` return
  path consumed one slot of the caller's stack. Invisible at top level
  (the `frame_count == 0` path resets `sp`), corrupting when nested
  inside a running VM — which the debugger's `p` command is the first
  caller to do.

### 1.7.0 — Machine-legible errors, real backtraces, and a VM arity/JIT correctness pass

The headline of this release is diagnostics: errors finally carry a file,
line, call stack, and a stable machine-readable code instead of a bare
prose string. That work led straight into two serious correctness bugs —
one that could corrupt data silently on every wrong-arity or variadic
call through the bytecode VM, and one that crashed the process outright
on `(/ x 0)` — both fixed here, along with a five-commit chain closing a
JIT-only counter leak that review kept turning up one more instance of.
Also new: a `(curry logic)` module with six pluggable non-classical
logics, a `(curry sets)` module (multisets + logic-parameterized set
theories), Mandelbrot v6.0 with double-double navigation, and CI that
actually builds and tests the project on every push.

**Diagnostics**
- **Source location and real call-stack backtraces.** Errors previously
  printed a bare message with no file, line, or call stack —
  `condition-backtrace` was a literal stub returning `'()`. Errors now
  report `Error: ... \n  at <proc> (file:line)` chains, and
  `(condition-backtrace e)` returns the real frame list. Covers scripts,
  the REPL, `-e`, `-c`, and compiled `.scc` files (including nested
  lambda chunks). Known limitation: line accuracy is per-top-level-form,
  not per-statement — a bug on line 5 of a 10-line function reports the
  `define`'s line, not line 5.
- **Stable machine-legible error codes.** `(error-object-code e)` /
  `(condition-code e)` return a stable symbol (`'wrong-type-argument`,
  `'unbound-variable`, `'wrong-number-of-arguments`, `'not-a-procedure`,
  `'division-by-zero`, `'index-out-of-range`, `'stack-overflow`) at the
  ~30 call sites hit most often in practice, so tooling (an LSP, the MCP
  server, an LLM client) can match on a code instead of scraping prose.
  See `docs/reference/error-codes.md` for the registry and how to extend
  it — most of curry's ~540 raise sites remain uncoded by design.
- **Fixed a `SIGFPE` crash.** While wiring up `'division-by-zero`, found
  that `(/ 1 0)`, `(quotient 5 0)`, `(remainder 5 0)`, and `(modulo 5 0)`
  didn't raise a Scheme condition at all — they crashed the whole
  process. Raw C `/`/`%` on the fixnum path and GMP's `mpq_div` on the
  exact-rational path are both undefined behavior on a zero divisor.
  All four now raise cleanly.
- **Fixed `list?`/`length` hanging forever on circular lists.** Both now
  use Floyd's tortoise-and-hare; `list?` returns `#f` and `length` raises
  on a cycle, per R7RS, instead of spinning.

**VM correctness (the scary one)**
- **The bytecode VM never checked argument count.** `OP_CALL`,
  `OP_TAIL_CALL`, and `vm_run` set up a closure's local-variable window
  from however many arguments happened to be pushed, with no check
  against the closure's declared arity. A fixed-arity closure called
  with too few arguments silently read uninitialized stack memory for
  the missing parameters; called with too many, the extras were just
  abandoned. Worse: a variadic closure's rest parameter — `(lambda (a .
  rest) ...)` — ended up holding the raw last-pushed argument instead of
  a list. `(f 1 2 3 4)` bound `rest` to `2`, not `(2 3 4)`. Neither bug
  crashed or errored; both silently produced wrong values, which is why
  the full test suite kept passing. Fixed with one shared validation
  helper wired into all three call sites; wrong arity now raises
  `wrong-number-of-arguments` and rest-arg collection is correct.
- **The same bug, and the same fix, in the LLVM JIT fast path.** Once
  hot-swapped to native code (`-DBUILD_LLVM=ON`), a closure had no arity
  check either — `(g 1)` on a two-argument `g` returned a garbage value
  (a leftover stack slot) instead of raising. Fixed the same way, at all
  three JIT call sites.
- **That fix leaked a counter across caught exceptions.** Raising from
  inside a JIT-to-JIT call is now reachable (previous bullet), but
  nothing restored the JIT call-depth counter on `longjmp`, so a caught
  exception from nested JIT calls permanently inflated it — eventually
  pinning it at the depth limit and silently disabling the JIT fast path
  for the rest of the thread's lifetime. No crash, no wrong value, just
  a severe silent performance cliff. Fixed with the same save/restore
  pattern already used for the GC inhibit counter and shadow stack,
  wired into every place curry installs an exception handler — which,
  after three successive review passes each finding another hand-rolled
  site (`with-exception-handler`, `dynamic-wind`, `call-with-port`,
  `parameterize`, `with-assumptions`, the actor thread entry point, the
  parallel work-pool, `with-mutex`), turned out to be nineteen call
  sites, not the four the first pass assumed.

**New modules**
- **`(curry logic)`** — six first-class non-classical logics (classical,
  Belnap FOUR, fuzzy, intuitionistic, probabilistic, defeasible) behind
  one shared knowledge-base API (`kb-assert!`, `kb-close!`,
  `kb-consistent?`, forward-chaining rules).
- **`(curry sets)`** — multisets (hash-backed element→count bags) and
  set theories parameterized by any `(curry logic)` logic, so swapping
  the underlying logic changes what "membership" means.
- **`(curry random)`** — continuous probability distributions.

**R7RS compliance fixes**
- `string-copy!` no longer corrupts strings when source/destination
  character ranges have different UTF-8 byte widths.
- `substring` now uses Unicode character indices, not byte offsets.
- `(command-line)` is a thunk per R7RS §6.14, not a raise.
- Bytevector ports, `write-shared`, and `string-set!` width-change
  support added.

**Infrastructure**
- **CI.** `.github/workflows/ci.yml` builds and runs the full `ctest`
  suite on every push/PR, Ubuntu + macOS × Debug + Release. Previously
  the only workflow was CodeQL scanning — nothing actually built curry
  or ran its tests before merge.
- LDAP module is now opt-in (`BUILD_MODULE_LDAP` defaults `OFF`,
  Homebrew formula gained `--with-ldap`), matching how other heavyweight
  optional modules already behaved.
- Several generational-GC + JIT interaction fixes: JIT no longer bakes
  nursery pointers as IR constants under `--gc generational`, root
  registration is now lock-protected against concurrent minor GC, and
  `JitClosure` allocation is correctly pinned rather than nursery-placed.

**Mandelbrot v6.0**
- Double-double precision navigation (drag/zoom accumulate via two-sum),
  faithful to ~10²⁸ zoom depth; auto-perturbation, Julia set mode,
  adaptive iteration; several pan/zoom sign-convention fixes.

### 1.6.3 — Fix GC crash when calling (gc) after (import (curry qt6))

Patch release fixing a `SIGSEGV` at address `0x8` in Boehm's `GC_mark_from`
that occurred whenever `(gc)` was called after importing the Qt6 module.

**Bug fix**
- **`gc_register_root` bad `GC_add_roots` for pointee header** (`src/gc.c`):
  A second `GC_add_roots` call registered the first 8 bytes of the
  pointed-to heap object as a Boehm root.  Those 8 bytes are the `Hdr
  {type, flags}` field — a small integer on little-endian ARM64 (e.g.
  `T_COMPLEX = 8`).  The check `cur & ~3u` also incorrectly passed for
  fixnums (fixnum 2 encodes as `9`, and `9 & ~3 = 8`), registering
  near-null addresses as root ranges.  When Boehm scanned those "roots" it
  read from address `0x8` and crashed.  The slot registration at line 286
  (`GC_add_roots(slot, slot+8)`) is sufficient on its own — removed the
  broken second call.
- **`JitClosure.fn` hidden from Boehm** (`src/llvm/jit.cpp`, `src/eval.c`,
  `src/vm.c`, `src/object.h`): the JIT function pointer is now stored as
  `GC_HIDE_POINTER(fn)` and revealed at all four call sites with
  `GC_REVEAL_POINTER`, preventing Boehm's conservative scanner from
  mistaking JIT code addresses for heap pointers.

---

### 1.6.2 — Generational GC + LLVM JIT correctness fix

Patch release fixing a use-after-free when running `--gc generational` with
the LLVM JIT backend (`--llvm` / `-DBUILD_LLVM=ON`).

**Bug fixes**
- **JIT statepoints inhibit minor GC** (`src/llvm/codegen.cpp`): every
  allocating call emitted as an `llvm.experimental.gc.statepoint` now
  brackets itself with `curry_gc_inhibit_minor_jit` / `curry_gc_resume_minor_jit`.
  Previously minor GC could fire mid-statepoint while live nursery pointers
  sat in JIT alloca slots invisible to the collector, producing stale
  references and potential use-after-free.
- **`ExnHandler` saves inhibit counter** (`src/eval.h`): the `SCM_PROTECT`
  macro now saves `gc_inhibit_count` at `setjmp` time and restores it on the
  `longjmp` path.  Without this, a Scheme exception propagating through a JIT
  frame would skip the resume call, permanently elevating the inhibit counter
  and preventing future minor collections.
- **C-linkage inhibit wrappers** (`src/gc.c`, `src/gc.h`):
  `gc_inhibit_minor_fn`, `gc_resume_minor_fn`, `gc_inhibit_save`, and
  `gc_inhibit_restore` are new plain-C functions exposing the
  `_Thread_local gc_inhibit_count` counter to C++ and JIT callers.
  The existing `gc_inhibit_minor()` / `gc_resume_minor()` inlines are
  guarded by `#ifndef __cplusplus` and were no-ops in C++ translation units.

---

### 1.6.1 — GC header C++ compatibility fix

Patch release fixing a build regression for users building the Qt6 module.

**Bug fixes**
- `src/gc.h`: wrap `#include <stdatomic.h>` and the `_Atomic` extern declarations
  in `#ifndef __cplusplus` so Qt6's C++ translation units no longer see conflicting
  `_Atomic` qualifiers from both `<stdatomic.h>` and `<atomic>` (20 compiler errors
  on Homebrew Qt6).
- MQTT retain test: switched to QoS 1 so the broker acknowledgement is awaited
  before the subscriber connects; previously QoS 0 fire-and-forget caused a race
  between publisher and subscriber.
- Pre-push hook: `|| true` guard prevents `set -euo pipefail` from killing the
  script when the formula SHA256 is a placeholder (grep finds no hex string);
  also extended check to catch any non-64-char value, not just the zero sentinel.

---

### 1.6.0 — GC performance instrumentation and real-time benchmarking

**GC statistics**
- New atomic counters in `src/gc.c`: `gc_stat_minor_count`, `gc_stat_major_count`,
  `gc_stat_minor_total_us`, `gc_stat_minor_max_us`.
- Pause ring buffer in `src/gc_gen.c`: last 256 minor GC pause times (µs) for
  computing p50/p95/p99 without sorting full history.
- Boehm major GC counted via `GC_set_on_collection_event` callback.
- New builtins `(gc-stats)` and `(gc-stats-reset!)` expose all counters and the
  pause ring as a Scheme alist.

**`(gc-stats)` alist**

```
((minor-count . N) (major-count . N) (minor-total-us . N) (minor-max-us . N)
 (pause-ring . #(u64 ...))   ; last 256 minor pause times in µs
 (heap-size-bytes . N) (free-bytes . N) (nursery-used . N))
```

**GC robustness (gc-perf work)**
- Lock-free `pinned_add`: atomic fast path; mutex only on resize.  Eliminates
  contention on the pinned slot list under concurrent minor GC + parallel map.
- Minor GC safepoint at VM `L_DISPATCH`: deferred minor GC fires between bytecode
  instructions rather than mid-instruction, preventing corruption of live C locals.
- Pinned slot nulling: after each scan the full `[0, pinned_count)` range is
  zeroed so Boehm can reclaim dead pinned objects between major collections.

**Real-time benchmarking stack**

`tools/bench-stack/` — one-command Docker Compose observability pipeline:
```
bench.scm → MQTT (mosquitto) → Telegraf → InfluxDB → Grafana (live dashboard)
```
Watch benchmark runs live at `http://localhost:3000` while `bench.scm` publishes
results.  Dashboard panels: mean time by benchmark, p99 over time, minor GC
count per run, max pause gauge, comparison table (boehm vs generational).

Three benchmark suites:
- `tests/bench.scm` — core throughput + GC-specific workloads (short-lived alloc,
  medium-lived promotion, large-object bypass, write-barrier mutation, mixed)
- `tests/bench_heavy.scm` — longer warm-up, actor ring, larger allocation budgets
- `tests/bench_torture.scm` — stress suite (1M alloc loop, deep recursion, etc.)

**New documentation**
- [`docs/reference/benchmarking.md`](docs/reference/benchmarking.md) — benchmark
  suite reference: build prereqs, quick start, all suites and panels, MQTT event
  schema, writing custom benchmarks
- [`docs/reference/profiling.md`](docs/reference/profiling.md) — profiler
  reference: `**eval-profiler**` levels, `(curry profiling)` API, `,profile` REPL
  command, timing workflows and limitations

---

### 1.5.0 — Generational GC

Two-generation Cheney garbage collector available as `--gc generational`.

**Architecture**
- Gen0 (nursery): shared 2 MB `mmap` region with mutex-protected bump pointer.
  All non-pinned objects are born here.
- Gen1 (tenured): 128 MB `mmap` region. Live nursery objects are promoted
  here via Cheney copy during minor collection.
- Minor collection: stop-the-world Cheney copy of the nursery into tenured
  space. Includes dirty-card scan (remembered set), tenured walk for
  `T_ENV`/`T_SET`/`T_HASHTABLE` (whose value arrays live in Boehm memory
  outside the card-table range), pinned-object scan, and ext-scanner pass.
- Major collection: full Cheney copy of both nursery and tenured space into
  a fresh tenured region, triggered when tenured exceeds 85% fill.
- Write barrier: `GC_WRITE_BARRIER(obj, field, val)` macro marks 512-byte
  cards dirty; under Boehm and semispace compiles to a single store.
  Instrumented at all mutable sites: `set-car!`, `set-cdr!`, `vector-set!`,
  `force`, `parameterize`, parameter-object calls, upvalue closing in the VM.
- Safepoints: polling (`gc_stop_world` flag). Threads yield at nursery
  exhaustion, actor receive/send!, and between work items in the parallel pool.
- Thread cooperation: `gc_gen_thread_park`/`unpark` let workers and the
  parallel-map dispatch thread opt out of STW while blocked on condvars.
- Parallel map/reduce: `par_scan_roots` ext-scanner covers `elems[]`,
  `results[]`, and per-chunk `WorkItem` val_t fields during dispatch.
- `WorkPool` and `WorkItem` pinned via Boehm so raw C pointers in the
  work-stealing deque stay valid across nursery resets.
- Symbolic CAS nodes (`SymVar`, `SymFn`, `SymExpr`) pinned; their val_t
  fields scanned during minor collection.
- Dynamic VM registry: no longer capped at 64; grows on demand, so programs
  spawning more than ~47 actors on a 16-core machine no longer silently drop
  VM roots (was a use-after-free).

**New CLI flags**
- `--gc generational` — select the two-generation backend
- `--gc-nursery-size N` — set nursery size (default `2M`; supports `K`/`M`/`G`)
- `--gc-tenured-size N` — set tenured region capacity (default `128M`)

**`(gc-stats)` generational alist**

Returns `((minor-collections . N) (major-collections . N) (nursery-bytes . N)
(nursery-used . N) (tenured-used . N) (tenured-capacity . N) (pinned-count . N))`.

**Known limitation**

The tree-walking eval/apply interpreter keeps intermediate `val_t` values as C
locals not tracked by any GC root. A minor GC firing during a deep numeric
computation corrupts those locals. Workaround: `--gc-nursery-size 16M`. A
precise or stack-mapping approach is the planned fix.

**SICM**: 167/167 pass under Boehm (default); 93/167 under `--gc generational`
with the default 2 MB nursery, 93/167 with `--gc-nursery-size 16M` (the C-stack
limitation affects the remaining 74 tests regardless of nursery size).

---

### 1.4.1 — Mandelbrot polish + Tetrabrot explorer

**New example: `examples/tetrabrot.scm`** — Bicomplex (ℂ²) Tetrabrot explorer

The bicomplex number system ℂ(i₁,i₂) extends ℂ to four real dimensions by
adding a second imaginary unit i₂ that commutes with i₁.  The Mandelbrot
iteration w → w² + c in this algebra lives in ℝ⁴ and is called the Tetrabrot.

The explorer lets you navigate any 2D cross-section of the four-dimensional
parameter space interactively:

- **Axis-pair dropdown** — six choices (x₀×x₁ … x₂×x₃); the (x₀,x₁) slice
  is the ordinary Mandelbrot set
- **Fixed-axis sliders** — sweep the two non-displayed ℝ⁴ components through
  the range −2 … 2 to move the slice through the solid
- Full pan, zoom-to-cursor, palette, iteration count, bookmarks, and PNG export
  (same feature set as the Mandelbrot explorer)
- GPU-accelerated bicomplex squaring shader — runs at full resolution even at
  high iteration counts

**Qt6 module — three new dropdown procedures**

`dropdown-add-item! dd label` — append a new item to an existing dropdown
without rebuilding it.  Used by the bookmark system to grow the list as the
user saves views.

`dropdown-clear! dd` — remove all items.

`dropdown-count dd` — return the number of items currently in the dropdown.

**Qt6 module — `canvas-save-png!` bug fixes**

Two HiDPI rendering bugs fixed in `canvas-save-png!` on Retina/HiDPI displays
(device pixel ratio ≥ 2):

- *Quarter-image bug* — the off-screen FBO was rendered into only the
  upper-right quadrant of the saved PNG.  Root cause: `gl-shader-draw!`
  computed the physical pixel size as `device->width() × dpr`, which is correct
  for a `QWidget` (logical width) but double-scales for `QOpenGLPaintDevice`
  (already physical width).  Fix: read the viewport Qt set in
  `beginNativePainting()` via `glGetIntegerv(GL_VIEWPORT)` instead.

- *Blurry HUD text* — QPainter-rendered text (coordinates, zoom level) was
  blurry in the saved PNG because the glyph cache rasterised at 1× density.
  Root cause: `setDevicePixelRatio(dpr)` moves the DPR scale into the
  projection matrix, leaving `state->matrix` as identity, so
  `pixelToDeviceTransformDensity()` returns 1 and glyphs are upscaled 2×.
  Fix: use `setWindow(0, 0, lw, lh)` / `setViewport(0, 0, pw, ph)` instead,
  which puts the DPR scale into `state->matrix` directly.  `gl-shader-draw!`
  and `gl-shader-draw-arrays!` now derive `u_dpr` from the larger of the
  device DPR and the painter world-transform scale so both the on-screen
  (`QWidget`) and off-screen (`QOpenGLPaintDevice`) paths give the same result.

**Bug fixes**

- `examples/mandelbrot.scm`: `cadddr` replaced with `list-ref` (Curry does not
  export `cadddr`)

---

### 1.4.0 — Extensible CAS

**User-extensible rewrite engine (Phase 4a)**
- `define-rule` / `define-ruleset` / `apply-rules` — pattern-matching rewrite rules with optional guards
- `list-rules`, `clear-rules` — inspect and reset rule sets

**Algebra declarations and assumptions (Phase 4b)**
- `define-algebra` — declare operator commutativity, associativity, identity, inverse
- `with-assumptions` — temporarily attach domain flags (`positive`, `real`, `integer`, `nonzero`, `quaternion`) to sym-vars
- `assume!` / `retract!` / `can-assume?` — permanent mutable assumptions

**Polynomial machinery (Phase 4c)**
- `poly-gcd`, `poly-resultant`, `poly-pseudo-remainder`, `poly-factor` (Yun squarefree + Kronecker)
- `poly-roots` (companion-matrix eigenvalues for degree ≤ 8)

**Equation solving (Phase 4d)**
- `solve` — univariate polynomial and simple transcendental equations
- `solve-system` — linear systems via Gaussian elimination

**Risch integration (Phase 4e)**
- Rational function integration (partial fractions over ℚ)
- Log-polynomial extension: integrates `f(x)·log(g(x))` forms

**Special functions (Phase 4f)**
- Orthogonal polynomials: `legendre`, `assoc-legendre`, `hermite`, `hermite-prob`, `chebyshev-t`, `chebyshev-u`, `laguerre`, `assoc-laguerre`
- `spherical-harmonic` — Y_l^m(θ,φ)
- Gamma family: `gamma` (exact integers/half-integers, symbolic), `log-gamma`, `digamma`, `beta`
- Bessel: `bessel-j`, `bessel-y`, `bessel-i`, `bessel-k` (Maclaurin series for small x)
- Elliptic integrals: `elliptic-k`, `elliptic-e`, `elliptic-f`, `elliptic-pi`

**Extended series (Phase 4g)**
- `laurent` — Laurent series around poles; pole order detected via guarded substitution
- `puiseux` — Puiseux (fractional-power) series via t-substitution

**Bug fixes**
- `sx_simplify`: exact `n/0` now raises; exact `0/0` returns unevaluated node; float `a/0.0` returns IEEE ±∞
- `sx_series`: guarded against unhandled exceptions when coefficient evaluation encounters poles
- `bessel-k`: replaced asymptotic-only seeds (up to 20% error at x=1) with Maclaurin series for x≤8

### 1.3.1 — Housekeeping

**Packaging**
- RPM CPack generator added alongside DEB; `cpack` now produces both
  `curry-scheme_*.deb` and `curry-scheme-*.rpm` from the same build.
- CMake project and CPack version now derived from `src/version.h` (single
  source of truth).

**CLI**
- `--gc BACKEND` was missing from `--help`; now listed alongside `--gc-max-heap`.

**Bug fixes**
- Mandelbrot explorer: left-drag pan and zoom-to-cursor y-axis were inverted;
  drag event name typo caused pan to not register.

**Housekeeping**
- `scripts/` directory merged into `tools/`; all references updated.
- LLVM "Enabled" banner text cleaned up.
- `docs/guides/INSTALL.md` documents both `.deb` and `.rpm` packaging with
  dependency tables for each format.

---

### 1.3.0 — Cheney Semispace GC

First moving garbage collector.  The Boehm conservative GC remains the default;
pass `--gc semispace` at startup to activate the Cheney backend.

**New GC backend: `--gc semispace`**

- Two 32 MB semispaces with bump-pointer allocation.  On exhaustion, a
  stop-the-world Cheney copy evacuates all live objects to to-space and swaps
  the spaces.
- Precise root set: VM value stack, `GLOBAL_ENV`, per-object pinned list
  (Actor/TVar/Channel/Mailbox/Continuation), module-registry external scanner.
- Type-specific scanners for all 54 `ObjType` values including raw C pointer
  fields (`Closure::env`, `BcClosure::chunk/upvals`, `EnvFrame::parent`,
  `Module::env`, `Record::rtd`, `Upvalue::next`).
- **Pinned types** (allocated in Boehm, never moved): `Symbol`, `Bignum`,
  `Rational`, `Mpfr`, `Port`, `Actor`, `Mailbox`, `TVar`, `Channel`,
  `Continuation`, `Primitive`.

**New Scheme procedures**

- **`(gc-collect!)`** — trigger an immediate collection cycle.
- **`(gc-stats)`** — return a GC statistics alist: `collections`,
  `bytes-allocated`, `bytes-survived`, `from-used`, `space-size`,
  `pinned-count` (semispace); `heap-size`, `free-bytes` (Boehm).
- **`(gc-on-collection proc)`** — register a zero-argument post-collection hook.

**C API additions**

- `gc_alloc_pinned` / `gc_alloc_raw_pinned` vtable entries and corresponding
  `CURRY_NEW_PINNED` macros — allocate typed or untyped objects in Boehm,
  bypassing the semispace nursery.
- `gc_ss_register_ext_scanner` — register an external root-scanner callback
  for C modules that hold `val_t` in non-GC-managed structs.
- `T_CHUNK` (53) and `T_UPVALUE` (54) added to `ObjType`; `Hdr` header
  prepended to `Chunk` and `Upvalue` structs.

**Documentation**

- `docs/reference/gc.md` — full GC reference: backends, Scheme API, C API,
  performance notes.

**Test results**

All 27 ctest suites pass.  Core language suites (r7rs, r6rs, numeric_ext,
actors, dynamic_wind, syntax_rules, akkadian, sexagesimal) all pass under
`--gc semispace`.  SICM ODE-integration tests: 93/167 under semispace — a
known stale-pointer residual deferred to Phase 6 (generational GC).

---

### 1.2.6 — Security patch

- **Fix stack buffer overflow in `number->string` with `'neugebauer`/`'cuneiform`**:
  `SexDigs.int_digs` and `SexDigs.frac_digs` were declared with 32 entries but
  `mpz_to_base60_digits` could write up to 64, corrupting the stack for integers
  ≥ 60³² (~2.68×10⁵⁷). Arrays expanded to 64 entries. (Reported by internal
  security review of v1.2.5.)

### 1.2.5 — Babylonian/Sexagesimal Number System

**Sexagesimal (base-60) I/O** — first-class Babylonian arithmetic, faithful to
Otto Neugebauer's 1935 transcription conventions.

- **`#s` reader prefix** — Neugebauer literal notation in source code.
  Commas separate integer sexagesimal places; semicolons mark the radix point.
  `#s1;30` → exact `3/2`, `#s1,0,0` → `3600`, `#s1;24,51,10` → `30547/21600`
  (the YBC 7289 approximation of √2, accurate to six decimal digits, c. 1800 BCE).
  The semicolon is **not** treated as a delimiter inside `#s` literals.

- **Cuneiform Unicode reader** — Babylonian cuneiform numerals are valid tokens:
  - 𒁹 (U+12079 ASH) = 1–9 repeated strokes
  - 𒌋 (U+1230B U) = tens
  - 𒑊 (U+1244A) = zero placeholder (SHAR2 tenu)
  - Adjacent glyphs within a sexagesimal group are written contiguously;
    groups are separated by spaces. `𒁹 𒌋𒁹` → `71`.

- **`(number->string n 'neugebauer)`** — format any exact or inexact number
  in Neugebauer notation. `(number->string 3600 'neugebauer)` → `"1,0,0"`.
  Optional `#:places k` keyword argument limits fractional sexagesimal digits
  for flonums: `(number->string (sqrt 2) 'neugebauer #:places 3)` → `"1;24,51,10"`.

- **`(number->string n 'cuneiform)`** — format as cuneiform glyph string.

- **`(string->number s 'neugebauer)`** / **`(string->number s 'cuneiform)`** —
  parse Neugebauer or cuneiform strings to exact Scheme numbers.

- **`(current-number-notation)`** / **`(current-number-notation sym)`** —
  global notation parameter (default `#f` = decimal). Setting to `'neugebauer`
  or `'cuneiform` makes `display` and `write` emit numbers in that notation.

- **`(curry sexagesimal)` module** — pure Scheme convenience library:
  `rational->sexagesimal`, `sexagesimal->rational`, `hms->seconds`,
  `seconds->hms`, `dms->degrees`, `degrees->dms`, `cuneiform->neugebauer`,
  `neugebauer->cuneiform`, `sex:ybc7289`.

- **`#:keyword` symbols are now self-evaluating** — fixed in both the
  tree-walking evaluator (`eval.c`) and the bytecode compiler (`compiler.c`).
  `#:foo` no longer triggers "unbound variable".

- **Full UTF-8 lookahead for file ports** — `port_peek_char` now returns a
  complete Unicode codepoint (not just the first byte) enabling multi-byte
  cuneiform glyph dispatch in the reader.

- **Test suite** — 76 assertions in `tests/sexagesimal_tests.scm` covering
  reader literals, cuneiform parsing, `number->string`, `string->number`,
  round-trips, `current-number-notation`, and the `(curry sexagesimal)` module.

### 1.2.2 — Qt6 GPU extensions; 4D cave explorer; release tooling

**Qt6 module — 13 new bindings**

- **Key-up events** (`window-on-key-up!`, `canvas-on-key!`, `canvas-on-key-up!`):
  fire on physical key release only; Qt auto-repeat fake releases are filtered.
  Enables held-key state tables for smooth WASD movement.
- **Mouse capture** (`canvas-grab-mouse!`, `canvas-release-mouse!`,
  `canvas-on-grab-move!`, `canvas-mouse-grabbed?`): cursor-lock + relative-delta
  mode for first-person camera. Cursor hidden and warped back to canvas centre
  after each event; `canvas-on-mouse!` move events suppressed while grabbed.
- **Mouse move semantics**: move events now emit `'drag` when a button is held,
  `'move` on hover — previously always `'move`.
- **Resize callback** (`canvas-on-resize!`): `(lambda (w h) …)` fires from
  `resizeEvent`; use to resize FBOs or rebuild projection matrices.
- **Timer with delta-t** (`make-timer/dt`, `timer/dt-start!`, `timer/dt-stop!`):
  callback receives elapsed ms since last tick via `QElapsedTimer` for
  frame-rate-independent physics.
- **Clipboard** (`clipboard-text`, `clipboard-set-text!`).
- **GPU vertex buffers** (`make-gl-buffer`, `gl-buffer-update!`,
  `gl-shader-draw-arrays!`): upload `f64vector` or Scheme vector to GPU; draw
  with per-vertex attribute VBOs. Primitives: `'points`, `'lines`,
  `'line-strip`, `'triangles`, `'triangle-strip`, `'triangle-fan`. 16-element
  Scheme vector maps to `mat4` uniform.
- **Framebuffer objects** (`make-gl-framebuffer`, `gl-framebuffer-texture`,
  `gl-framebuffer-resize!`, `gl-shader-to!`): offscreen render-to-texture for
  post-processing passes; `gl-shader-to!` restores Qt's default FBO after drawing.

**naturalmaze — `examples/naturalmaze/`**

Multi-file 4D cave explorer (`make run` from the directory). Each `.scm` can
be independently compiled to bytecode with `make`.

- 16×16×4 maze: DFS spanning tree + 28 % extra passages for open-world feel,
  2×2 clearing rooms, ~22 % W-passage density between dimension layers.
- GLSL 330 DDA raycaster with pitch (look up/down), procedural cave textures:
  rock fbm, moss by wall height, hanging vines, per-cell fungi spots with
  per-biome glow colour and sin-pulse animation.
- Floor: earthy dirt, scattered mushrooms (5 hash candidates per cell), puddle
  reflections, W-rift mandala runes (pulsing ring + 6-spoke pattern) on cells
  with 4th-dimension passages.
- Ceiling: stalactite noise, slowly breathing bioluminescent patches.
- Four biomes by W-slice: amber / cyan / violet / crimson fungi glow.
- Dimensional ripple overlay during W-transitions (chromatic tint toward
  destination biome proportional to visual-W fractional part).
- Mouse-grab look, smooth WASD with wall-sliding collision, Q/E step through
  the 4th dimension, scroll-wheel W-step, minimap HUD with W-rift indicators.

**Release tooling**

- `.claude/commands/release.md`: `/release <version>` slash command — 5-checkpoint
  end-to-end pipeline (preflight → bump commit → tag + SHA256 → formula → brew verify).
- `.claude/hooks/pre-push` (symlinked to `.git/hooks/pre-push`): blocks push if
  `src/version.h`, Formula URL tag, Formula `version` field, or most recent git
  tag disagree, or if the sha256 is still the zero placeholder.
- `tools/release-verify.sh`: post-release brew verification (uninstall, tap
  update, reinstall, `curry --version` assert, smoke test).

**Bug fixes**

- Formula/curry.rb `version` field was `"1.2.0"` while the URL and
  `src/version.h` both said `1.2.1`; corrected. The pre-push hook now catches
  this class of drift automatically.
- Curry VM: 3-argument `(< a b c)` inside a top-level recursive function called
  repeatedly from a named-let loop silently receives a non-numeric value after
  enough iterations (`to_mpz` error). Worked around in `naturalmaze/world.scm`
  by splitting into two 2-argument comparisons; VM bug documented for future fix.
- `(load "file.scc")` parses bytecode as Scheme source text and errors on the
  magic header; `naturalmaze/main.scm` updated to always load `.scm` source.

---

### 1.2.1 — Pollard rho bug fixes; expanded test coverage

**Bug fixes**

- **Pollard rho infinite loop** (`src/numtheory.c`): two bugs caused `factor`
  to loop forever on semiprimes whose prime factors exceed the trial-division
  limit (100 000): (1) `factor_into` modifies its `mpz_t n` argument (reduces
  to 1 when done), so the recursive call `factor_into(f, ...)` was destroying
  `f` before `mpz_divexact(n, n, f)` could use it — dividing by 1 instead of
  the actual factor; fixed by saving a copy before recursing. (2) The batch
  product accumulator `q` was never initialised to 1 — `mpz_inits` zeroes
  everything, so every batch GCD returned n; fixed by explicit `mpz_set_ui(q,
  1)` at init and on each batch boundary.

**Tests**

- `tests/numtheory_tests.scm` (111 → 120 assertions): large-semiprime factoring
  (`(* 100003 100019)`, `(* 999983 999979)`), `factor` of 2²⁰, flonum
  `continued-fraction` input, three additional `best-rational-approx` cases.
- `tests/mpfr_tests.scm` (34 → 65 assertions): standard arithmetic dispatch to
  MPFR, `inexact` promotion inside `with-precision`, `number->string` on MPFR,
  binary radix fix (`(number->string 255 2)` → `"11111111"`),
  `floor`/`ceiling`/`truncate` MPFR dispatch, `mpfr-erfc`/`mpfr-hypot`/
  `mpfr-fma`, precision propagation in binary ops.

---

### 1.2.0 — arbitrary-precision floats (MPFR) and number theory

**New features**

- **MPFR arbitrary-precision floats** (`-DBUILD_MPFR=ON`, `src/mpfr_num.c`).
  Adds `T_MPFR` to the numeric tower with `(mpfr x [prec])`, constants
  `(mpfr-pi)`, `(mpfr-e)`, `(mpfr-phi)`, `(mpfr-log2)`, `(mpfr-euler)`,
  `(mpfr-catalan)`, `(mpfr-apery)`, and MPFR-specific transcendentals
  (`mpfr-gamma`, `mpfr-zeta`, `mpfr-erf`, `mpfr-erfc`, `mpfr-j0`, `mpfr-j1`,
  `mpfr-hypot`, `mpfr-fma`, …).  Dynamic precision via `(with-precision N body
  ...)` / `(call-with-precision N thunk)` / `(current-precision)`.  Selectable
  rounding mode via `(mpfr-rounding-mode 'rndn|'rndz|'rndu|'rndd|'rnda)`.
  Standard tower transcendentals (`sin`, `cos`, `exp`, `log`, `sqrt`, …)
  dispatch to MPFR when given an MPFR operand.

- **Interval arithmetic** (with MPFR): `make-interval`, `interval`,
  `interval-lo`, `interval-hi`, `interval-midpoint`, `interval-width`,
  `interval-contains?`.  Endpoints use directed-rounded MPFR for certified
  bounds.

- **Number theory library** (`src/numtheory.c`, always built — GMP only).
  Primality and factoring (`prime?`, `next-prime`, `factor`, `prime-factors`),
  arithmetic functions (`totient`, `carmichael`, `mobius`, `divisors`,
  `divisor-count`, `divisor-sum`, `perfect?`/`abundant?`/`deficient?`, `omega`,
  `big-omega`), modular arithmetic (`mod-expt`, `mod-inverse`, `jacobi-symbol`,
  `kronecker-symbol`, `legendre-symbol`, `extended-gcd`, `chinese-remainder`),
  combinatorial sequences (`fibonacci`, `lucas`, `binomial`, `multinomial`,
  `catalan`, `bernoulli`, `euler-number`, `stirling1`, `stirling2`, `bell`,
  `partition-count`), continued fractions (`continued-fraction`, `convergents`,
  `best-rational-approx`), and number predicates (`squarefree?`,
  `perfect-power?`, `smooth?`).

**Bug fixes**

- `number->string` for radix 2 (and any radix other than 8, 10, 16) on
  fixnum inputs incorrectly emitted decimal digits using `%ld`; now routes
  through `mpz_get_str` so every radix is honoured.

See `docs/reference/numeric-precision.md`.

---

### 1.1.1 — SRFI compatibility layer (surfage)

**New features**

- **`(surfage s1 lists)`** — SRFI-1 list library: `iota`, `any`, `every`, `remove`, `delete`, `fold`, `append-map`, `filter-map`, `flat-map`, `take`, `drop`, `take-while`, `drop-while`, `count`, `partition`, `last-pair`, `first`–`fifth`, plus re-exports of all list procedures already in the global environment.
- **`(surfage s27 random-bits)`** — SRFI-27 random-number source API: re-exports Curry's built-in xoshiro256+ implementation (`default-random-source`, `make-random-source`, `random-source-randomize!`, `random-source-pseudo-randomize!`, `random-source->random-integer`, `random-source->random-real`, `random-integer`, `random-real`) under the portable `(surfage s27 random-bits)` name.

Code using `(import (surfage s1 lists))` or `(import (surfage s27 random-bits))` is now portable across Curry, Guile, Chicken, Chibi-Scheme, and other implementations that follow the surfage naming convention.

---

### 1.1.0 — CL condition system · C FFI · STM · channels · spinors · tensor ops

**New features**

- **CL-style condition system** (`src/condition.c`, `(import (curry conditions))`): Common
  Lisp-style non-unwinding error handling. `handler-bind` installs handlers that run with
  the full call stack intact; `with-restarts` establishes named recovery points;
  `invoke-restart` jumps to a recovery option without unwinding; `handler-case` is the
  unwinding fallback. A condition type hierarchy (BFS over parent lists) lets handlers match
  by type family. `signal` returns normally if no handler claims the condition; `condition-error`
  signals then raises. `ignore-errors` returns `(values result-or-#f condition-or-#f)`.
  Built-in types: `error`, `warning`, `math-error`, `singular-matrix`, `no-elementary-form`,
  `gc-pressure`, and others. See `docs/reference/language.md` and `examples/conditions_demo.scm`.

- **General C FFI** (`-DBUILD_FFI=ON`, `src/ffi.c`, `(import (curry ffi))`): Call any C
  function from Scheme via libffi. `define-foreign-library` loads a shared library;
  `define-foreign` declares a typed function binding; `with-pinned-matrix` / `with-pinned-tensor`
  pass matrix/tensor data pointers to C with zero copying (no marshal overhead for BLAS etc.).
  Type system handles `int`, `uint`, `long`, `size_t` (and Scheme-style `size-t`), `double`,
  `float`, `c-ptr`, `string`, `bool`, and `void`. Requires `libffi-dev` on Linux,
  automatically found via Homebrew on macOS.

- **STM + CSP channels** (ported from cill): TL2 software transactional memory
  (`atomically`, `make-tvar`, `tvar-read`, `tvar-write!`, `retry`, `or-else`, `select`);
  CSP buffered channels with synchronous rendezvous (`make-channel`, `channel-send!`,
  `channel-recv!`, `channel-close!`, non-blocking `%channel-try-*`). All primitives in
  global env; `(curry stm)` adds `or-else` and `select` macros. See
  `docs/reference/concurrency.md`.

- **Spinor type** (`T_SPINOR=45`, ported from cill): Weyl (left/right-handed), Dirac, and
  Majorana spinors with correct SL(2,C)/Lorentz transform law. `make-spinor`, `spinor-ref/set!`,
  `spinor+/-`, `spinor-scale`, `spinor-transform`, `spinor-conjugate`, `spinor-adjoint`
  (Dirac γ⁰), `spinor-inner` (Hermitian), `spinor->list`.

- **Tensor index-structure operations** (ported from cill): `tensor-transpose` (axis
  permutation), `tensor-contract` (generalised trace over two axes), `tensor-einsum`
  (Einstein summation notation — `"ij,jk->ik"` for matrix multiply, up to 8 tensors).

**Build changes**

- `BUILD_FFI=ON` CMake option; requires `libffi-dev` / Homebrew libffi.
- Banner and `-v` output now show `(LLVM Enabled)` and `(FFI)` capability tags.
- `src/curry_ffi.h` replaces `src/ffi.h` (renamed to avoid collision with `<ffi.h>`).

---

### 1.0.1 — macOS arm64 LLVM release-build fix; CMake cleanup

**Bug fixes**

- **macOS arm64 TLS ABI mismatch** (Release build with `BUILD_LLVM=ON`): C++ translation units (`jit.cpp`) referenced `extern thread_local gc_nursery` and `g_jit_call_depth`, causing the linker to look for C++ TLS wrapper symbols (`_ZTW10gc_nursery`, `_ZTW16g_jit_call_depth`) that the C compiler never emits — only `$tlv$init`-style symbols. Fixed by routing C++ callers through plain `extern "C"` functions: `gc_alloc_impl()` in `gc.c` and `jit_depth_push()`/`jit_depth_pop()` in `eval.c`. TLS variables and their inline accessors are now hidden from C++ via `#ifndef __cplusplus`. Linux is unaffected (ELF TLS is C/C++ ABI-compatible).

- **Deprecated CMake SQLite target**: `SQLite::SQLite3` → `SQLite3::SQLite3` (new canonical name in CMake's FindSQLite3 module).

- **CMake syntax warning**: missing whitespace between the crypto option description string and `ON` in `CMakeLists.txt`.

---

### 1.0.0 — LLVM ORC v2 JIT backend; work-stealing thread pool; Qt6 confirmed

**New features**

- **LLVM ORC v2 JIT backend** (`-DBUILD_LLVM=ON`): hot `BcClosure` objects are automatically compiled to native ARM64 / x86-64 after 50 calls. The compiled version replaces the bytecode interpreter transparently. `(jit-compile! proc)` force-compiles immediately; `(jit-compiled? proc)` and `(curry-llvm-available?)` let Scheme code detect and drive JIT compilation. Typical speedups: ~6× for recursive functions, ~14× for tight named-let loops.

- **Persistent work-stealing thread pool** (Chase-Lev deques): `map` and `reduce` now dispatch through a pool of `hw_concurrency` worker threads that are created once at startup and parked on a condvar between jobs. Eliminates the ~1 ms per-call thread-spawn overhead of the previous approach. `for-each/par` is an explicit opt-in for parallel side-effects; `for-each` remains always sequential.

- **Qt6 6.x confirmed** alongside LLVM JIT in the same binary (`build-release`). The build script (`/Users/yvain/src/b`) now passes both `$(brew --prefix llvm)` and `$(brew --prefix qt@6)` in `CMAKE_PREFIX_PATH`.

**Bug fixes**

- **Nested parallel-map deadlock**: worker threads calling `map` on lists longer than the parallel threshold would submit nested work items and then block on the pool condvar — with no remaining workers to service those items. Fixed by adding a `_Thread_local bool pool_is_worker` flag; `prim_map`, `prim_reduce`, and `prim_for_each_par` fall back to sequential execution when called from inside a worker thread.

- **Park-broadcast race condition** in `pool_submit`: `n_parked` was read without holding `park_mutex`, so a worker could increment the counter and call `cond_wait` between the load and the broadcast, missing the wake-up permanently. Fixed by acquiring `park_mutex` before checking `n_parked`.

- **`jit-compile!` upvalue crash**: force-compiling a closure with captured upvalues via `jit-compile!` produced "unbound variable" errors because `prim_jit_compile` did not call `jit_wrap_upvals`, unlike the auto-JIT path in `maybe_jit_bcc`. Fixed; `jit_wrap_upvals` is now non-static so `builtins_curry.c` can reach it.

- **Stale bytecode cache** after binary rebuilds: `SCC_FMT_VER` bumped from `0x01` to `0x02` to force recompilation of all cached `.scc` files when the binary changes.

- **PLplot batch-mode hang**: `plinit()` in the plplot module defaulted to `plspause(1)`, causing every `plot-end` call to wait indefinitely for user input even with the `svg` headless device. Fixed with `plspause(0)` before each `plinit()`.

- **Qt6 font warnings on macOS**: test harness used generic font names (`"Sans"`, `"Monospace"`) that do not exist on macOS. Replaced with `"Helvetica"` and `"Menlo"`.

---

### 0.8.18 — Release-build call/cc and MCP SSE fixes

**Bug fixes**

- **`call/cc` — clang ARM64 dead-store elimination**: `cont->result = value` before `longjmp()` was silently eliminated by clang (ARM64 `-O2`) because `longjmp` is declared `[[noreturn]]` and the store appeared dead. Added `__asm__ volatile("" ::: "memory")` barriers in both `eval()`'s TCO loop and `apply()` before each `longjmp` on continuation invocation. Both tree-walker (`eval_call_cc`) and bytecode VM (`prim_call_cc`) paths also now use `*(volatile val_t *)&cont->result` to force a memory reload after `longjmp`, since clang constant-folded the non-volatile read to `V_VOID` at setjmp time. These issues only manifested in release builds; debug builds (`-O0`) were unaffected.

- **`call/cc` — tree-walker optimizer frame instability**: `eval_call_cc()` was split from `eval()`'s giant goto-loop into a dedicated `__attribute__((noinline))` helper so the setjmp frame is stable and all variables accessed in the longjmp path land in callee-saved registers.

- **`guard` / R7RS exception tests**: `guard`'s expansion via `call/cc` depended on working continuation capture; the fixes above unblock `guard`, `with-exception-handler`, `raise`, `raise-continuable`, and `error-object?` tests in `r7rs_tests.scm`.

- **MCP SSE keepalive — Boehm GC signal interruption**: `sleep(15)` in `handle_sse_get`'s keepalive loop was cut short by Boehm GC's stop-the-world signal (EINTR), causing a premature keepalive to be sent — which the SSE isolation test detected as session-1 data leaking to session-2. Replaced with `nanosleep()` + EINTR retry so the full 15-second interval is always observed.

---

### 0.8.17 — Clang/Linux build fixes

**Build fixes**

- Release builds with upstream LLVM Clang on Linux now pass `-fno-omit-frame-pointer`. Clang at `-O2` omits frame pointers for register pressure, which prevents Boehm GC's conservative stack scanner from walking frames correctly and causes use-after-free segfaults. Apple Clang is unaffected (it always preserves frame pointers for Instruments compatibility).
- `prim_call_cc` (`call/cc`): the local `ret` variable is now `volatile` and the function is marked `__attribute__((noinline))`. Without these, Clang's optimizer cached `ret` in a caller-save register across the `setjmp`/`longjmp` boundary, returning garbage on continuation invocation. Again, Apple Clang's AArch64 calling convention masked this on macOS.

---

### 0.8.16 — Phase 11: multi-DOF Lagrangian + Hamiltonian mechanics

**Core C fixes**

- `dot-product` now accepts up/down tuples (previously only handled linked lists and silently returned 0 for tuples).
- `(partial i)`: tuple-valued slots in the local tuple (coordinate, velocity) are now replaced with nested sym-var tuples so that Lagrangians can call `dot-product`/`ref` on them symbolically. For an up-tuple slot, returns a down-tuple of partial derivatives (the covariant gradient).
- `sx_simplify` SX_DIV: common numeric coefficients in `(c1·A)/(c2·B)` are now cancelled — e.g. `(2·px)/(2·m) → px/m`. This fixes Hamiltonian output and Hamilton equation simplification.
- `num_mul`/`num_add`/`num_sub` and `sx_neg`/`sx_add`/`sx_sub`/`sx_mul`: tuple check now fires before symbolic check, enabling scalar×tuple distribution throughout the CAS layer.

**`(curry sicm)` additions**

- `L-free-particle-nd`, `L-harmonic-nd`: n-DOF isotropic Lagrangians using `dot-product`.
- `L-central-rectangular`: central force in 2D Cartesian coordinates.
- `L-Kepler-polar`: Kepler problem in polar coordinates `(up r θ)`.
- `momentum`: selector for slot 2 of a Hamiltonian state `(up t q p)`.
- `Lagrangian->Hamiltonian`: Legendre transform for diagonal mass matrices. Computes mass coefficients from `∂(∂L/∂qdot)/∂qdot`, then builds `H = Σ pᵢ²/(2mᵢ) + V(q)`.
- `Hamilton-equations`: returns `(up dH/dp, −dH/dq)` at a Hamiltonian state.
- `make-Hamiltonian`: direct `T*(p) + V(q)` Hamiltonian constructor.
- `Poisson-bracket`: `{f,g} = Σ (∂f/∂qᵢ · ∂g/∂pᵢ − ∂f/∂pᵢ · ∂g/∂qᵢ)`.
- `commutator`: `[A,B]f = A(Bf) − B(Af)`.

**Tests:** `sicm_tests.scm` expanded from 35 to 55 assertions (added §§16–21: 2D harmonic oscillator EOM, Kepler EOM, 1D/2D Hamiltonians, Hamilton equations, Poisson bracket identities including `{q,p}=1`).

**Internal simplifications**

- `numeric.c`: `tuple_binop` / `tuple_unop` helpers replace copy-pasted element-wise loops in `num_add`, `num_sub`, `num_neg`. `UNPACK_QUAT` macro replaces a 4-line quaternion-or-scalar extraction at five sites.
- `symbolic.c`: 18 transcendental one-liners (`sx_sqrt` … `sx_csc`) replaced by `SX_UNARY` / `SX_UNARY_NUM` macro table — adding a new transcendental now takes one line.

---

### 0.8.15 — SICM module fixes and test coverage

**`(curry sicm)` bug fixes**

- `Lagrangian->V` was listed in the module header as a supported procedure
  but was never implemented. It now correctly extracts the potential energy
  from a Lagrangian by evaluating L at zero velocity (`V = −L(t,q,0)`),
  which works because kinetic energy vanishes when nothing is moving.
- `Lagrangian->T` previously required the potential function V to be passed
  explicitly as a second argument. It now derives V from the Lagrangian
  itself (`T = L + V`) and takes only L.
- `literal-function*` used `iota`, which is not implemented in Curry.
  Replaced with an explicit loop.

**Test coverage**

- `tests/sicm_tests.scm` expanded from 21 to 35 assertions, covering the
  previously untested procedures: `literal-function*`, `Lagrangian->V`,
  `Lagrangian->T`, `make-Lagrangian`, `square` on `down` tuples,
  `Euler-Lagrange-operator` with free particle and gravity Lagrangians, and
  numeric `Lagrange-equations` verified against an exact analytic solution.

**Refactoring**

- `src/scc.c`: extracted shared `load_chunks_from_file` helper from the
  near-duplicate `read_scc` and `scc_load_direct` functions.

**Documentation**

- `docs/pkg-design.md` — design evaluation and recommendation for the
  `curry pkg` package manager (registry model, lock files, environments,
  C extension handling, versioning, package identity, manifest format,
  security).

---

### 0.8.14 — Akkadian/CLI test suites; SCC cache GC bug fix

**Test suites**

- `tests/akkadian_tests.scm` — 205 assertions covering every entry in
  `akkadian_names.h` in both transliterated Akkadian (e.g. `šakānum`) and
  cuneiform (e.g. `𒁹`) forms, for all AKK_SF special-form synonyms and
  AKK_PR procedure aliases.
- `tests/test_cli.sh` — 30 shell-level assertions for CLI features:
  shebang handling in `.scm` files, `-c` compile-to-`.scc`, `-c -o`
  custom output path, `-c -x` executable flag (shebang prepend + chmod),
  combined getopt flags (`-xc FILE`), magic-byte detection for
  extension-less `.scc` files, `-l` load-before-eval, script argument
  passing via `command-line-args`.
- Both suites are registered in `tests/CMakeLists.txt` and run via
  `ctest`.

**SCC cache GC bug fix**

- `read_scc` and `scc_load_direct` allocated the `Chunk**` pointer array
  with plain `malloc`.  Boehm GC does not scan non-GC heap memory for
  interior pointers, so the `Chunk` objects could be collected while the
  run loop was still executing them.  This manifested as a non-deterministic
  `unbound variable: <garbled>` error on the second run of any script that
  triggered a GC collection mid-loop (reproducibly hit by the 274-chunk
  akkadian test suite).  Fixed by using `GC_MALLOC` for both arrays.

**Documentation**

- `docs/vm.md` — new Bytecode Cache section: `.scc` format, constant-pool
  tags, cache validation, and the `GC_MALLOC` requirement.
- `docs/akkadian-reference.md` — bumped to v0.8.14; added test-coverage
  section.
- `CLAUDE.md` — expanded test suite table.

---

### 0.8.13 — `.scc` bytecode cache; Qt6 scroll/click fixes

**Bytecode cache (`.scc` files)**

- Compiled `Chunk` arrays are now cached alongside their source as
  `<script>.scc`, skipping recompilation on subsequent runs when the
  source file's mtime and size are unchanged.
- Two-tier lookup: source-adjacent `.scc` first; falls back to
  `~/.cache/curry/<mirrored-abs-path>.scc` when the source directory is
  not writable (system-installed scripts, read-only mounts, etc.).
- Cache is invalidated automatically on any content change or Curry
  version bump.  One chunk per top-level form preserves
  macro-expansion semantics across the file.
- `src/version.h` extracted so the version string is shared between
  `main.c` and `scc.c` without repetition.

**Qt6 / mandelbrot fixes**

- `setAttribute(WA_AcceptTouchEvents, false)` prevents macOS from
  swallowing trackpad scroll events as native gestures before they reach
  `wheelEvent`.
- `canvas->raise()` fixes click hit-testing on macOS where the native
  `NSScrollView` sits above `QOpenGLWidget` in the z-order.
- `timer-start!` is now idempotent — calling it on an already-running
  timer is a no-op, preventing duplicate ticks.
- `request-render!` no longer spawns actors directly; it sets a
  `*view-dirty*` flag and lets the 16 ms render timer gate actual spawns,
  preventing an O(n) thread explosion on rapid mouse-move events.

---

### 0.8.12 — `case` compiled natively by the bytecode compiler

**`case` special form in the bytecode compiler**

- `case` was handled by the tree-walking evaluator but not by the bytecode
  compiler.  Any script using `case` inside a compiled lambda would fail with
  `unbound variable: case`.
- Fixed by adding `compile_case` to `compiler.c`, which desugars `(case key
  clause...)` into `(let ((%%case-key%% key)) (cond ...))` at compile time and
  recurses into the existing `compile_let` / `compile_cond` paths.  This gives
  correct tail-call semantics for free: a `case` in tail position compiles its
  matching body in tail position.
- All three clause forms are supported:
  - `((datum...) expr...)` — eqv? match via `memv`, body compiled in sequence
  - `(else expr...)` — unconditional fallthrough
  - `((datum...) => proc)` — calls `(proc key)` on a match

---

### 0.8.11 — REPL ,vm command: GC heap stats and VM introspection

**New REPL command: `,vm`**

- Prints a snapshot of the Boehm GC heap and VM execution state:
  - `heap:` — bytes currently in use vs total heap committed to the process
  - `alloc:` — total lifetime bytes allocated (monotonically increasing)
  - `gc:` — number of GC collection cycles completed since startup
  - `stack:` — current VM value-stack depth vs the 4096-slot ceiling
  - `frames:` — current call-frame depth vs the 256-frame ceiling
- Useful for spotting memory growth, GC pressure, or unexpectedly deep
  recursion without reaching for an external profiler.
- Implemented via `GC_get_heap_size`, `GC_get_free_bytes`,
  `GC_get_total_bytes`, and `GC_gc_no` from the Boehm GC public API.
- `,help` updated to list `,vm` alongside the existing commands.

---

### 0.8.10 — GC root fix for VM struct; vm_push overflow check; Qt6 exception safety

**Critical: VM struct protected from Boehm GC collection**

- `_Thread_local VM *vm` is not scanned by Boehm GC's conservative collector
  (TLS is not in the stack, globals, or register set that GC scans).  When any
  allocation inside a primitive (e.g. `vector`) triggered a collection cycle,
  Boehm GC could determine the VM struct was unreachable, collect it, and reuse
  the memory — overwriting `vm->sp` with zero.  The subsequent `vm->sp -= argc + 1`
  wrapped to `0xffffffffffffffxx`, causing a SIGSEGV on the next stack write.
  Fixed by allocating the VM struct with `GC_MALLOC_UNCOLLECTABLE` so GC scans
  its interior for live `val_t` references but never frees it.  `vm_free` now
  calls `GC_FREE` explicitly.

**`vm_push` overflow check**

- The inline `vm_push` in `vm.h` (used by `apply_arr` and `apply` in `eval.c`)
  had no bounds check, unlike the `PUSH` macro inside `vm_run`.  Added a call to
  a new `vm_stack_overflow()` function (noreturn, raises a Scheme error) when
  `vm->sp` reaches the stack ceiling.

**Public API: `curry_is_error` / `curry_error_message`**

- Two new functions added to `include/curry.h` and implemented in `src/api.c`:
  - `curry_is_error(v)` — returns true if `v` is a Scheme error object (`T_ERROR`)
  - `curry_error_message(v)` — extracts the string message from an error object,
    or `NULL` if the message is not a string

**Qt6: VM state save/restore and exception reporting**

- When a Scheme exception fires inside `curry_apply()` from a Qt callback,
  `longjmp` bypasses the VM's normal `vm->sp` restoration, leaving the stack
  pointer corrupted for all subsequent callbacks.  `SCHEME_CALL` now saves
  `vm->sp`, `frame_count`, and `open_upvalues` via `curry_vm_state_save` before
  each callback and restores them in the exception handler.  The same save/restore
  is applied directly in `paintEvent`.
- New `qt6_print_exn(where, exn)` helper prints a human-readable error message
  (using the new `curry_error_message` API) to stderr whenever a Scheme exception
  is caught at a Qt boundary.

---

### 0.8.9 — VM bug-fixes: exactness, guard, macro expansion, profiling, MCP SSE

**Constant pool exactness preserved**

- `chunk_add_const` used `num_eq` to deduplicate constants, which ignores
  exactness — `num_eq(2, 2.0)` returns true, causing flonum `2.0` literals to be
  silently replaced by fixnum `2` in the constant pool.  Replaced with `scm_eqv`,
  which respects type: `(eqv? 2 2.0) = #f`.  Fixes the Redis test suite where
  `zscore` expected a flonum but received a fixnum.

**`T_BCCLOSURE` in the `ObjType` enum**

- `T_BCCLOSURE` was only a `#define` in `vm.h`, not a member of the `ObjType`
  enum in `object.h`.  The `vis_proc` macro did not include it, so C extensions
  (e.g. the MCP module) could not recognise VM-compiled closures as procedures.
  `T_BCCLOSURE = 41` is now in the enum and `vis_proc` checks for it.

**`guard` compiled natively**

- `guard` was delegated to the tree-walking evaluator, so bindings introduced by
  `let`/`define` in the surrounding VM frame were invisible to the guard body
  (looked up in `GLOBAL_ENV` and raised "unbound variable").  `guard` is now
  desugared at compile time into `call/cc + with-exception-handler + cond`,
  allowing it to capture local upvalues correctly.

**Macro expansion at compile time**

- The compiler now consults `GLOBAL_ENV` for syntax transformers before
  attempting to compile a call.  If the operator is a `syntax-rules` macro,
  the transformer is applied at compile time and the result compiled.  Expansion
  errors are wrapped in a `(raise ...)` form so they surface at runtime with
  full context.  Fixes `syntax_rules` test cases that called macros from
  compiled (VM) code.

**BcClosure profiling**

- Profiling hooks (level 1 call-count, level 2 timed) are now wired into
  `OP_CALL`, `OP_TAIL_CALL`, and `OP_RETURN` in `vm.c`, and into the
  `vis_bcclosure` branch of `apply()`/`apply_arr()` in `eval.c`.  `CallFrame`
  carries a `prof_start_ns` field for level-2 timing.

**MCP SSE threads: `vm_init()` on entry**

- Each SSE connection spawns a `conn_thread` (pthread).  `gc_register_thread()`
  was called but `vm_init()` was not, leaving the thread-local `vm == NULL`.
  Any `tools/call` request that invoked a BcClosure dereferenced `vm->sp` →
  SIGSEGV.  `vm_init()` is now called immediately after `gc_register_thread()`
  in `conn_thread`.

---

### 0.8.8 — VM as primary engine; call/cc, parameterize, quasiquote in compiler

**VM is now the primary script execution engine**

- `main.c` now loads scripts through `compiler_compile + vm_run` per top-level
  form instead of `scm_load` (the tree-walker).  REPL, `-e`, and file execution
  all route through the bytecode VM.

**Thread-local VM state**

- `VM *vm` changed from a process-global to `_Thread_local`; each thread must
  call `vm_init()` before using `vm_run`.  Actor threads now call `vm_init()`
  at startup, fixing a data-race SEGFAULT when actors used compiled closures.

**`call/cc` as a first-class builtin**

- `call-with-current-continuation` and `call/cc` are now registered C
  primitives (`prim_call_cc`) rather than tree-walker special forms.  They
  work correctly when called from compiled (VM) code.
- `prim_call_cc` saves and restores `vm->frame_count`, `vm->sp`, and
  `vm->open_upvalues` around `setjmp`/`longjmp` so that a longjmp escaping
  nested `vm_run` frames leaves the VM in a consistent state.  The same
  save/restore was added to `prim_with_exception_handler`.

**`parameterize` compiled natively**

- `parameterize` is removed from the eval-delegate list and compiled by a new
  `compile_parameterize()` function that desugars it at compile time to
  `let + dynamic-wind`.  Local variables referenced in the body (e.g. a
  continuation `k`) are now captured as upvalues rather than looked up in
  `GLOBAL_ENV`, fixing "unbound variable" errors when `parameterize` enclosed
  a `call/cc`-bound variable.

**`quasiquote` in the compiler**

- The compiler now handles `` ` `` / `quasiquote` directly: it calls `expand_qq()`
  from `eval.c` to expand the template into ordinary list-construction code
  and compiles the result.  `expand_qq` is now a public symbol declared in
  `eval.h`.

**Multiple-values fixes**

- `OP_VALUES N` now produces a proper `T_VALUES` object instead of a plain
  list, so `call-with-values` can distinguish a single-list return from
  multiple values.
- `OP_CALL_WITH_VALUES` and the new `prim_call_with_values` builtin both
  unpack `T_VALUES` objects and spread the values as separate arguments to
  the consumer.

**`OP_TAIL_CALL` entry-depth guard**

- The non-`BcClosure` path of `OP_TAIL_CALL` was missing the `entry_depth`
  check that detects when a nested `vm_run` call has completed.  This caused
  a SEGFAULT (executing garbage as opcodes) whenever a primitive was in tail
  position inside a nested call.  The check is now present on all return paths.

**New test suite**

- `tests/dynamic_wind_tests.scm` (16 tests) covering `make-parameter`,
  `parameterize` (normal and escape-via-`call/cc`), nested `parameterize`,
  converter callbacks, and `dynamic-wind` ordering.

---

### 0.8.7 — VM robustness and BcClosure interop

**VM safety**

- Value stack overflow (`PUSH` with > 4096 entries) now raises a proper Scheme
  error instead of silently writing past the stack array.
- Call-frame overflow (> 256 nested calls) now raises a Scheme error at both the
  `vm_run` entry point and the `OP_CALL` dispatch site; previously the check
  printed to stderr and returned `void` without unwinding.

**BcClosure interoperability**

- `procedure?` now returns `#t` for compiled (`BcClosure`) procedures.  Previously
  only tree-walker closures, primitives, continuations, and traced values were
  recognised.
- `apply_arr` (the cross-engine dispatch used by `apply`, `map`, `for-each`, etc.)
  now correctly handles `BcClosure` callees by pushing arguments onto the VM stack
  and delegating to `vm_run`.  Previously a compiled lambda passed to `map` would
  raise "not a procedure".

### 0.8.6 — Bytecode compiler and VM

Curry now executes via a **stack-based bytecode VM** instead of the
tree-walking interpreter.  All top-level evaluation — REPL, `-e`, file
load — goes through the new pipeline.

**Compiler** (`src/compiler.c`)

- Single-pass AST → `Chunk` bytecode compiler; each `lambda` produces one
  `Chunk` object with a constant pool, byte stream, and line table.
- Variable resolution at compile time: local → upvalue → global.  Upvalue
  capture marks enclosing locals and emits `[is_local, index]` capture
  descriptors after `OP_CLOSURE`.
- Lambda bodies pre-scanned for internal `(define …)` forms; pre-declared
  with a sentinel depth of `-1` giving **letrec\*** semantics.
- All scope-forming constructs (`let`, `let*`, `letrec`, `do`, named `let`)
  compiled as **lambda calls** rather than inlined scopes, preventing
  slot-index collisions when they appear as call arguments.
- Full special-form coverage: `quote`, `if`, `begin`, `define`, `set!`,
  `lambda`, `let`, `let*`, `letrec`, `letrec*`, named `let`, `and`, `or`,
  `cond` (including `=>`), `when`, `unless`, `do`, `values`, `apply`.
- Akkadian/cuneiform synonyms translated via `akk_translate()` before
  dispatch — Akkadian source compiles identically to its English equivalent.

**VM** (`src/vm.c`, `src/vm.h`)

- Flat value stack of `val_t` (`VM_STACK_MAX` = 4096); call stack of
  `CallFrame` (`VM_FRAMES_MAX` = 256).
- Calling convention: callee at `slots[-1]`, args at `slots[0..N-1]`.
  `OP_RETURN` replaces the callee+args window with the result.
- **Tail-call optimisation**: `OP_TAIL_CALL` reuses the current `CallFrame`
  for `BcClosure` callees — `memmove` args over slots, reset `ip`.
  Non-`BcClosure` callables tail-call via `apply_arr()`.
- **Upvalue protocol** (same as Lua 5): open upvalues point into the live
  stack; `vm_close_upvalues` copies them to `Upvalue.closed` on scope exit.
- `OP_CLOSE_UP A` closes the upvalue for `frame->slots[A]` without popping.
  `OP_SLIDE N` drops N locals below TOS in one step (scope-exit cleanup).
- `vm_reset()` sanitises stack state after a caught exception.
- Interoperability: primitives and tree-walker closures are dispatched
  via `apply_arr()`; both engines share `GLOBAL_ENV`.

**Opcode set** (`src/opcode.h`, `src/chunk.c`)

70 opcodes covering constants, locals, globals, upvalues, stack
manipulation, full numeric tower arithmetic, comparison, pairs/lists,
strings/chars, type predicates, vectors, control flow, calls, closures,
apply/values, exception handling, and I/O.  New opcodes: `OP_SLIDE`
(scope cleanup) and a corrected `OP_CLOSE_UP` (slot-addressed, non-popping).

**Bug fixes during development**

- `end_compiler` was emitting `OP_VOID` before `OP_RETURN`, causing all
  lambdas to return void.
- `OP_JUMP_FALSE` / `OP_JUMP_TRUE` always pop their condition; spurious
  `OP_POP` instructions after them in `compile_cond`, `compile_when`, and
  `compile_unless` were removed.
- `cond =>` clause now `DUP`s the test before `JUMP_FALSE` so the test
  value survives the pop for the `(proc test)` call.

**Documentation**

`docs/vm.md` — full architecture reference: calling convention, TCO,
upvalue open/closed protocol, compiler scope model, special-form
compilation strategies, complete opcode table, known limitations.

### 0.8.5 — Quaternion trig, non-commutative CAS, Akkadian expansion

**Quaternion numeric tower — transcendental functions**

All nine transcendental functions now handle quaternion arguments. Every `q = a + v̂·‖v‖` is embedded in the complex plane spanned by `{1, v̂}`, the complex formula is applied, and the result is reconstructed:

- `sin`, `cos`, `sinh`, `cosh`: direct closed-form in the {1, v̂} plane
- `tan`, `tanh`: routed through sin/cos
- `asin`, `acos`, `atan`: complex embedding via `z = a + ‖v‖·i`, apply formula, reconstruct
- `asinh`, `acosh`, `atanh`: extended condition covers quaternion alongside complex
- `exp`, `log`, `sqrt`: new quaternion branches using `quat_assemble()` helper
- `abs(quaternion)` now returns the Euclidean norm `√(a²+b²+c²+d²)` (previously returned the quaternion unchanged)
- `num_sub` and `num_div` (Hamilton right-division `a·conj(b)/‖b‖²`) were missing quaternion branches — added

Euler's identity `exp(πv̂) = −1` holds for any unit pure-imaginary quaternion `v̂`. The Pythagorean identity `sin²(q)+cos²(q) = 1` holds for all quaternions.

**Symbolic CAS — non-commutative products**

- `SYM_ASSUME_QUATERNION` flag on `sym-var`: `(sym-var 'q 'quaternion)` declares a quaternion-valued variable
- `SX_NCMUL` operator: an ordered, non-commutative product node. `sx_mul()` routes to `SX_NCMUL` whenever any operand is a concrete quaternion/octonion or a quaternion-flagged sym-var
- Real scalars (fixnum/flonum/bignum/rational) commute out as a leading coefficient; all other factors maintain left-to-right order
- Differentiation: ordered product rule — `∂(f₁·f₂·…·fₙ)/∂x = Σᵢ f₁·…·(∂fᵢ/∂x)·…·fₙ`
- `expand`: `expand_ncmul2()` recursively distributes NC products over sums, so `(q+p)²` yields four terms (`q²+qp+pq+p²`) rather than the commutative three
- Integer exponent expansion (`expt q n`) uses NC multiplication when the base is quaternion-flagged
- `num_is_zero`, `num_is_one`, `num_cmp` extended for quaternions so simplification rules fire correctly on quaternion coefficients

**Akkadian / cuneiform**

- `sym-assumption?` → `ṣimdat-la-idûm?` / `𒋻𒉡𒅆?` ("decree of the unknown?")
- Assumption keywords accepted in both English and Akkadian in `sym-var` and `sym-assumption?`: `ṣīrum`/real, `damqum`/positive, `lemnûm`/negative, `nikkassum`/integer, `la-ṣifrum`/nonzero, `rebûm`/quaternion

**Quaternion builtins — previously unregistered procedures now exposed**

- `quaternion-w`, `quaternion-x`, `quaternion-y`, `quaternion-z` — component accessors
- `quaternion-norm` — Euclidean norm `√(w²+x²+y²+z²)`
- `quaternion-conjugate` — `a−bi−cj−dk`
- `quaternion-normalize` — unit quaternion
- `quaternion-inverse` — `conj(q)/‖q‖²`
- `quaternion+` — variadic addition
- `quaternion*` — variadic Hamilton product
- `quaternion-rotate-vector` — rotate a 3-vector by a quaternion via `q·v·q⁻¹`
- `conj` generic now delegates to quaternion conjugate (previously fell through to no-op)
- `eqv?` and `equal?` now compare quaternions by component value, not pointer identity

**Symbolic CAS — additional simplifications**

- Like-term collection in sums: `(+ q q)` → `(nc* 2 q)`, `(+ (* 3 q) (* -3 q))` → `0`; works for commutative (`*`) and non-commutative (`nc*`) products alike
- NC product scalar folding: real-embedded quaternion `a+0i+0j+0k` folds into its real scalar part within `nc*`; a scalar of −1 folds into negation: `(* -1 q)` → `(- q)`
- NC integration factoring: leading and trailing constant quaternion factors are extracted around the integral of the variable-dependent middle block, preserving left-to-right order

**Tests**

300 assertions in `tests/numeric_ext_tests.scm`; new sections cover quaternion builtins (accessors, norm, conjugate, normalize, inverse, +, *, rotate-vector), corrected `conj`/`eqv?`/`equal?` behavior, and CAS simplifications (like-term collection, −1 folding, NC integration factoring).

### 0.8.4 — GPIO interrupts and Akkadian completeness

**GPIO interrupt support** (`(curry rpi)` module):

- `gpio-open` now accepts `'rising`, `'falling`, and `'both` as direction modes, configuring a line for libgpiod edge-event monitoring instead of plain input/output.
- **`(gpio-wait-edge handle [timeout-ms])`** — blocking wait for a GPIO edge using `poll()` on the libgpiod event fd. Returns `'rising`, `'falling`, or `#f` on timeout. Pass `-1` (default) to wait indefinitely. Designed to be wrapped in `spawn` for async use.
- **`(gpio-watch handle proc)`** — spawns a background C thread that calls `(proc edge timestamp-ns)` on each interrupt. The Scheme callback is kept alive as a Boehm GC root for the lifetime of the watcher. Returns a watcher handle.
- **`(gpio-unwatch watcher)`** — signals the watcher thread via a stop-pipe, joins it, removes the GC root, and frees the struct.
- **`(watcher? v)`** — predicate.

**Akkadian/cuneiform completeness** — ~69 new transliterated and cuneiform aliases added to cover all R7RS procedures introduced in v0.8.3 (plus several from v0.7.7 that were missing):

- Arithmetic: `square` (*mitḫartum*), `exact-integer?`, `truncate/`, `truncate-quotient`, `truncate-remainder`, `exact-integer-sqrt` (*ibum-kinattu*)
- I/O: binary port procedures (`read-u8`, `write-u8`, `peek-u8`, `u8-ready?`), `read-string`, `read-bytevector`, `write-bytevector`, file operations (`file-exists?`, `delete-file`, `call-with-input-file`, `call-with-output-file`, `with-input-from-file`, `with-output-to-file`)
- Strings: all ordering comparators (`string<=?` through `string-ci>=?`), `string-set!`, `string-copy!`, `string-for-each`, `string-fill!`, `string-foldcase`, `string->utf8`, `utf8->string`
- Bytevectors: complete suite (`make-bytevector`, `bytevector`, `bytevector-length`, `bytevector-u8-ref`, `bytevector-u8-set!`, `bytevector-copy`, `bytevector-copy!`, `bytevector-append`)
- Characters: all comparators (`char=?` through `char>=?`), all case-insensitive variants, `digit-value`, `char-foldcase`
- Vectors: `vector-append`, `vector-copy!`
- Process context: `get-environment-variable`, `get-environment-variables`, `emergency-exit`
- Time: `current-second`, `current-jiffy`, `jiffies-per-second`
- Error objects: `error-object-message`, `read-error?`, `file-error?`
- Lists: `make-list`

**Internal**: `src/builtins.c` split into `src/builtins.c` (R7RS standard procedures) and `src/builtins_curry.c` (CAS, vector calculus, quantum, surreal, quadrature extensions). `defprim()` made non-static for cross-file use.

---

### 0.8.3 — R7RS compliance gap-fill and RPi test suite

**R7RS compliance** — ~50 new procedures filling the remaining gaps in `(scheme base)`, `(scheme char)`, `(scheme file)`, `(scheme process-context)`, `(scheme time)`, and `(scheme write)`:

- **Arithmetic**: `square`, `exact-integer?`, `truncate/`, `truncate-quotient`, `truncate-remainder`, `exact-integer-sqrt`
- **Characters**: `char<=?`, `char>?`, `char>=?`, `char-ci=?`, `char-ci<?`, `char-ci>?`, `char-ci<=?`, `char-ci>=?`, `digit-value`
- **Strings**: `string<=?`, `string>?`, `string>=?`, `string-ci=?`, `string-ci<?`, `string-ci>?`, `string-ci<=?`, `string-ci>=?`, `string-upcase`, `string-downcase`, `string-set!`, `string-copy!`
- **Bytevectors**: `bytevector`, `bytevector-copy`, `bytevector-copy!`, `bytevector-append`
- **Vectors**: `vector-append`
- **I/O**: `flush-output-port`, `char-ready?`, `u8-ready?`, `read-u8`, `peek-u8`, `read-string`, `read-bytevector`, `read-bytevector!`, `write-u8`, `write-bytevector`, `write-simple`, `delete-file`, `call-with-input-file`, `call-with-output-file`, `with-input-from-file`, `with-output-to-file`, `file-exists?`
- **Process context**: `get-environment-variable`, `get-environment-variables`, `emergency-exit`
- **Time**: `current-second`, `current-jiffy`, `jiffies-per-second`
- **Error handling**: `error-object-message` alias (both names accepted), `read-error?`, `file-error?`; `scm_raise` now tags read/file errors correctly; `open-input-file` / `open-output-file` raise `file-error` instead of returning `#f`

**RPi test suite** (`tests/test_rpi.scm`): predicate and type-error tests always run; hardware sections (`gpio-open`, `i2c-open`, `spi-open`, `pwm-open`) skip gracefully when device nodes are absent — passes on CI and on Pi hardware alike.

---

### 0.8.2 — CAS Phase 7: assumptions + exotic limits

**Assumption flags on symbolic variables:**

`(sym-var 'x 'positive)` (and `'negative`, `'real`, `'integer`, `'nonzero`) stores a domain assumption in the variable's flag word. Assumptions unlock targeted simplification rules:

- **`(sym-var 'x 'positive)`** — `|x| → x`, `√(x²) → x`, `log(xⁿ) → n·log(x)`, `sign(x) → 1`
- **`(sym-var 'x 'negative)`** — `|x| → −x`, `sign(x) → −1`

```scheme
(define xp (sym-var 'x 'positive))
(abs xp)                          ; => xp
(simplify (sqrt (expt xp 2)))     ; => xp
(simplify (log (expt xp 3)))      ; => (* 3 (log x))
(sym-assumption? xp 'nonzero)     ; => #t  (implied by positive)
```

**`(sign x)`** — new sign function; evaluates numerically on constants and simplifies to `1`/`-1` with assumption flags. Output renders in both `sym->infix` and `sym->latex`.

**Exotic indeterminate limits:**

All four classical indeterminate forms now resolve:

```scheme
(symbolic x)
(limit (* x (log x)) x 0.0 'right)          ; => 0    (0·∞)
(limit (expt x x)       x 0.0 'right)       ; => 1    (0⁰)
(limit (expt x (/ 1 x)) x +inf.0)           ; => 1    (∞⁰)
(limit (expt (+ 1 (/ 1 x)) x) x +inf.0)    ; => e    (1^∞)
```

Algorithm: `0·∞` rewrites as a ratio and applies L'Hôpital; power forms rewrite `f^g` as `exp(g·log(f))`, take the limit of the exponent, then exponentiate. A new internal `sx_ratio_simplify` function cancels the L'Hôpital derivative quotient without interfering with the simplifier.

---

### 0.8.1 — CAS Phase 5: Taylor series

- **`(series f x a n)`** — truncated Taylor/Maclaurin series of `f` around point `a` to order `n`.  
  Computed by iterating `sx_diff` / `sx_substitute`; zero-coefficient terms are dropped.  
  Integer-valued flonum derivatives (e.g. `exp(0) = 1.0`) are coerced to fixnums before dividing by `k!`, so expansions around exact points yield **exact rational coefficients**: `1/2`, `1/6`, `1/24` …  
  Output is a plain symbolic ADD expression — composable with `simplify`, `substitute`, `∂`, `sym->infix`, `sym->latex`.

```scheme
(symbolic x)
(series (exp x) x 0 4)   ; (+ 1 x (* 1/2 x²) (* 1/6 x³) (* 1/24 x⁴))
(series (sin x) x 0 5)   ; (+ x (* -1/6 x³) (* 1/120 x⁵))
(sym->latex (series (cos x) x 0 4))
; 1 - \frac{1}{2} x^{2} + \frac{1}{24} x^{4}
```

---

### 0.8.0 — Maxwell's equations: four interactive workbooks

Four interactive Qt6 demos — one per Maxwell equation — each paired with a
student guide that derives the physics, walks through the simulation, and
includes guided exercises.  All four use the built-in symbolic CAS to verify
the relevant identity live in the sidebar.

- **Faraday's Law** (`examples/faraday-explorer.scm`, `docs/faraday-explorer.md`)  
  Animated solenoid with time-varying B; induced E_φ computed from ∇×E = −∂B/∂t.
  Exact two-region solution (linear / 1/r), EMF saturation, Lenz's-law phase demo.
  CAS: verifies ∇×E + ∂B/∂t = 0 for a plane wave symbolically.

- **Ampère's Law** (`examples/ampere-explorer.scm`, `docs/ampere-explorer.md`)  
  Two modes toggled live: conduction current (wire) vs. displacement current
  (capacitor charging).  Demonstrates the 90° phase contrast between the two.
  CAS: verifies ∇×B − μ₀ε₀∂E/∂t = 0 for a plane wave.

- **Gauss's Law for E** (`examples/gauss-e-explorer.scm`, `docs/gauss-e-explorer.md`)  
  Uniformly-charged sphere; Gaussian surface draggable from centre to exterior.
  Shows flux saturation at r = R and the r³ / r² field profile.  Sign toggle.
  CAS: ∇·(r̂/3) = 1 = ρ/ε₀ (normalised units).

- **Gauss's Law for B** (`examples/gauss-b-explorer.scm`, `docs/gauss-b-explorer.md`)  
  2D magnetic dipole; positionable Gaussian surface demonstrates ∮B·n̂dl = 0
  when both poles are enclosed, and what a monopole *would* look like.
  CAS: proves ∇·(∇×A) = 0 identically for a concrete A = (0, xy, xyz).

---

### 0.7.9 — CAS Phase 4: limits, IBP integration, vector calculus; Raspberry Pi module

**Symbolic integration — new patterns:**
- **Integration by parts** for polynomial × trig/exp products: `∫x·sin(x)`, `∫x·cos(x)`, `∫x·exp(x)`, and iterated IBP for `∫x²·sin(x)` etc.
- **Polynomial × logarithm** (LIATE rule): `∫x^n·ln(x) = x^(n+1)·ln(x)/(n+1) − x^(n+1)/(n+1)²`
- **Trig power reductions** via half-angle: `∫sin²(f) = x/2 − sin(2f)/(4f′)`, `∫cos²(f) = x/2 + sin(2f)/(4f′)`
- **Quadratic denominator**: `∫c/(ax²+bx+d) = 2c/√Δ · atan((2ax+b)/√Δ)` when Δ=4ad−b²>0; handles completing-the-square automatically

**New `limit` procedure:**
- `(limit f x a)` — two-sided limit; `(limit f x a 'left/'right)` for one-sided
- Direct substitution, L'Hôpital for 0/0 and ∞/∞ (up to 5 applications), `finite/∞ = 0`
- Three-deep L'Hôpital works: `(limit (/ (- x (sin x)) (expt x 3)) x 0)` → `1/6`

**Vector calculus (Cartesian, N-dimensional):**
- `(grad f vars)` / `(gradient f vars)` — gradient of a scalar field
- `(divergence F vars)` — divergence of a vector field
- `(curl F vars)` — curl (3D)
- `(laplacian f vars)` / `(vec-laplacian F vars)` — scalar and vector Laplacian
- `(dot-product A B)` / `(cross-product A B)` — symbolic dot and cross products
- Identities verified symbolically: `div(curl F) = 0`, `curl(grad f) = (0 0 0)`
- Maxwell's equations verified for a plane wave in vacuum (see `docs/symbolic.md`)

**Simplifier improvements:**
- `a − a = 0` for any structurally-equal symbolic expressions
- `a + (−a) = 0` cancellation in the ADD simplifier

**New module `(curry rpi)`** — GPIO, I2C, SPI, and PWM for Raspberry Pi and
compatible Linux embedded boards (Orange Pi, Radxa, Armbian, etc.).  Linux
only; not supported on macOS.  Enable with `-DBUILD_MODULE_RPI=ON`.

- **GPIO** via `libgpiod` — the modern kernel character-device interface
  (`/dev/gpiochipN`).  Replaces the deprecated sysfs approach.
  `gpio-open`, `gpio-read`, `gpio-write`, `gpio-close`
- **I2C** via direct `ioctl` on `/dev/i2c-N` — no extra library beyond
  `libgpiod-dev`.  `i2c-open`, `i2c-read`, `i2c-write`, `i2c-close`
- **SPI** via direct `ioctl` on `/dev/spidevN.M` — full-duplex transfers as
  bytevectors.  `spi-open`, `spi-transfer`, `spi-close`
- **PWM** via sysfs `/sys/class/pwm` — nanosecond precision, works with
  `dtoverlay=pwm`.  `pwm-open`, `pwm-set!`, `pwm-enable!`, `pwm-disable!`, `pwm-close`
- All handles are opaque tagged pairs; predicate procedures (`gpio?`, `i2c?`,
  `spi?`, `pwm?`) provided for each type
- Setup guide with hardware examples at [docs/RPI.md](docs/RPI.md)
- Full API reference at [docs/module-rpi.md](docs/module-rpi.md)

---

### 0.7.8 — Profiling level-2 overhaul, raw builtins, solar system HUD

**Profiling level 2 — accurate wall-clock timing**:
- Level 2 now intercepts named closures *before* the `goto tail` optimisation so that a real return address exists and wall-clock time can be measured per call, not just counted. Previously, timing only covered the `apply()` path; now it covers every call to a named closure except self-tail-recursive ones
- Self-tail-recursive calls (where a closure calls itself as its own tail position) are exempted from the intercept — they fall through to the normal TCO path — to prevent unbounded stack growth in hot loops. They are still counted. Mutually recursive functions are fully timed
- The level-2 description in `docs/module-profiling.md` updated to document the trade-off

**Raw built-in procedures** (no import needed):
- `(profiling-report)` — equivalent to the module's `(profiler-report)`; returns the sorted `((name . (calls . ns)) ...)` alist
- `(profiling-reset)` — equivalent to `(profiler-reset)`; clears accumulated data
- Together with `(set! **eval-profiler** 2)`, these let scripts enable and query the profiler without importing `(curry profiling)`

**Solar system demo — live profiling HUD** (`examples/solar-system-qt6.scm`):
- New overlay displaying the top 12 hottest named closures by accumulated wall-clock time, heat-mapped from yellow (hottest) to grey, updated every animation frame
- Toggle with the **Profile HUD \[p\]** sidebar checkbox or by pressing **`p`**
- **Reset Profiler** button clears counters without restarting the simulation
- Demo enables level 2 at startup via `(set! **eval-profiler** 2)`

---

### 0.7.7 — R7RS compliance, fold fixes, extended Akkadian vocabulary, runtime profiler

**R7RS base-library completeness** — all missing procedures and special forms added:

- `let-values` / `let*-values` — destructuring bind over multiple return values
- `case =>` clause — apply a procedure to the matched key value
- `make-list k [fill]`
- `string-copy`, `string->list`, `vector->list` — optional `start`/`end` indices
- `string-for-each proc string [string ...]`
- `string-fill! string char [start [end]]`
- `string-foldcase` / `char-foldcase`
- `write-string string [port [start [end]]]`
- `string->utf8` / `utf8->string` with optional `start`/`end`
- `vector-copy! to at from [start [end]]`
- `vector-map proc vec [vec ...]`
- `vector-for-each proc vec [vec ...]`

All 12 test suites continue to pass (100%).

**`fold-left` / `fold-right` correctness fix**:
- `fold-left` argument order corrected from SRFI-1 `(proc element acc)` to R6RS `(proc acc element)`. The two conventions agree for commutative operations like `+` but differ for `cons`, `string-append`, and any order-sensitive reduction. `(fold-left string-append "0" '("1" "2" "3"))` now yields `"0123"` (was `"3210"`)
- `fold-right` added: `fold-right` was present in the Akkadian name table (`lapātum-imittam` / 𒇲𒌋) but was never registered as a builtin — calling it silently did nothing. Now registered and working: `(fold-right cons '() '(1 2 3))` → `(1 2 3)`

**Akkadian / cuneiform vocabulary extended**:
- Full numeric tower operations — quaternion, octonion, multivector, surreal, and CAS procedures all have Standard Babylonian Akkadian synonyms and cuneiform aliases
- Language reference and Akkadian reference updated to cover the complete vocabulary

**Profiling module** (`(curry profiling)`):
- `(profiler-start [level])` — enable profiling at level 1 (call counts), 2 (+ wall-clock timing via `apply()`), or 3 (+ primitive call counts). Updates the `**eval-profiler**` Scheme binding
- `(profiler-stop)` — set level to 0; accumulated data is preserved
- `(profiler-reset)` — clear all accumulated data
- `(profiler-level)` — return current level as a fixnum
- `(profiler-report)` — return an alist `((name . (calls . ns)) ...)` sorted by call count, descending
- TCO tail-calls are counted at all levels but not timed (no exit point on the `goto tail` path); apply-path calls are timed at level ≥ 2
- Instrumentation is always compiled into the core binary; when profiling is off, the hot-path check is a single integer compare with branch predictor predicting not-taken — effectively zero overhead
- `examples/profiling_mcp.scm` — MCP server wrapping the profiler as Claude Code tools

**Examples**:
- `examples/quantum_scenarios.scm` — three practical applications of the quantum superposition type: epistemic uncertainty modelled as a quantum value, arithmetic lifted over branches without collapsing, `(observe)` / `(quantum-states)` used for decision-making and distribution analysis

---

### 0.7.6 — Qt6 interactivity, Mandelbrot fixes, Neo4j documentation

**Qt6 module — new input events**:
- `(canvas-on-scroll! canvas proc)` — scroll wheel and trackpad two-finger scroll; callback receives `(dx dy x y mods)`. `dy > 0` = scroll up / zoom in. Pixel delta is used when available (trackpad), angle delta (wheel mouse) otherwise
- Mouse double-click now delivered as `'double-press` event type through the existing `canvas-on-mouse!` callback
- `(run-event-loop)` now calls `::exit(0)` after the Qt event loop returns, preventing a hang during Metal/OpenGL surface teardown on macOS

**Mandelbrot example — five correctness fixes**:
- **Concurrent read/write race**: introduced `*display-buf*` double-buffer; workers write to `*frame-buf*`, the coordinator atomically swaps to `*display-buf*` on completion — the paint thread never reads a buffer that's being written
- **Stale coordinator race**: each render captures its `*render-tag*` at spawn time; the coordinator only swaps the display buffer and clears `*rendering*` if the tag is still current, preventing a superseded render from clobbering in-progress state
- **Dead resize detection**: moved the canvas-resize check from `draw-frame` (where `*W*`/`*H*` were already updated) into the draw callback, using a `resized?` flag captured before the update
- **`timer-stop!` nil-guard**: `render-tick!` now guards `(when *render-timer* ...)` consistently
- **Scroll wheel and double-click zoom**: both now use the new Qt6 module events; zoom is cursor-centred

**Neo4j module**:
- `(curry neo4j)` is now documented: full reference at `docs/module-neo4j.md`, entry added to the module table
- The module was already fully implemented (Bolt 4.x/5.x, PackStream, transactions); this release makes it discoverable

### 0.7.5 — CAS expansion: transcendentals and polynomial operations

**Phase 1 — 12 new transcendental functions** (symbolic diff, integrate, Wirtinger, infix/LaTeX output, numeric evaluation):
- Hyperbolic: `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`
- Inverse trig: `asin`, `acos`, `atan`
- Reciprocal trig: `cot`, `sec`, `csc`
- All carry full chain-rule differentiation, linear-argument integration (IBP for inverse trig), and holomorphic Wirtinger rules
- Complex numeric evaluation via logarithmic identities: `asin(z) = -i·ln(iz+√(1-z²))`, etc.

**Phase 2 — 4 new polynomial/structural operations**:
- `(expand expr)` — distribute `*` over `+`; expand integer powers 2..16
- `(degree expr var)` — polynomial degree in a variable (exact fixnum)
- `(leading-coeff expr var)` — coefficient of the highest-degree term
- `(collect expr var)` — combine like-degree terms; canonical descending form

**Bug fixes**:
- `num_sub` was missing the complex-number branch (pre-existing); mixed real/complex subtraction now works correctly
- `asin`/`acos`/`atan` were generated via a macro that omitted the symbolic dispatch check; applying them to symbolic variables no longer crashes

**Documentation**:
- Build and installation instructions extracted from `README.md` into `docs/INSTALL.md`

### 0.7.3.1 — ODE solver module

- Added `(curry ode)`: pure Scheme ODE solvers for initial-value problems `dy/dt = f(t, y)`
- **Euler** — first-order, fixed step
- **RK4** — classical fourth-order Runge-Kutta, fixed step; exact for polynomials of degree ≤ 4
- **RK45** — Dormand-Prince adaptive step (the algorithm behind MATLAB's `ode45` and SciPy's `RK45`); step size controlled automatically to meet a tolerance
- **Verlet** — velocity-Verlet symplectic integrator for Hamiltonian systems; conserves energy over long integrations where RK methods drift
- All methods accept scalar `y` (single ODE) or list `y` (system of ODEs)
- Works with the full numeric tower: exact rationals, complex numbers, and symbolic expressions
- All methods have `/steps` variants returning `((t . y) ...)` snapshots at every accepted step
- 30 tests covering all four methods against closed-form solutions

### 0.7.3 — MQTT client module

- Added `(curry mqtt)` module: full MQTT client using the Eclipse Paho C synchronous API (`libpaho-mqtt3cs`)
- Plain TCP and TLS connections: `mqtt-connect`, `mqtt-connect-tls`
- Publish (`mqtt-publish`) with QoS 0/1/2 and optional retain flag
- Subscribe / unsubscribe with per-topic QoS: `mqtt-subscribe`, `mqtt-unsubscribe`
- Blocking receive with timeout: `mqtt-receive` returns `(topic . payload)` or `#f`
- Incoming messages delivered via a native ring-buffer queue (mutex + condvar) — Paho callback thread never touches the Scheme/GC heap
- Test harness (`tests/test_mqtt.sh`) spins up an ephemeral Mosquitto broker (plain + TLS with a fresh self-signed cert); 14 tests cover pub/sub ordering, QoS 1, wildcard subscriptions, timeout, and TLS
- Redis TLS (`redis-connect-tls`) and Redis tests (40 tests via `tests/test_redis.sh`) added in this cycle

### 0.7.2 — MCP server module and packaging

- **Homebrew formula** (`Formula/curry.rb`) — install on macOS via `brew tap deconstructo/curry && brew install curry`; pre-builds sqlite, crypto, ldap, storage, image, and git modules automatically
- **Debian package** — `cpack -G DEB` produces `curry-scheme_0.7.2_<arch>.deb`; installs to standard system paths with correct `Depends` / `Recommends`
- Added `(curry mcp)` module: expose Curry procedures as [Model Context Protocol](https://modelcontextprotocol.io/) tools callable from Claude Code and other AI clients
- **stdio transport** — JSON-RPC 2.0 over stdin/stdout; one client per process, spawned by the MCP client (`mcp-serve`)
- **SSE transport** — persistent HTTP + Server-Sent Events server; multiple concurrent clients on one port (`mcp-serve-sse`)
- Progress notifications (`mcp-notify-progress`) for long-running tool calls
- Example servers: `mcp_server.scm` (eval, factorial, stateful define, progress demo), `mcp_math.scm` (CAS: differentiation, simplification, auto-diff, Taylor series), `mcp_nbody.scm` (N-body gravity in D dimensions)

### 0.1.7 — Matrix, tensor, and gravity simulator

- First-class `Matrix` and `Tensor` types with arithmetic, map, fold, and slicing
- `(curry math matrix)` and `(curry math tensor)` loadable Scheme modules
- `(curry gravity)` — continuous-dimension physics simulator: gravity and electromagnetism in non-integer spatial dimension D
- `syntax-rules` macro expander implemented; `parameterize` / `dynamic-wind` interaction fixed
- Qt6 module hardened against Scheme exceptions escaping across C++ stack frames

### 0.1.5 — Dynamic-wind, macOS support, new modules

- `dynamic-wind` implemented; `with-mutex` deadlock fixed; port finalizer added
- Full macOS build support (Apple Silicon and x86_64); `sem_init` portability fix
- `plplot`, `regex`, and `sync` modules added
- `trace` / `untrace` for tracing calls to global procedures
- Memory safety: use-after-free bugs, leaks, and symbol table data race fixed
- `tesseract.scm` demo with anaglyph stereoscopic 3D support

### 0.1.0 — Initial release

- R7RS Scheme interpreter with tree-walking evaluator and proper tail-call optimisation via `goto tail`
- Numeric tower: fixnum → bignum (GMP) → rational → flonum → complex → quaternion → octonion → multivector (Clifford Cl(p,q,r)) → surreal (Hahn series) → symbolic CAS
- Actor-model concurrency via pthreads: `spawn`, `send!`, `receive`
- Standard Babylonian Akkadian error messages with cuneiform preambles (𒀭 ḫiṭītu)
- Akkadian/cuneiform synonym evaluation — Curry source code can be written in Standard Babylonian Akkadian
- Modules: `json`, `network`, `redis`, `sqlite`, `crypto`, `ldap`, `storage`, `graphql`, `image`, `git`, `qt6`, `vecdb`
