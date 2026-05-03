# Curry Scheme — Implementation Spec
## Task 1: `syntax-rules` · Task 2: `parameterize` + `dynamic-wind` correctness

---

## Context

Read `CLAUDE.md` in full before starting. Key architectural facts relevant here:

- Every Scheme value is a tagged `val_t` (`src/value.h`).
- Heap objects begin with `Hdr { uint32_t type; uint32_t flags; }`.
- `T_SYNTAX` (`src/object.h:51`) wraps a `transformer` field of type `val_t`. When the evaluator sees a `T_SYNTAX` in operator position it calls:
  ```c
  val_t transformed = apply(as_syntax(op_val)->transformer, make_pair(expr, V_NIL));
  ```
  then evaluates `transformed` in the same environment. The transformer receives the entire unevaluated form as its sole argument.
- `define-syntax`, `let-syntax`, `letrec-syntax` already evaluate their transformer argument and store it in a `Syntax` struct. No changes needed there.
- `WindFrame` is a stack-allocated struct (`src/eval.h`). `current_wind` (thread-local) is the current top of the dynamic-wind chain. `wind_unwind_to(target)` walks the chain calling each `after` thunk until `current_wind == target`.
- Escape continuations capture `current_wind` at the point of `call/cc`. On invocation, `wind_unwind_to(cont->wind_top)` runs before `longjmp`. This is already correct for upward escapes.
- `parameterize` currently does manual `setjmp`/save/restore rather than registering a `WindFrame`. This means escape continuations that cross a `parameterize` boundary do not trigger parameter restoration.

---

## Task 1 — Implement `syntax-rules`

### What to implement

`(syntax-rules (literal ...) (pattern template) ...)` must return a procedure suitable for use as a `T_SYNTAX` transformer. When called with a form, it matches the form against each pattern in order, binds pattern variables, and returns the expanded template with substitutions applied. This is the behaviour specified in R7RS §7.1.3.

### Implementation location

Add a new source file `src/syntax_rules.c` with header `src/syntax_rules.h`. Register the single builtin `syntax-rules` in `builtins_register()` in `src/builtins.c`.

Do **not** modify the `T_SYNTAX` dispatch in `eval.c`. The transformer returned by `syntax-rules` is a `T_PRIMITIVE` (or `T_CLOSURE`) that the existing dispatch already knows how to call.

### Pattern matching rules

Patterns are matched against the input form. The first element of a top-level pattern is always the keyword (the macro name); skip it when matching — match the rest of the pattern against the rest of the form.

| Pattern element | Matches |
|-----------------|---------|
| `_` | anything |
| symbol in literals list | that exact symbol (by `sym_intern` identity) |
| any other symbol | anything; binds that name to the matched subform |
| `(p ...)` where `...` is the literal ellipsis symbol | zero or more subforms matching `p`; binds all pattern variables in `p` to lists |
| `(p1 p2 ... pN)` (no ellipsis) | a list of exactly N elements, matched positionally |
| `(p1 ... pN-1 pN ...)` | N-1 required elements then zero or more matching `pN` |
| datum (number, string, boolean, char) | equal datum |

Ellipsis (`...`) is the symbol with print name `"..."`. Detect it by interned pointer identity.

If no pattern matches, raise an error: `scm_raise(V_FALSE, "syntax-rules: no matching pattern for %s", sym_cstr(keyword))`.

### Template expansion rules

After a successful match, expand the template:

| Template element | Expands to |
|------------------|------------|
| pattern variable | bound subform |
| pattern variable followed by `...` | splice the bound list |
| `(t1 t2 ...)` where any `t_i` contains a pattern variable bound to a list + has `...` | replicate the subtemplate once per element of the list, splice results |
| any other symbol | that symbol (introduced identifier — see hygiene note below) |
| datum | itself |

### Hygiene

Implement **basic hygiene only**: pattern variables are substituted by their matched subforms, which are syntactic objects from the use site. Introduced identifiers (symbols in the template that are not pattern variables) resolve in the definition environment, not the use environment.

To achieve this without a full marks-and-substitutions system: capture the definition environment (`env` at the time `syntax-rules` is evaluated) in the returned transformer closure. When expanding a template symbol that is not a pattern variable, emit a `(the-environment-symbol . captured-env)` pair — or more practically, wrap introduced identifiers in a new `T_RENAMED_SYM` type. 

