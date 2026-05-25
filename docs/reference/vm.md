# Curry Bytecode VM

Curry's evaluation path is a **stack-based bytecode virtual machine** that
compiles Scheme ASTs to `Chunk` bytecode objects and executes them in a tight
dispatch loop.  It replaces the original tree-walking interpreter for all
code entered at the REPL, loaded from files, or passed via `-e`.

---

## Architecture overview

```
Reader  →  AST (val_t cons tree)
              │
        compiler_compile()
              │
         Chunk (bytecode + constant pool)
              │
          vm_run()
              │
           result (val_t)
```

**Three key structures** carry the runtime state:

| Structure   | Role |
|-------------|------|
| `Chunk`     | Compiled unit: byte stream, constant pool, line table, arity |
| `BcClosure` | Runtime closure: `Chunk*` + captured `Upvalue*[]` array |
| `CallFrame` | Activation record: `BcClosure*`, instruction pointer `ip`, `slots` pointer into the value stack, slot count |

The global `VM *vm` holds the flat value stack (`val_t stack[VM_STACK_MAX]`),
the frame stack (`CallFrame frames[VM_FRAMES_MAX]`), and a linked list of
open upvalues.

Limits: stack depth 4096 values, call depth 256 frames.

---

## Value stack

The VM uses a single flat array of `val_t`.  `vm->sp` points one past the
top.  Each `CallFrame` records a `slots` pointer into this array; all local
variable accesses (`OP_LOAD_LOCAL A`, `OP_STORE_LOCAL A`) are relative to
`frame->slots`.

```
stack: [ ... | callee | arg0 | arg1 | local2 | local3 | ... ]
                        ^
                   frame->slots
```

The callee sits at `slots[-1]`.  On `OP_RETURN` the callee and all args/locals
are discarded and the return value is pushed in their place.

---

## Calling convention

Before `OP_CALL N` or `OP_TAIL_CALL N`:

```
stack top →  argN-1
             ...
             arg0
             callee        ← slots[-1] of the new frame
             ...
```

For **`BcClosure` callees** (`OP_CALL`), a new `CallFrame` is pushed.
`frame->slots` is set to `sp - argc`, so the args are already in the right
place as locals 0…N-1.

For **non-BcClosure callees** (primitives, tree-walker closures, continuations)
the VM calls `apply_arr()` from `eval.c`, pushes the result, and continues.
This keeps both execution engines interoperable during the migration.

---

## Tail-call optimisation

`OP_TAIL_CALL N` reuses the *current* frame instead of pushing a new one:

1. `vm_close_upvalues` is called for the current slots (closes any captured
   locals before they are overwritten).
2. The N arguments are `memmove`'d over `frame->slots[0..N-1]`.
3. `frame->closure` and `frame->ip` are updated to the new closure and its
   code start; `sp` is reset to `slots + N`.
4. Execution continues in the same `CallFrame`.

For non-BcClosure callees, `apply_arr()` is invoked and the frame is
immediately popped (making it equivalent to a non-tail call followed by
`OP_RETURN`).

---

## Upvalues

The upvalue protocol is identical to Lua 5's:

- **Open**: `Upvalue.location` points into the live stack.  Any number of
  closures can share the same `Upvalue` object for the same slot.
- **Closed**: when a scope exits, `vm_close_upvalues(addr)` copies the
  stack value into `Upvalue.closed` and redirects `location` to
  `&upvalue->closed`.  The heap-allocated value outlives the frame.

`OP_CLOSE_UP A` closes the upvalue for `frame->slots[A]` without popping
the slot.  Scope cleanup emits one `OP_CLOSE_UP` per captured local (in
reverse order), then one `OP_SLIDE N` to drop the N locals while keeping
the scope's result on top.

---

## The compiler

### Entry points

```c
val_t compiler_compile(val_t expr);          // single expression → BcClosure
val_t compiler_compile_script(val_t forms);  // list of top-level forms → BcClosure
```

Both return a zero-argument `BcClosure`.  `vm_run(as_bcclosure(cl), 0)`
executes it.

### Compiler struct

Each lambda (or synthetic wrapper) gets its own `Compiler` on the C stack,
linked to its enclosing `Compiler` via `->enclosing`.

```c
typedef struct Compiler {
    struct Compiler *enclosing;
    Chunk     *chunk;        // bytecode being built
    Local      locals[256];  // name, depth, captured flag
    int        local_count;
    int        scope_depth;
    UpvalDesc  upvals[256];  // is_local flag + index in enclosing
    int        upval_count;
} Compiler;
```

