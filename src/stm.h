#ifndef CURRY_STM_H
#define CURRY_STM_H

/*
 * Software Transactional Memory — TL2 algorithm
 *
 * API (Scheme names):
 *   (make-tvar val)                  → tvar
 *   (tvar-read tv)                   → val   ; works inside or outside atomically
 *   (tvar-write! tv val)             → void
 *   (atomically thunk)               → val   ; runs thunk atomically; retries on conflict
 *   (retry)                          → never ; block until any read tvar changes, then restart
 *   (or-else thunk1 thunk2)          → val   ; try thunk1; if it retries, try thunk2
 *
 * TL2 overview:
 *   - Global version clock (atomic uint64).
 *   - Each TVar carries its own version (when it was last committed) and a mutex
 *     held only during commit.
 *   - A transaction keeps a read-set {(tv, ver)} and a write-set {(tv, new_val)}.
 *   - Commit: lock all write tvars; bump global clock; validate reads; apply writes;
 *     unlock + signal waiters.
 *   - retry: release transaction, wait on a condvar that every read tvar signals on
 *     commit, then restart the whole atomically body.
 *   - or-else: saves a checkpoint of the transaction log; on retry from the first
 *     thunk, restores the checkpoint and tries the second; if both retry, the
 *     combined read-set is used to wait.
 */

#include "value.h"
#include "object.h"

void stm_init(void);

/* C-level helpers used by builtins */
val_t stm_make_tvar(val_t init);
val_t stm_tvar_read(val_t tv);
void  stm_tvar_write(val_t tv, val_t val);
val_t stm_atomically(val_t thunk);
val_t stm_or_else(val_t thunk1, val_t thunk2);

/* Called by the (retry) primitive — never returns normally */
void stm_retry(void) __attribute__((noreturn));

#endif /* CURRY_STM_H */