However: full hygiene is expensive to implement and test. **If the full approach would require a new object type or significant eval.c changes, implement unhygienic expansion for now and document it clearly.** The test suite (see below) will mark hygiene-sensitive tests explicitly; skip them if hygiene is not implemented. The priority is correct pattern matching and template expansion for the common case.

### Data structures

Represent a compiled `syntax-rules` transformer as a C struct containing:

```c
typedef struct {
    Hdr      hdr;          /* T_SYNTAX_RULES or reuse T_PRIMITIVE with ud */
    val_t    keyword;      /* interned symbol — the macro name */
    val_t    literals;     /* Scheme list of literal symbols */
    uint32_t nrules;
    SrRule   rules[];      /* flexible array */
} SyntaxRules;

typedef struct {
    val_t pattern;         /* full pattern form (including keyword position) */
    val_t tmpl;            /* template form */
} SrRule;
```

Alternatively, package the compiled transformer as a `T_PRIMITIVE` with `ud` pointing to a `SyntaxRules` struct — this avoids adding a new object type and works with the existing `T_SYNTAX` dispatch because the transformer is just called via `apply`.

### Scheme-level API

```scheme
;; These must work after implementation:

(define-syntax my-and
  (syntax-rules ()
    ((_)         #t)
    ((_ e)       e)
    ((_ e1 e2 ...) (if e1 (my-and e2 ...) #f))))

(define-syntax swap!
  (syntax-rules ()
    ((_ a b)
     (let ((tmp a))
       (set! a b)
       (set! b tmp)))))

(define-syntax my-or
  (syntax-rules ()
    ((_) #f)
    ((_ e) e)
    ((_ e1 e2 ...)
     (let ((t e1))
       (if t t (my-or e2 ...))))))
```

### Test file

Create `tests/syntax_rules_tests.scm`. Structure it the same way as `tests/r7rs_tests.scm` (using the same `check` harness). Cover:

- Zero-clause `syntax-rules` raises on use
- Single non-variadic pattern
- Multiple patterns with fallthrough
- Ellipsis in pattern and template (zero matches, one match, many matches)
- Nested ellipsis is out of scope; do not test it
- Literal matching (a symbol in the literals list must match exactly, not bind)
- `_` wildcard
- Recursive macro (e.g. `my-and` above)
- `let` defined via `syntax-rules` (classic test of let-as-macro)
- Mark any hygiene-sensitive tests with `;;HYGIENE` and skip them with `(if #f ...)` if hygiene is not implemented

Add the new test suite to `CMakeLists.txt` alongside `actors_tests.scm` and `numeric_ext_tests.scm`.

### Akkadian names

Add to `src/akkadian_names.h`:

```c
AKK_PR("syntax-rules", "ṭupšarrūtum ṣibātum", "𒌝𒌋𒍣")   /* template-pattern */
```

(Choose an appropriate cuneiform rendering consistent with the existing table conventions.)

---

## Task 2 — Fix `parameterize` + `dynamic-wind` interaction

### The problem

`parameterize` in `src/eval.c` saves parameter values, evaluates the body under a `setjmp`, then restores values in a `finally`-style block. This restoration does not run if an escape continuation jumps out of the `parameterize` region, because the `longjmp` in `apply()` for `T_CONTINUATION` bypasses the restore code entirely.

`wind_unwind_to` already handles this correctly for `dynamic-wind` frames — it calls `after` thunks on the way out. The fix is to make `parameterize` register a `WindFrame` instead of doing manual save/restore.

### What to change

**In `src/eval.c`, the `S_PARAMETERIZE` branch:**

Replace the current implementation with one that:

1. Evaluates all `(param val)` pairs, collecting `(parameter-object . new-value)` pairs. Apply converters where present (`as_param(p)->converter`).
2. Allocates a `WindFrame` on the heap (not the stack — `WindFrame` must survive past the `setjmp` boundary). Use `gc_alloc` or declare it as a GC-managed struct. **This is the critical change**: stack-allocated `WindFrame`s are invalidated by `longjmp` if the frame is below the `setjmp` in the call stack.
3. Sets the `before` thunk to a closure that sets each parameter to its new value.
4. Sets the `after` thunk to a closure that restores each parameter to its saved old value.
5. Calls the `before` thunk immediately.
6. Pushes the `WindFrame` onto `current_wind`.
7. Evaluates the body (in tail position where possible).
8. Pops the `WindFrame` (`current_wind = wf->prev`).
9. Calls the `after` thunk.
10. Returns the body's result.

