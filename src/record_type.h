#ifndef CURRY_RECORD_TYPE_H
#define CURRY_RECORD_TYPE_H

#include "value.h"

/*
 * Shared define-record-type parsing/construction, used by both eval.c's
 * tree-walker case (kept — library/define-library bodies are always
 * tree-walked, never compiled, so a native compiler path alone can't retire
 * it) and compiler.c's native codegen. Factored out so the two paths parse
 * R7RS and R6RS record syntax identically instead of maintaining separate,
 * silently-divergable copies of the same field/name derivation logic.
 */

/* Which of an RTD's four "record-related procedures" (SRFI-279's own
 * phrase) a given binding is -- lets both compiler.c and eval.c stash
 * each binding's resulting closure back onto the RTD (via
 * %rtd-set-constructor!/-predicate!/-accessor!/-mutator!, see
 * src/builtins.c) without re-deriving R6RS/R7RS's differing field-spec
 * shapes themselves. field_index is only meaningful for ACCESSOR/MUTATOR. */
typedef enum {
    RTD_ROLE_CONSTRUCTOR,
    RTD_ROLE_PREDICATE,
    RTD_ROLE_ACCESSOR,
    RTD_ROLE_MUTATOR,
} RtdRole;

/* One binding a define-record-type form introduces: the constructor, the
 * predicate, or one field's accessor/mutator. `body` is a ready-to-splice
 * one-form lambda body (a list of exactly one expression) referencing the
 * %record-ctor/%record-pred?/%record-ref/%record-set! primitives, with the
 * RTD referenced via whatever `rtd_ref` record_type_build_spec was called
 * with (see below). */
typedef struct {
    val_t   name;
    val_t   params;
    val_t   body;
    RtdRole role;
    int     field_index; /* only meaningful when role is ACCESSOR/MUTATOR */
} RecordBinding;

typedef struct {
    val_t          rtd_val;   /* the built RecordType*, wrapped as val_t */
    RecordBinding *bindings;  /* gc_alloc_raw_pinned'd array, length = count */
    int            count;
} RecordTypeSpec;

/* Parse `rest` — everything after the define-record-type keyword, i.e.
 * (name ctor-form pred field-spec...) for R7RS or
 * (name (fields ...) ...) for R6RS — build the RTD, and fill `spec` with
 * every (name, params, body) triple that needs binding.
 *
 * `rtd_ref` controls how each binding's body refers to the RTD:
 *   - V_FALSE: embed (quote <the just-built RecordType*>) directly — safe
 *     for the tree-walker (eval.c), which builds each binding's Closure
 *     directly in C and never serializes anything.
 *   - a symbol: use that symbol as a plain variable reference instead of
 *     quoting the raw pointer. compiler.c's codegen MUST use this form:
 *     record_type_build_spec is called once and its RTD then gets
 *     embedded as a constant independently in each of N separately-
 *     compiled closures (ctor/pred/accessors/mutators) — in memory that's
 *     harmless (same pointer referenced N times), but serializing N
 *     independent copies to a .scc file and reading them back reconstructs
 *     N non-eq? RecordType objects, breaking %record-pred?'s pointer-
 *     equality check (found by testing: cache-hit define-record-type
 *     scripts failed their own predicate). Referencing a shared variable
 *     instead — compiled to (define <rtd_ref> (%make-record-type ...))
 *     once, ahead of the bindings — keeps identity correct within a single
 *     execution (fresh or cache-hit replayed) without ever embedding the
 *     RTD as a serialized constant at all. */
void record_type_build_spec(val_t rest, val_t rtd_ref, RecordTypeSpec *spec);

#endif /* CURRY_RECORD_TYPE_H */
