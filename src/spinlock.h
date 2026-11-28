#ifndef CURRY_SPINLOCK_H
#define CURRY_SPINLOCK_H
/* A lock word for the short critical sections around a hash table, a set or a
 * string port (#452, #453): one operation, no call into Scheme, no other lock.
 *
 * 0 means free, otherwise the holder's token. The one thing that can raise
 * inside a section is the stack-overflow guard (check_c_stack_depth, reached
 * from hashing or comparing a very deeply nested key, also for the stored keys
 * of a rehash). Each thread remembers the word it holds, and the guard
 * releases it before it raises (spin_release_held): the sections change the
 * table only after their last point that can raise, so it is consistent. A
 * thread that still leaves a word held some other way takes it over on its
 * next use, but other threads would wait for it, so the release is the real
 * protection. Waiters spin, then yield the processor. */
#include <stdint.h>
#include <stdbool.h>
#include <sched.h>
#include "gc.h"

extern uint32_t spin_next_token;
extern CURRY_THREAD_LOCAL uint32_t spin_my_token;
extern CURRY_THREAD_LOCAL uint32_t *spin_held;   /* the word this thread holds, or NULL */

static inline void spin_lock(uint32_t *l) {
    if (!spin_my_token) spin_my_token = __atomic_fetch_add(&spin_next_token, 1, __ATOMIC_RELAXED);
    uint32_t me = spin_my_token, expected = 0;
    for (int spins = 0;
         !__atomic_compare_exchange_n(l, &expected, me, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
         spins++) {
        if (expected == me) break;               /* left held by this thread's own raise */
        expected = 0;
        if (spins > 64) sched_yield();
    }
    spin_held = l;
}

static inline void spin_unlock(uint32_t *l) {
    spin_held = NULL;
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

/* Called before a raise that may unwind out of a critical section. */
static inline void spin_release_held(void) {
    if (spin_held) spin_unlock(spin_held);
}

#endif