Steps 6–9 must be exception-safe: wrap with `SCM_PROTECT` or a local `setjmp` that pops the frame and calls `after` before re-raising.

Because closures capturing `val_t` arrays are needed for the `before`/`after` thunks, the simplest approach is to write two small `PrimFn` callbacks that take a `ud` pointer to a heap-allocated struct containing the arrays:

```c
typedef struct {
    int     n;
    val_t  *params;   /* GC-managed array of parameter objects */
    val_t  *newvals;  /* GC-managed array of new values */
    val_t  *oldvals;  /* GC-managed array of saved old values */
} ParamBindings;
```

**In `src/eval.h`**, if `WindFrame` is currently declared without GC management, add a note that `parameterize` allocates `WindFrame`s on the GC heap; nothing else needs changing since `WindFrame` contains only `val_t` and a pointer to the previous frame.

### Also fix: `WindFrame` stack allocation in `prim_dynamic_wind`

`prim_dynamic_wind` in `src/builtins.c` also stack-allocates its `WindFrame`. For the same reason — `longjmp` can invalidate it — move this to `gc_alloc` as well.

```c
// Before:
WindFrame wf;
wf.before = before; wf.after = after; wf.prev = current_wind;
current_wind = &wf;

// After:
WindFrame *wf = gc_alloc(sizeof(WindFrame));
wf->before = before; wf->after = after; wf->prev = current_wind;
current_wind = wf;
// ... and use wf->prev to pop
```

### Remove the TODO comment

Remove the `/* TODO: full dynamic-wind integration */` comment from `src/eval.c:698` once the fix is in place. Replace it with a brief comment explaining the WindFrame approach.

### Test cases

Add to `tests/r7rs_tests.scm` (or a new `tests/dynamic_wind_tests.scm` if it grows large):

```scheme
;; parameterize restores on normal exit
(define p (make-parameter 1))
(parameterize ((p 2)) 'ignored)
(check "parameterize restore normal" (p) 1)

;; parameterize restores via escape continuation
(define q (make-parameter 10))
(call/cc
  (lambda (k)
    (parameterize ((q 99))
      (k 'escaped))))
(check "parameterize restore via escape" (q) 10)

;; nested parameterize
(define r (make-parameter 'a))
(parameterize ((r 'b))
  (parameterize ((r 'c))
    (check "nested parameterize inner" (r) 'c))
  (check "nested parameterize outer" (r) 'b))
(check "nested parameterize restored" (r) 'a)

;; dynamic-wind after runs on escape
(define log '())
(call/cc
  (lambda (k)
    (dynamic-wind
      (lambda () (set! log (cons 'in log)))
      (lambda () (k 'done))
      (lambda () (set! log (cons 'out log))))))
(check "dynamic-wind after on escape" log '(out in))

;; dynamic-wind + parameterize interaction
(define s (make-parameter 0))
(define dw-log '())
(call/cc
  (lambda (k)
    (dynamic-wind
      (lambda () (set! dw-log (cons 'wind-in dw-log)))
      (lambda ()
        (parameterize ((s 42))
          (k 'out)))
      (lambda () (set! dw-log (cons 'wind-out dw-log))))))
(check "param restored after dw escape" (s) 0)
(check "dw after ran on escape" (memv 'wind-out dw-log) '(wind-out wind-in))
```

---

## Order of implementation

1. Task 2 first (smaller, self-contained, unblocks correct test infrastructure).
2. Task 1 second.
3. Run the full existing test suite (`ctest`) after each task; no regressions permitted.

---

## Out of scope

- Full reentrant (multi-shot, downward) `call/cc`. Do not attempt this.
- Full hygienic macro expansion beyond what is noted above.
- `syntax-case`.
- SRFI implementations.
- Unicode-correct `string-ref`/`substring` (separate TODO).
- Actor link/monitor registry (separate TODO).