### Variable resolution

At compile time, every name reference is resolved through three levels:

1. **Local** — linear scan of `c->locals[]` from innermost outward.
   Emits `OP_LOAD_LOCAL A` / `OP_STORE_LOCAL A`.
2. **Upvalue** — recursive walk up the `->enclosing` chain.  The
   first enclosing level where the name is a local contributes
   `is_local=1`; each further level re-captures with `is_local=0`.
   Emits `OP_LOAD_UP A` / `OP_STORE_UP A`.
3. **Global** — falls through to `GLOBAL_ENV` (the same environment
   the tree-walker uses, keeping one shared namespace).
   Emits `OP_LOAD_GLOBAL A` / `OP_STORE_GLOBAL A` where `A` indexes a
   symbol in the constant pool.

### Internal defines

Lambda bodies are pre-scanned for internal `(define …)` forms before
compilation begins.  Each name is pre-declared as a local with a sentinel
depth of `-1` (uninitialised).  This gives **letrec\*** semantics: later
definitions can reference earlier ones, but use before initialisation raises
an error.

### Lambda compilation

`compile_lambda(parent, params, body, name, line)`:

1. Creates a child `Compiler` linked to `parent`.
2. Calls `compile_params` to declare each parameter as a local (slot 0,
   1, …).  Variadic `(lambda (a b . rest) …)` sets `arity = -(N+1)`;
   the VM collects excess args into a list at `slots[N]`.
3. Scans for internal defines (pre-declares them).
4. Calls `compile_seq(body, tail=true)` — the body's last expression is
   in tail position.
5. `end_compiler` emits `OP_RETURN` and records `upval_count`.
6. Back in the parent: emits `OP_CLOSURE A` (A = constant-pool index of
   the child `Chunk*`) followed by `2 × upval_count` bytes of capture
   descriptors `[is_local, index]`.

### Special forms

| Form | Compilation strategy |
|------|----------------------|
| `quote` | `emit_const` of the literal value |
| `quasiquote` | `expand_qq()` in `eval.c` expands the template to list-construction code; result is compiled normally |
| `if` | compile test → `JUMP_FALSE` → then → `JUMP` → else |
| `begin` | `compile_seq` (sequence, last in tail position) |
| `define` | compile value → `DEF_GLOBAL` (top level) or `STORE_LOCAL` (internal) → push void |
| `set!` | compile value → `STORE_LOCAL/UP/GLOBAL` → push void |
| `lambda` | `compile_lambda` |
| `and` | short-circuit via `JUMP_FALSE`; all-true path falls through with last value |
| `or` | `DUP` + `JUMP_TRUE` to skip rest; false path pops and continues |
| `let` | `((lambda (params…) body) inits…)` — isolated frame, parallel binding |
| `let*` | nested single-binding lambdas: `((lambda (x) (let* rest body)) v)` |
| `letrec` / `letrec*` | zero-arg lambda wrapper; void placeholders then STORE |
| `named let` | zero-arg outer wrapper; slot 0 = loop closure; inner lambda captures it |
| `cond` | per-clause `JUMP_FALSE`; `=>` uses `DUP`+`JUMP_FALSE`+`SWAP`+call |
| `when` | `JUMP_FALSE` → body / void |
| `unless` | `JUMP_TRUE` → body / void |
| `do` | zero-arg lambda wrapper; step values computed before assignment; `JUMP_TRUE` exits |
| `values` | `OP_VALUES N` — bundles N values into a `T_VALUES` object |
| `apply` | `OP_APPLY N` |
| `parameterize` | `compile_parameterize()` desugars at compile time to `let + dynamic-wind` so body locals are captured as upvalues, not looked up in `GLOBAL_ENV` |

Akkadian/cuneiform synonyms are translated via `akk_translate()` before
dispatch, so Akkadian source compiles identically to its English equivalent.

### Scope-isolation pattern

All scope-forming constructs (`let`, `let*`, `letrec`, `do`, named `let`)
are compiled as **lambda calls** rather than inlined scopes.  This is
necessary because when any of these forms appears as an argument to another
call, the callee for that outer call is already pushed at a low stack slot
before the inner form runs.  If the inner form declared locals starting at
slot 0 in the *parent* frame, slot indices would collide.

Wrapping in a lambda guarantees a fresh `CallFrame` where slot 0 always
corresponds to the first local of the form.

### Jump patching

