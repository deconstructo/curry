# Writing a pure-Scheme `(curry X)` module

Every pure-Scheme module under `lib/curry/modules/curry/` should be wrapped in
an R7RS `define-library` form, not left as a bare top-level script. This page
covers the mechanics and the gotchas that aren't obvious from the R7RS spec
itself, learned by converting the whole `(curry X)` module set to this form.

## The shape

```scheme
(define-library (curry my-module)
  (import (scheme base))
  (import (curry some-dependency))   ; one clause per imported library
  (export
    public-name-1 public-name-2
    ;; ...)
  (begin

    (define (public-name-1 ...) ...)
    (define (%private-helper ...) ...)   ; not in the export list — stays hidden
    ...

  )) ;; end begin, define-library
```

Match the library name to the file's own `(curry ...)` import path: a file at
`lib/curry/modules/curry/foo.scm` is `(define-library (curry foo) ...)`; one
at `lib/curry/modules/curry/foo/bar.scm` is `(define-library (curry foo bar)
...)`.

## Why explicit imports are mandatory, not decorative

A `define-library` body runs in a **fresh environment with no parent** — see
`env_new_root()` in `src/env.c` and `modules_define_library()` in
`src/modules.c`. Nothing is visible inside the `(begin ...)` body except what
the library's own `(import ...)` clauses bring in — not even core builtins
like `car`, `+`, `error`, or `display`. A bare top-level script (no
`define-library` wrapper) doesn't have this restriction, which is exactly why
scripts can silently work without an import that a converted library then
needs added explicitly.

The good news: `(scheme base)`, `(scheme write)`, `(scheme inexact)`, and
friends all alias the **same flat `GLOBAL_ENV`** in curry (see
`modules.c`, "Register built-in `(scheme base)`"). Importing `(scheme base)`
alone is normally enough to reach the *entire* core builtin surface —
arithmetic, bytevectors, hash tables, records, ports, `guard`, and even the
actor primitives (`spawn`, `send!`, `receive`, `self`). You essentially never
need `(scheme write)` or `(scheme inexact)` as separate imports in practice;
`(scheme base)` covers them. Import a specific `(curry X)` library only when
you use bindings that library itself defines (not core builtins it happens to
be documented near).

## Building the export list

Only names in `(export ...)` are visible to importers. Everything else in the
`(begin ...)` body is effectively private, even without needing a `%` prefix
— though keeping the `%`-prefix convention for genuinely internal helpers
still helps readers.

To build the list:

1. Start from the module's own header-comment API listing (most of these
   files already document their public procedures at the top) and its
   `docs/reference/module-X.md` page, if one exists. Treat the doc page as
   authoritative when it exists — it usually reflects additions that predate
   or postdate the header comment.
2. Export every top-level `define`, `define-syntax`, or `define-record-type`
   accessor/predicate/constructor that's part of that documented surface.
3. Leave out helpers that only exist to support the public surface — dispatch
   tables, `%`-prefixed procedures, internal record types, scratch constants.
4. **Check the module's own test file for direct calls to anything not yet
   on your list.** A test importing `(curry X)` and then calling a bare
   helper name is a live signal that name is part of the module's informal
   public surface (e.g. `(curry random)` exports `iota` even though it's not
   in the module's own header doc, because its own docs page and tests use
   it directly after importing the module).

## The macro-expansion gotcha

This is the one that isn't obvious and silently breaks at the *use site*
rather than at definition time: curry's `syntax-rules` expansion is **not
hygienic across `define-library` boundaries**. If an exported macro's
expansion refers to a helper procedure, another macro, a record predicate, or
a built-in class value, that identifier is looked up in the *importer's*
environment when the expanded code runs — not in the defining library's
environment. If that helper isn't also exported, the importer gets an
`unbound-variable` error the first time they actually *use* the macro, even
though `(import (curry X))` itself succeeded cleanly.

This does **not** apply to ordinary procedures — a procedure's body always
sees its own defining lexical scope when it runs, regardless of who calls it.
It only bites macros, because a macro's expansion is spliced as literal code
into the caller's context.

Concretely, trace every `define-syntax` in the file and export everything its
expansion could reach:

- `(curry conditions)`: `handler-bind`'s expansion calls `%handler-bind-nest`
  — exported for exactly this reason, even though it looks purely internal.
- `(curry oop)`: `define-class`/`define-generic`/`define-method` expand
  through `%build-class`, `%parse-slot`, `%parse-slot-opts`, `%make-slot`,
  `%gen-accessors`, `%gen-accessors-for-spec`, `%make-generic`,
  `%ensure-generic!`, `%add-method!` — all exported. The built-in class
  values (`<object>`, `<number>`, `<string>`, ...) are also exported, because
  user code names them *directly* as `define-method`/`is-a?` specializers —
  that's ordinary user-written code at the use site, not macro-generated
  code, but it still needs those bindings visible.
- `(curry stm)`: `select`'s expansion calls the helper macros
  `%select-try-clause` and `%select-fallback` — both exported.

If you add a new macro to one of these modules, trace its expansion the same
way before deciding what's "just an internal helper."

## Sanity-check before moving on

1. `./build/curry -e '(import (curry my-module)) (display "ok")'` — catches
   missing imports.
2. Exercise every exported macro at least once, not just every exported
   procedure — plain unbound-import failures show up on `import`, but a
   missing macro-expansion export only shows up when that specific macro
   form is actually used.
3. Run the module's own test file (`tests/*_tests.scm`) if one exists —
   `ctest --test-dir build -R <name>` or `./build/curry tests/X_tests.scm`
   directly.

## See also

- `CLAUDE.md`'s "Module system" section for the C-module and `.sld`/`.scm`
  loading mechanics this builds on.
- [`modules.md`](modules.md) — the full module index.
