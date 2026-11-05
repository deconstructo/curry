# syntax-case with full hygiene — design and plan

*Drafted 2026-11-06. Status: Phase 0 (design). Nothing implemented yet.*

## Why

Curry has `syntax-rules` only. Its hygiene is partial (see the header of
`src/syntax_rules.c`): a template-introduced symbol that is not already a real
reference gets a fresh per-expansion name. That closes capture of a macro's own
bindings, but not the other direction: a use-site binding of a name the macro's
template refers to freely (`list`, `if`, a helper) still shadows it.

The immediate reason for `syntax-case` is porting `../Schevana`, whose
`define-foreign` macro inspects its arguments while expanding (it chooses a
conversion per C type). `syntax-rules` cannot do that. The decision, made on
2026-11-06, is to implement **full hygiene through the compilers**, not a
datum-only `syntax-case` on top of the current heuristic.

## Where curry is now (verified 2026-11-06)

- A macro transformer is any procedure taking the whole use-site form and
  returning a datum. Procedural transformers already work in both evaluators
  (`eval.c` T_SYNTAX use-site path; `compiler_classic.c` `SF_MACRO` case).
- Identifiers are bare interned symbols. Special-form dispatch is an `==`
  ladder over `S_*` symbols (`classify_head`, `compiler_classic.c`). Counted in
  the core files: about 250 `vis_symbol` guards and `== S_xxx` comparisons
  (`eval.c` 13 + 67, `compiler_classic.c` 12 + 51, `ir_lower.c` 17 + 31,
  `compiler.c` 8 + 17, `runtime.c` 4 + 7, `syntax_rules.c` 13 + 1,
  `ir_emit.c` 4, `record_type.c` 3 + 3), not counting scope-table comparisons
  (`resolve_local`, `resolve_upvalue`, `resolve_syntax_local`, `KnownLambda`,
  `self_tail_name`, `EnvFrame` hashing).
- `src_lambda` (JIT input) and debug local names hold source symbols; `.scc`
  (`SCC_FMT_VER` 8) serializes constants through a closed tag set and hard-fails
  on any other heap type.
- Next free `ObjType` is 58. `#'` `` #` `` `#,` `#,@` are unused in `reader.c`.
- There are no uninterned symbols; gensyms are interned names with a `\x01`.

## Mechanism

An **identifier** is a heap object `{name: symbol, marks: list of mark}`. A
**mark** is `{id, def_scope}`, created fresh for each macro expansion. `def_scope`
records where the macro was defined: for a global or library macro, its
environment (`target_env` or `GLOBAL_ENV`); for a `let-syntax` or internal
`define-syntax` macro, the compiler scope depth at the definition.

Expansion of a use `(m ...)` with mark `k`:
1. The transformer receives the use-site form. Identifiers from the use site
   carry no `k`.
2. `syntax` templates return identifiers that were written in the macro with
   `k` added.
3. The result is compiled as it is. The compilers accept identifiers wherever
   they accept symbols.

Scope tables are keyed by an identifier key, `(name . marks)`:
- A binder adds `(name . marks)` to `locals`.
- `id_resolve(id)` looks for a binder whose key equals `(name . marks)`,
  searching innermost to outermost.
- If none is found and `marks` is not empty, remove the outermost mark `k` and
  resume the search **restricted to `k.def_scope`**: the locals that existed at
  the macro's definition, then its environment. A macro's free reference
  therefore resolves where the macro was written, not at the use site. This is
  what fixes use-site shadowing.
- With no marks left, the search ends in the global environment. A symbol
  resolves to a special form only if nothing shadows it in that scope.

`bound-identifier=?` compares `(name . marks)`. `free-identifier=?` compares
what `id_resolve` returns. `datum->syntax` copies the marks of its context
identifier onto the new name (anaphoric macros).

