/*
 * CSP buffered channel implementation.
 *
 * Each channel is a heap-allocated Channel struct (T_CHANNEL).  The ring
 * buffer is a gc_alloc'd val_t array that is GC-visible so the collector
 * keeps channel contents alive.  A single pthread_mutex_t protects all
 * fields; two condvars (not_full, not_empty) let senders and receivers sleep
 * without spinning.
 *
 * cap == 0:  synchronous rendezvous — a sender blocks until a receiver is
 *            waiting (and vice versa).  Implemented as a 1-slot buffer with
 *            flag bits in hdr.flags tracking which side is parked.
 */

#include "channel.h"
#include "gc.h"
#include "eval.h"
#include "value.h"
#include "object.h"

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define CH_SYNC_WAITING_SEND  0x01u
#define CH_SYNC_WAITING_RECV  0x02u

void channel_init(void) {}

/* ---- Allocation ---- */

val_t channel_make(uint32_t cap) {
    Channel *ch   = CURRY_NEW(Channel);
    ch->hdr.type  = T_CHANNEL;
    ch->hdr.flags = 0;
    ch->head      = 0;
    ch->tail      = 0;
    ch->count     = 0;
    ch->cap       = cap;
    ch->closed    = false;

    uint32_t ring_cap = (cap == 0) ? 1 : cap;
    ch->buf = gc_alloc(ring_cap * sizeof(val_t));

    pthread_mutex_init(&ch->lock,      NULL);
    pthread_cond_init(&ch->not_full,   NULL);
    pthread_cond_init(&ch->not_empty,  NULL);
    return vptr(ch);
}

/* ---- Send ---- */

void channel_send(val_t v, val_t val) {
    if (!vis_channel(v))
        scm_raise(V_FALSE, "channel-send!: not a channel");
    Channel *ch = as_channel(v);

    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        scm_raise(V_FALSE, "channel-send!: channel is closed");
    }

    if (ch->cap == 0) {
        ch->buf[0] = val;
        ch->hdr.flags |= CH_SYNC_WAITING_SEND;
        pthread_cond_signal(&ch->not_empty);
        while ((ch->hdr.flags & CH_SYNC_WAITING_SEND) && !ch->closed)
            pthread_cond_wait(&ch->not_full, &ch->lock);
    } else {
        while (ch->count == ch->cap && !ch->closed)
            pthread_cond_wait(&ch->not_full, &ch->lock);

        if (ch->closed) {
            pthread_mutex_unlock(&ch->lock);
            scm_raise(V_FALSE, "channel-send!: channel closed while waiting");
        }

        ch->buf[ch->tail] = val;
        ch->tail = (ch->tail + 1) % ch->cap;
        ch->count++;
        pthread_cond_signal(&ch->not_empty);
    }

    pthread_mutex_unlock(&ch->lock);
}

/* ---- Recv ---- */

val_t channel_recv(val_t v) {
    if (!vis_channel(v))
        scm_raise(V_FALSE, "channel-recv!: not a channel");
    Channel *ch = as_channel(v);

    pthread_mutex_lock(&ch->lock);

    if (ch->cap == 0) {
        ch->hdr.flags |= CH_SYNC_WAITING_RECV;
        pthread_cond_signal(&ch->not_full);
        while (!(ch->hdr.flags & CH_SYNC_WAITING_SEND) && !ch->closed)
            pthread_cond_wait(&ch->not_empty, &ch->lock);

        if (ch->closed && !(ch->hdr.flags & CH_SYNC_WAITING_SEND)) {
            ch->hdr.flags &= ~(uint32_t)CH_SYNC_WAITING_RECV;
            pthread_mutex_unlock(&ch->lock);
            scm_raise(V_FALSE, "channel-recv!: channel closed with no sender");
        }

        val_t val = ch->buf[0];
        ch->hdr.flags &= ~(uint32_t)(CH_SYNC_WAITING_SEND | CH_SYNC_WAITING_RECV);
        pthread_cond_broadcast(&ch->not_full);
        pthread_mutex_unlock(&ch->lock);
        return val;

    } else {
        while (ch->count == 0 && !ch->closed)
            pthread_cond_wait(&ch->not_empty, &ch->lock);

        if (ch->count == 0) {
            pthread_mutex_unlock(&ch->lock);
            scm_raise(V_FALSE, "channel-recv!: channel closed and empty");
        }

        val_t val = ch->buf[ch->head];
        ch->head  = (ch->head + 1) % ch->cap;
        ch->count--;
        pthread_cond_signal(&ch->not_full);
        pthread_mutex_unlock(&ch->lock);
        return val;
    }
}

/* ---- Close ---- */

void channel_close(val_t v) {
    if (!vis_channel(v))
        scm_raise(V_FALSE, "channel-close!: not a channel");
    Channel *ch = as_channel(v);

    pthread_mutex_lock(&ch->lock);
    ch->closed = true;
    pthread_cond_broadcast(&ch->not_full);
    pthread_cond_broadcast(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);
}

bool channel_closed(val_t v) {
    if (!vis_channel(v))
        scm_raise(V_FALSE, "channel-closed?: not a channel");
    Channel *ch = as_channel(v);
    pthread_mutex_lock(&ch->lock);
    bool c = ch->closed;
    pthread_mutex_unlock(&ch->lock);
    return c;
}

/* ---- Non-blocking variants ---- */

val_t channel_try_send(val_t v, val_t val) {
    if (!vis_channel(v))
        scm_raise(V_FALSE, "channel-try-send!: not a channel");
    Channel *ch = as_channel(v);

    pthread_mutex_lock(&ch->lock);
    val_t ret = V_UNDEF;

    if (!ch->closed && ch->cap > 0 && ch->count < ch->cap) {
        ch->buf[ch->tail] = val;
        ch->tail = (ch->tail + 1) % ch->cap;
        ch->count++;
        pthread_cond_signal(&ch->not_empty);
        ret = V_VOID;
    }

    pthread_mutex_unlock(&ch->lock);
    return ret;
}

val_t channel_try_recv(val_t v) {
    if (!vis_channel(v))
        scm_raise(V_FALSE, "channel-try-recv!: not a channel");
    Channel *ch = as_channel(v);

    pthread_mutex_lock(&ch->lock);
    val_t ret = V_UNDEF;

    if (ch->cap > 0 && ch->count > 0) {
        ret      = ch->buf[ch->head];
        ch->head = (ch->head + 1) % ch->cap;
        ch->count--;
        pthread_cond_signal(&ch->not_full);
    }

    pthread_mutex_unlock(&ch->lock);
    return ret;
}
