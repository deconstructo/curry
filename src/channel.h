#ifndef CURRY_CHANNEL_H
#define CURRY_CHANNEL_H

/*
 * CSP buffered channels — ring buffer + mutex + two condvars.
 *
 * API (Scheme names, all in global env — no import needed):
 *   (make-channel [cap])      → channel   ; cap=0 → synchronous rendezvous
 *   (channel-send! ch val)    → void      ; blocks when full
 *   (channel-recv! ch)        → val       ; blocks when empty
 *   (channel-close! ch)       → void
 *   (channel-closed? ch)      → bool
 *   (channel? v)              → bool
 *
 * Non-blocking primitives (return V_UNDEF on would-block):
 *   (%channel-try-send ch val) → void | V_UNDEF
 *   (%channel-try-recv ch)     → val  | V_UNDEF
 *   (%channel-blocked? v)      → bool
 *
 * The (select ...) macro over %channel-try-* is defined in
 * lib/curry/modules/curry/stm.scm.
 */

#include "value.h"
#include "object.h"

void  channel_init(void);

val_t channel_make(uint32_t cap);
void  channel_send(val_t ch, val_t val);
val_t channel_recv(val_t ch);
void  channel_close(val_t ch);
bool  channel_closed(val_t ch);

/* Non-blocking variants: return V_UNDEF on would-block. */
val_t channel_try_send(val_t ch, val_t val);
val_t channel_try_recv(val_t ch);

#endif /* CURRY_CHANNEL_H */