Jumps use **absolute byte offsets** within the `Chunk`.  `emit_jump` emits
the opcode and a `0xFFFF` placeholder, returning the placeholder's offset.
`patch_jump(c, placeholder)` fills in the current code position.
`OP_JUMP` (unconditional) is used for end-of-branch skips; both `JUMP_FALSE`
and `JUMP_TRUE` **always pop** their condition regardless of whether the
branch is taken.

---

## Opcode reference

Operand notation: **A** = 1-byte immediate, **B** = 2-byte little-endian
immediate (absolute offset or wide index).

### Constants and literals

| Opcode | Operand | Stack effect | Notes |
|--------|---------|--------------|-------|
| `OP_CONST` | A | → v | push `constants[A]` |
| `OP_CONST_W` | B | → v | push `constants[B]` (wide, > 255 constants) |
| `OP_TRUE` | – | → #t | |
| `OP_FALSE` | – | → #f | |
| `OP_NIL` | – | → '() | |
| `OP_VOID` | – | → void | |

### Local variables

| Opcode | Operand | Stack effect | Notes |
|--------|---------|--------------|-------|
| `OP_LOAD_LOCAL` | A | → v | push `frame->slots[A]` |
| `OP_STORE_LOCAL` | A | v → | `slots[A] = pop()`; no push |

`define` and `set!` follow `STORE_LOCAL` with `OP_VOID` to leave void on
the stack as their result.

### Global variables

| Opcode | Operand | Stack effect | Notes |
|--------|---------|--------------|-------|
| `OP_LOAD_GLOBAL` | A | → v | `constants[A]` is a symbol; looks up `GLOBAL_ENV` |
| `OP_STORE_GLOBAL` | A | v → | `set!` on existing binding; error if unbound |
| `OP_DEF_GLOBAL` | A | v → | `define`; creates binding |

### Upvalues

| Opcode | Operand | Stack effect | Notes |
|--------|---------|--------------|-------|
| `OP_LOAD_UP` | A | → v | dereference `closure->upvals[A]->location` |
| `OP_STORE_UP` | A | v → | `*upvals[A]->location = pop()` |

### Stack manipulation

| Opcode | Operand | Stack effect | Notes |
|--------|---------|--------------|-------|
| `OP_POP` | – | v → | discard TOS |
| `OP_DUP` | – | v → v v | duplicate TOS |
| `OP_SWAP` | – | a b → b a | exchange top two |
| `OP_SLIDE` | A | … v → v | move TOS past A items below it (scope cleanup) |
| `OP_NOP` | – | – | no-op |

`OP_SLIDE N` is used by `end_scope`: after closing captured locals, it drops
N local slots while preserving the scope's result value.

### Arithmetic

All arithmetic goes through the full numeric tower (`num_add`, `num_sub`,
etc.) and promotes automatically: fixnum → bignum → rational → flonum →
complex → quaternion → … → symbolic.

| Opcode | Stack effect |
|--------|--------------|
| `OP_ADD` | a b → (+ a b) |
| `OP_SUB` | a b → (- a b) |
| `OP_MUL` | a b → (* a b) |
| `OP_DIV` | a b → (/ a b) |
| `OP_NEG` | a → (- a) |
| `OP_ABS` | a → (abs a) |
| `OP_EXPT` | a b → (expt a b) |

### Comparison

| Opcode | Result |
|--------|--------|
| `OP_EQ` / `OP_NUMEQ` | `(= a b)` — numeric equality |
| `OP_LT` | `(< a b)` |
| `OP_LE` | `(<= a b)` |
| `OP_GT` | `(> a b)` |
| `OP_GE` | `(>= a b)` |
| `OP_EQV` | `(eqv? a b)` |
| `OP_EQUAL` | `(equal? a b)` — structural |
| `OP_NOT` | `(not a)` |

### Pairs and lists

| Opcode | Operation |
|--------|-----------|
| `OP_CONS` | `(cons a b)` |
| `OP_CAR` | `(car pair)` |
| `OP_CDR` | `(cdr pair)` |
| `OP_SETCAR` | `(set-car! pair val)` → void |
| `OP_SETCDR` | `(set-cdr! pair val)` → void |
| `OP_NULLP` | `(null? v)` |
| `OP_PAIRP` | `(pair? v)` |

### Strings and characters

| Opcode | Operation |
|--------|-----------|
| `OP_STRINGLEN` | `(string-length s)` |
| `OP_STRINGREF` | `(string-ref s i)` — UTF-8 aware |
| `OP_CHARTOFIX` | `(char->integer c)` |
| `OP_FIXTOCHAR` | `(integer->char n)` |