All non-compiler consumers see plain data. `quote`, `case` datums,
`define-record-type` field names, debug local names and `.scc` constants use
`syntax->datum`, deep and total, so identifiers never reach runtime values.
A binder renamed to avoid a clash gets a unique symbol name for the bytecode
and keeps its source name for the debugger.

## Decisions to make before Phase 3

Each decision lists named candidates, because the choice changes behaviour.

**D1. Special forms become shadowable.** Today a local named `if` or `list-ref`
cannot shadow a special form; with `id_resolve` it can.
- *Shadowable everywhere (R6RS behaviour).* Pros: consistent, simple rule.
  Cons: existing code that binds a special-form name and relies on it being
  ignored changes behaviour. The test suites will show how much.
- *Shadowable only for introduced identifiers (marked).* Pros: user code is
  unchanged. Cons: two rules, and a macro can no longer be told apart from a
  user with the same names.

**D2. Top-level definitions introduced by a macro.** `(define tmp ...)` in a
template: R6RS renames it; common practice makes it visible.
- *Strip marks (visible).* Pros: macros that define public names work as
  expected, which most library macros here rely on. Cons: not hygienic.
- *Rename (hidden).* Pros: hygienic. Cons: needs `datum->syntax` for every
  defining macro, so existing library macros need rewriting.

**D3. Syntax objects in `.scc` constants.**
- *Forbid.* `quote` strips, so none can reach a constant. Pros: no format
  change. Cons: a macro that stores a syntax object in a constant fails at
  write time with a clear error.
- *Add a tag.* Pros: complete. Cons: bumps `SCC_FMT_VER`, adds a reader and a
  writer to keep correct.

**D4. Retrofit `syntax-rules`.** Re-implement it on identifiers and marks, which
removes the renaming heuristic. Risk: `(srfi 26)` `cut`/`cute`, `schematic` and
`matchable` depend on it today. Do it last, behind their existing suites.

## Phases

Each phase is its own PR from `main`, with all ctest suites passing and every
test run using `--clear-cache`.

0. **Design (this document).**
1. **Syntax objects and the `syntax-case` surface.** `T_SYNTAX_OBJ`, reader sugar,
   `syntax-case` with literals, fenders and ellipsis, `syntax`, `quasisyntax`,
   `with-syntax`, `generate-temporaries`, `syntax->datum`, `datum->syntax`,
   `identifier?`, `bound-identifier=?`, `free-identifier=?`. At this stage the
   expansion boundary strips to symbols, so it is usable at once and Schevana's
   macros can be ported.
2. **Identifier accessor layer.** `id_sym`, `id_is`, `is_identifier`; replace the
   direct symbol checks one file at a time. No behaviour change.
3. **Marks and resolution.** Scope tables keyed by identifier key, `id_resolve`,
   binding-aware special-form dispatch (D1), `.scc` (D3), JIT and debug names.
4. **Retrofit and docs.** D4; `docs/reference/syntax-case.md`; update
   `docs/reference/language.md` (it says "no `syntax-case`"), the header of
   `src/syntax_rules.h`, and the Akkadian synonyms for the new forms.

Phases 0–2 are low-risk. Phase 3 is the large, high-regression-risk stage,
and the estimate for it is the least reliable one.

## Open risks

- The restricted-search rule above is the design's core claim. Phase 3 must
  begin with a spike that implements `id_resolve` for one binding form
  (`lambda`) in both evaluators and runs a hygiene test set against it, before
  the other forms are converted.
- The compiler and the tree-walker each carry their own copy of every binding
  form. Both must agree, or the same macro behaves differently when compiled
  and when tree-walked (`include`d files, plain `.scm` modules).
- The JIT bails out on `uses_local_macro` chunks today; confirm it still does.
- `compile_time_eval` sees only `GLOBAL_ENV`, so a library macro's procedural
  transformer cannot call that library's private helpers at compile time. This
  predates `syntax-case` but becomes much more visible with it.
