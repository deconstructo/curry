#ifndef CURRY_SET_H
#define CURRY_SET_H

/*
 * Mutable hash sets and hash tables for Curry Scheme.
 *
 * Comparator types:
 *   SET_CMP_EQ     - eq?    (pointer identity / fixnum =)
 *   SET_CMP_EQV    - eqv?   (eq? + same-type numeric =, char=)
 *   SET_CMP_EQUAL  - equal? (recursive structural equality)
 *
 * Scheme API exposed via builtins:
 *   make-eq-set / make-eqv-set / make-equal-set
 *   set-add! set-delete! set-member? set->list list->set
 *   set-union set-intersection set-difference set-subset?
 *   make-hash-table hash-table-set! hash-table-ref hash-table-delete!
 *   hash-table->alist hash-table-keys hash-table-values
 */

#include "value.h"
#include <stdbool.h>

/* ---- Set ---- */
val_t set_make(int cmp_type);
val_t set_add(val_t s, val_t elem);        /* non-destructive, returns new set */
void  set_add_mut(val_t s, val_t elem);    /* destructive */
void  set_delete_mut(val_t s, val_t elem);
bool  set_member(val_t s, val_t elem);
val_t set_to_list(val_t s);
val_t list_to_set(val_t lst, int cmp_type);
uint32_t set_size(val_t s);

val_t set_union(val_t a, val_t b);
val_t set_intersection(val_t a, val_t b);
val_t set_difference(val_t a, val_t b);
bool  set_subset(val_t a, val_t b);   /* is a a subset of b? */
bool  set_equal(val_t a, val_t b);

/* ---- Hash table ---- */
val_t hash_make(int cmp_type);
void  hash_set(val_t h, val_t key, val_t val);
val_t hash_ref(val_t h, val_t key, val_t default_val);
bool  hash_has(val_t h, val_t key);
void  hash_delete(val_t h, val_t key);
val_t hash_to_alist(val_t h);
val_t hash_keys(val_t h);
val_t hash_values(val_t h);
uint32_t hash_size(val_t h);

/* ---- Equality predicates ---- */
bool scm_eq(val_t a, val_t b);      /* eq?    */
bool scm_eqv(val_t a, val_t b);     /* eqv?   */
bool scm_equal(val_t a, val_t b);   /* equal? */

/* Hashing for sets/tables */
uint32_t val_hash(val_t v, int cmp_type);

#endif /* CURRY_SET_H */