### Type predicates

`OP_NUMBERP`, `OP_STRINGP`, `OP_SYMBOLP`, `OP_CHARP`, `OP_BOOLP`,
`OP_PROCP`, `OP_VECTORP` — each pops TOS and pushes `#t` or `#f`.
`OP_PROCP` returns `#t` for both `BcClosure` and tree-walker `Closure`.

### Vectors

| Opcode | Operand | Operation |
|--------|---------|-----------|
| `OP_MAKEVEC` | A | `(make-vector len [fill])`; A = 1 or 2 |
| `OP_VECREF` | – | `(vector-ref vec idx)` |
| `OP_VECSET` | – | `(vector-set! vec idx val)` → void |
| `OP_VECLEN` | – | `(vector-length vec)` |

### Control flow

| Opcode | Operand | Notes |
|--------|---------|-------|
| `OP_JUMP` | B | unconditional absolute jump |
| `OP_JUMP_FALSE` | B | pop TOS; jump if `#f` |
| `OP_JUMP_TRUE` | B | pop TOS; jump if not `#f` |

**Important**: both conditional jump opcodes **always pop** their condition
regardless of whether the branch is taken.  Compiler code that emits a
conditional jump must not emit a separate `OP_POP` for the test value.

### Calls and returns

| Opcode | Operand | Notes |
|--------|---------|-------|
| `OP_CALL` | A | call with A args; push new `CallFrame` for `BcClosure` |
| `OP_TAIL_CALL` | A | tail call; reuses current frame for `BcClosure` |
| `OP_RETURN` | – | pop frame; result replaces callee+args window |

### Closures

| Opcode | Operand | Notes |
|--------|---------|-------|
| `OP_CLOSURE` | A | create `BcClosure` from `constants[A]` (a `Chunk*`); followed by `2 × upval_count` capture bytes |
| `OP_CLOSE_UP` | A | close open upvalue for `frame->slots[A]`; does not pop |

`OP_CLOSURE` capture bytes: pairs of `[is_local : u8, index : u8]`.
`is_local=1` captures `frame->slots[index]` of the current frame.
`is_local=0` re-captures `closure->upvals[index]` of the current closure.

### Apply and multiple values

| Opcode | Operand | Notes |
|--------|---------|-------|
| `OP_APPLY` | A | `(apply fn arg… list)`; A = total stack items including fn; flattens last list |
| `OP_VALUES` | A | bundle A values; no-op if A = 1 |
| `OP_CALL_WITH_VALUES` | – | `(call-with-values thunk consumer)` |

### Exception handling

| Opcode | Operand | Notes |
|--------|---------|-------|
| `OP_PUSH_HANDLER` | B | push handler frame; B = fallback offset (reserved) |
| `OP_POP_HANDLER` | – | pop handler frame |
| `OP_RAISE` | – | raise TOS as exception via `scm_raise` |

### I/O

| Opcode | Operation |
|--------|-----------|
| `OP_DISPLAY` | `(display v)` → void |
| `OP_WRITE` | `(write v)` → void |
| `OP_NEWLINE` | emit newline → void |

---

## Interoperability with the tree-walker

Both execution engines coexist and call each other freely:

- **VM → tree-walker**: Any non-`BcClosure` callable (tree-walker `Closure`,
  primitive, continuation) is dispatched via `apply_arr()` from `eval.c`.
  The VM pushes the result and continues.
- **Tree-walker → VM**: `apply()` and `apply_arr()` in `eval.c` detect
  `BcClosure` callees and dispatch to `vm_run()`.  This means that tree-walker
  code (used by `import`, `define-syntax`, etc. via `tree-eval`) can call
  compiled closures seamlessly.
- **Shared global environment**: Both engines read and write `GLOBAL_ENV`, so
  `define` from either side is immediately visible to the other.
- **Thread-local VM**: `vm` is `_Thread_local`; each thread (including actor
  threads) must call `vm_init()` before invoking `vm_run()`.  This ensures
  actors don't race on a shared VM state.
- **Exception safety**: `prim_call_cc` and `prim_with_exception_handler` save
  and restore `vm->frame_count`, `vm->sp`, and `vm->open_upvalues` around
  `setjmp`/`longjmp` so that a `longjmp` that skips nested `vm_run` frames
  leaves the VM in a consistent state.

### `tree-eval`

