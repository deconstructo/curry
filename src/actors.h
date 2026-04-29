#ifndef CURRY_ACTORS_H
#define CURRY_ACTORS_H

/*
 * Erlang-style actor system for Curry Scheme.
 *
 * Each actor is an independent unit of computation with:
 *   - Its own Scheme closure as entry point
 *   - A private mailbox (mutex-protected message queue)
 *   - A unique 64-bit ID
 *   - A parent actor (for error propagation)
 *   - A link set (linked actors are notified on exit)
 *
 * Concurrency model:
 *   - Each actor runs in a POSIX thread (M:N mapping, one thread per actor)
 *   - Actors share the Boehm GC heap (GC is thread-safe)
 *   - Message passing is the only sanctioned communication between actors
 *   - No shared mutable state between actors (by convention)
 *
 * Scheme API:
 *   (spawn proc [arg...])   -> actor
 *   (send! actor msg)       -> void
 *   (receive)               -> msg       ; blocks until message arrives
 *   (receive timeout-ms)    -> msg or #f ; with timeout
 *   (self)                  -> actor     ; current actor
 *   (actor-id actor)        -> fixnum
 *   (actor-alive? actor)    -> bool
 *   (link! actor)           ; link current actor to actor (bidirectional exit notify)
 *   (monitor! actor)        ; one-way: receive {'DOWN, actor, reason} on exit
 *   (exit! reason)          ; terminate current actor with reason
 */

#include "value.h"
#include "object.h"
#include <stdbool.h>
#include <pthread.h>

void   actors_init(void);

val_t  actor_spawn(val_t closure, val_t args);
void   actor_send(val_t actor, val_t msg);
val_t  actor_receive(val_t actor, long timeout_ms);  /* -1 = no timeout */
val_t  actor_self(void);
void   actor_exit(val_t reason) __attribute__((noreturn));
void   actor_link(val_t a, val_t b);     /* bidirectional */
void   actor_monitor(val_t monitor, val_t target);

bool   actor_alive(val_t actor);
uint64_t actor_id(val_t actor);

/* Thread-local current actor (NULL in main thread) */
extern _Thread_local Actor *current_actor;

#endif /* CURRY_ACTORS_H */
