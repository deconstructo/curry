#ifndef CURRY_SPINLOCK_H
#define CURRY_SPINLOCK_H
/* A lock word for the short critical sections around a hash table, a set or a
 * string port (#452, #453): one operation, no call into Scheme, no other lock.
 *
 * 0 means free, otherwise the holder's token. A raise inside a critical
 * section (the stack-overflow guard in equal?/hash on a very deeply nested
 * key) would leave the word held; the same thread takes it over on its next
 * use instead of waiting for itself. Waiters spin, then yield the processor. */
#include <stdint.h>
#include <stdbool.h>
#include <sched.h>
#include "gc.h"

extern uint32_t spin_next_token;
extern CURRY_THREAD_LOCAL uint32_t spin_my_token;

static inline void spin_lock(uint32_t *l) {
    if (!spin_my_token) spin_my_token = __atomic_fetch_add(&spin_next_token, 1, __ATOMIC_RELAXED);
    uint32_t me = spin_my_token, expected = 0;
    for (int spins = 0;
         !__atomic_compare_exchange_n(l, &expected, me, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
         spins++) {
        if (expected == me) return;              /* left held by this thread's own raise */
        expected = 0;
        if (spins > 64) sched_yield();
    }
}

static inline void spin_unlock(uint32_t *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }

#endif