`(tree-eval expr)` forces evaluation of `expr` through the **tree-walking interpreter** rather than the bytecode VM. This is used internally by `import`, `define-syntax`, and macro expansion, where the tree-walker must remain the authoritative evaluator. It is also available as a Scheme primitive for cases where you hold a raw expression tree (e.g. produced by `read` or a macro) and want to evaluate it without going through the compiler:

```scheme
(tree-eval '(+ 1 2))                ; => 3
(tree-eval '(define x 42))          ; defines x in GLOBAL_ENV
(tree-eval (list '+ 1 2))           ; => 3  — works on any list structure
```

Ordinary code should use `eval` instead; `tree-eval` bypasses compilation and is slower for code that runs many times.

---

## Debugging

`chunk_disasm(chunk, label)` pretty-prints a chunk's bytecode to `stderr`,
showing byte offset, source line, opcode name, operand, and (for constant/
global opcodes) the constant value.

```
=== fib (42 bytes, 5 consts) ===
0000    1 CONST               0  ; 0
0002    | LOAD_LOCAL           0
0004    | EQ
0005    | JUMP_FALSE          11
0008    | CONST               0  ; 0
0010    | RETURN
0011    | LOAD_LOCAL           0
...
```

---

## Bytecode cache (.scc files)

When curry runs a `.scm` script it compiles each top-level form to a `Chunk`
and immediately executes it.  After the last form it serialises all chunks to
a `.scc` (Scheme Compiled Cache) file adjacent to the source, so subsequent
runs skip recompilation.

### Cache format

```
"CURRYBC" (7 bytes)  format-version (u8)
version-string-len (u8)  version-string (N bytes)
source-mtime (i64 LE)  source-size (i64 LE)
n_chunks (u32 LE)
chunk[0] … chunk[n_chunks-1]    -- each serialised with write_chunk()
0xCAFEBEEF (u32 LE sentinel)
```

A cache hit requires an exact match on `CURRY_VERSION`, source `mtime`, and
source byte-size.  Any mismatch falls through to recompilation.

Compiled `.scc` files can also be made directly executable (`curry -c -x`),
which prepends a `#!/usr/bin/env curry` shebang and sets the file's executable
bit.  Extension-less executables are detected by the `CURRYBC` magic bytes.

### Constant pool tags

Each constant in a `Chunk`'s pool is preceded by a one-byte tag:

| Tag | Type | Notes |
|-----|------|-------|
| 0 | Immediate | raw `val_t` (fixnum, bool, char, nil, void, eof) |
| 1 | Flonum | 8-byte IEEE 754 double |
| 2 | Bignum | hex string (GMP `mpz`) |
| 3 | Rational | `"num/den"` hex string (GMP `mpq`) |
| 4 | Complex | two recursive constants (real, imag) |
| 5 | Quaternion | four doubles |
| 6 | Octonion | eight doubles |
| 7 | String | u32 byte-length + UTF-8 bytes |
| 8 | Symbol | u32 byte-length + UTF-8 bytes; re-interned on load |
| 9 | Chunk | recursive `write_chunk` / `read_chunk` (for `OP_CLOSURE`) |
| 10 | Pair | two recursive constants (car, cdr) |
| 11 | Vector | u32 element-count + recursive elements |
| 12 | Bytevector | u32 byte-length + raw bytes |

### GC note

The `Chunk**` array returned by `scc_load` / `scc_load_direct` is allocated
with `GC_MALLOC` (not plain `malloc`) so that Boehm GC's conservative heap
scan finds the interior `Chunk*` pointers and keeps the chunk objects alive
across GC collections that may occur while the run loop is executing them.

---

## Known limitations

- **`call/cc`**: escape (upward-only) continuations work from both the VM and
  tree-walker paths via `prim_call_cc`, which uses `setjmp`/`longjmp` and
  saves/restores the VM stack state.  Full first-class (re-entrant) continuations
  are not implemented — they require a copying or CPS-transformed evaluator.
- **Jump range**: jump targets are 16-bit absolute offsets, limiting chunk
  size to 65535 bytes.  Extremely large generated functions may hit this
  limit.
- **Upvalue count**: capped at 256 per closure by `MAX_UPVALS`.
- **Local count**: capped at 256 per frame by `MAX_LOCALS`.
- **`parameterize` gensym cap**: `compile_parameterize` handles at most 32
  bindings per form (a compile-time constant `MAX_PARAMS`).  Pathological
  uses beyond that are silently truncated.
