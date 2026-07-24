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

/* One binding a define-record-type form introduces: the constructor, the
 * predicate, or one field's accessor/mutator. `body` is a ready-to-splice
 * one-form lambda body (a list of exactly one expression) referencing the
 * %record-ctor/%record-pred?/%record-ref/%record-set! primitives, with the
 * RTD embedded as a (quote <rtd>) literal — the same shape eval.c has always
 * built its Closure bodies from. */
typedef struct {
    val_t name;
    val_t params;
    val_t body;
} RecordBinding;

typedef struct {
    val_t          rtd_val;   /* the built RecordType*, wrapped as val_t */
    RecordBinding *bindings;  /* gc_alloc_raw_pinned'd array, length = count */
    int            count;
} RecordTypeSpec;

/* Parse `rest` — everything after the define-record-type keyword, i.e.
 * (name ctor-form pred field-spec...) for R7RS or
 * (name (fields ...) ...) for R6RS — build the RTD, and fill `spec` with
 * every (name, params, body) triple that needs binding. */
void record_type_build_spec(val_t rest, RecordTypeSpec *spec);

#endif /* CURRY_RECORD_TYPE_H */
