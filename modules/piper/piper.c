/*
 * curry_piper — Piper neural text-to-speech (https://github.com/OHF-Voice/piper1-gpl)
 * native bindings, wired into (curry tts) as the 'piper backend.
 *
 * Scheme API:
 *   (piper-create model-path config-path-or-#f espeak-data-path-or-#f) -> synth
 *   (piper-synth? v)                        -> bool
 *   (piper-free! synth)                     -> void
 *   (piper-version)                         -> string
 *   (piper-speak-async synth text speaker-id-or-#f length-scale-or-#f) -> handle
 *     Synthesizes on a background thread and plays directly to the
 *     default audio output device as chunks are produced (CoreAudio on
 *     macOS, ALSA on Linux) -- no temp file, no external player process.
 *   (piper-handle? v)                       -> bool
 *   (piper-wait h)                          -> void   (blocks until done)
 *   (piper-stop! h)                         -> void   (cooperative: sets a
 *                                                       flag the playback
 *                                                       loop checks between
 *                                                       chunks, does not
 *                                                       forcibly kill anything)
 *   (piper-alive? h)                        -> bool
 *   (piper-save synth text path speaker-id-or-#f length-scale-or-#f) -> void
 *     Blocking: synthesizes and writes a 16-bit PCM WAV file. No audio
 *     device involved.
 *
 * Threading/GC safety: the background playback thread spawned by
 * piper-speak-async NEVER touches a curry_val or any GC-heap pointer --
 * `text` is strdup()'d (a plain malloc'd copy, not curry_string()'s
 * GC-heap pointer) before the thread starts, specifically so the thread
 * has zero dependency on curry's GC being aware of it (no
 * gc_register_thread() call, unlike e.g. modules/mcp/mcp.c's per-
 * connection threads, which DO construct curry_vals off the main thread
 * and therefore DO need it). Everything the thread produces for the
 * main thread to read back (done/stop_requested/had_error/error_msg) is
 * plain C state behind a mutex -- piper-wait/piper-alive? only ever
 * construct curry_vals on the calling (VM) thread, after reading that
 * state. If this module ever needs to hand richer data back from the
 * playback thread itself, that data must cross the same plain-C-struct
 * boundary and be turned into a curry_val only once back on a thread
 * curry's GC knows about.
 */

#include <curry.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#include "piper.h"

#if defined(__APPLE__)
#  include <AudioToolbox/AudioToolbox.h>
#  define PIPER_HAVE_COREAUDIO 1
#elif defined(__linux__) && defined(PIPER_BUILD_HAVE_ALSA)
/* PIPER_BUILD_HAVE_ALSA is a build-provided define (CMakeLists.txt),
 * set ONLY when ALSA (libasound + alsa/asoundlib.h) was actually found --
 * ALSA is optional at build time (piper-save, WAV file output, never
 * touches an audio device at all), so this must NOT be assumed from
 * __linux__ alone: that would make the build fail outright (missing
 * header/unresolved symbols) on a Linux box without libasound-dev
 * installed, instead of the intended graceful "piper-speak-async raises
 * a clear error, piper-save still works" runtime fallback below. */
#  include <alsa/asoundlib.h>
#  define PIPER_HAVE_ALSA 1
#endif

/* ── Opaque handle representation ──────────────────────────────────────
 * Same "pack a raw pointer into a bytevector, tag with a marker symbol"
 * convention modules/neo4j/neo4j.c and modules/network/network.c both
 * already use for their own connection/socket handles -- not GC-managed
 * (piper_synthesizer* and PiperPlayback* are plain malloc'd/library-
 * allocated C memory), explicit free required.
 */

static curry_val ptr_to_val(const char *tag, void *p) {
    curry_val bv = curry_make_bytevector(sizeof(void *), 0);
    for (size_t i = 0; i < sizeof(void *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&p)[i]);
    return curry_make_pair(curry_make_symbol(tag), bv);
}

static bool val_is_tagged(curry_val v, const char *tag) {
    return curry_is_pair(v) && curry_is_symbol(curry_car(v)) &&
           strcmp(curry_symbol(curry_car(v)), tag) == 0;
}

static void *val_to_ptr(curry_val v) {
    curry_val bv = curry_cdr(v);
    /* Issue #165: every call site checks val_is_tagged(av[0], tag) first,
     * but that only verifies the CAR -- the cdr, assumed to be a
     * pointer-holding bytevector, was read straight via
     * curry_bytevector_ref with no curry_is_bytevector/length check. A
     * forged handle like (cons 'piper-synth 42) has the right tag but a
     * non-bytevector cdr. */
    if (!curry_is_bytevector(bv) || curry_bytevector_length(bv) != sizeof(void *))
        curry_error("piper: malformed handle");
    void *p;
    for (size_t i = 0; i < sizeof(void *); i++)
        ((uint8_t *)&p)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return p;
}

/* ── Live-synth tracking: a process-exit safety net ────────────────────
 * onnxruntime keeps a process-global OrtEnv singleton whose static
 * destructor runs during exit()'s __cxa_finalize_ranges sweep. If ANY
 * piper_synthesizer is still alive at that point (i.e. piper_free() was
 * never called on it), that destructor throws inside noexcept context
 * and the whole process SIGABRTs -- confirmed with lldb (the crash is
 * entirely inside libonnxruntime's own
 * unique_ptr<OrtEnv>::~unique_ptr, not in this module's code) and by
 * direct comparison against libpiper's own reference CLI
 * (src/main/main.cpp), which always calls piper_free() before
 * returning and exits cleanly. This is NOT the same as the already-
 * documented "no GC finalizer, caller must free" contract's ordinary
 * consequence (a harmless leak) -- skipping piper-free! here crashes
 * the whole process, not just this synth's own memory. (curry tts
 * piper)'s own %synth-cache in particular never frees a cached synth
 * at all, so without this safety net every process that ever used the
 * piper TTS backend would abort on exit. Tracking + freeing any
 * leftovers via atexit() converts that guaranteed crash back into the
 * ordinary, already-accepted "ONNX session memory held until process
 * exit" leak. */
static pthread_mutex_t live_synths_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct SynthNode { piper_synthesizer *synth; struct SynthNode *next; } SynthNode;
static SynthNode *live_synths = NULL;

static void free_leaked_synths_at_exit(void);    /* forward decl, defined below */
static void free_leaked_playbacks_at_exit(void); /* forward decl, defined below */

/* atexit() runs handlers LIFO. onnxruntime lazily constructs its
 * process-global OrtEnv (and registers ITS OWN destructor) inside the
 * FIRST piper_create() call, not at library-load time -- registering
 * our cleanup unconditionally in curry_module_init (i.e. before any
 * synth, hence before ORT's own lazy registration, has ever happened)
 * put us BEFORE ORT in the atexit list, so ORT's crash-inducing
 * teardown ran first at exit regardless of what we did (confirmed:
 * that ordering crashed every run). Registering here instead, on the
 * first successful track_synth() call -- guaranteed to run after
 * piper_create() has already returned, and therefore after ORT's own
 * lazy registration already happened -- puts us LAST in the list, so
 * LIFO order runs our cleanup FIRST, before ORT's teardown. */
static pthread_once_t atexit_once = PTHREAD_ONCE_INIT;
static void register_exit_cleanup_once(void) {
    /* Registered in this order so LIFO makes playback cleanup run
     * BEFORE synth cleanup at exit -- a leftover PiperPlayback holds a
     * borrowed (non-owning) pointer to its synth, so tearing down
     * playbacks first, synths second, keeps that borrow valid for the
     * whole of playback cleanup. */
    atexit(free_leaked_synths_at_exit);
    atexit(free_leaked_playbacks_at_exit);
}

static void track_synth(piper_synthesizer *synth) {
    SynthNode *node = malloc(sizeof(SynthNode));
    pthread_mutex_lock(&live_synths_lock);
    node->synth = synth;
    node->next = live_synths;
    live_synths = node;
    pthread_mutex_unlock(&live_synths_lock);
    pthread_once(&atexit_once, register_exit_cleanup_once);
}

static void untrack_synth(piper_synthesizer *synth) {
    pthread_mutex_lock(&live_synths_lock);
    for (SynthNode **cur = &live_synths; *cur; cur = &(*cur)->next) {
        if ((*cur)->synth == synth) {
            SynthNode *dead = *cur;
            *cur = dead->next;
            free(dead);
            break;
        }
    }
    pthread_mutex_unlock(&live_synths_lock);
}

static void free_leaked_synths_at_exit(void) {
    pthread_mutex_lock(&live_synths_lock);
    SynthNode *node = live_synths;
    live_synths = NULL;
    pthread_mutex_unlock(&live_synths_lock);
    while (node) {
        SynthNode *next = node->next;
        piper_free(node->synth);
        free(node);
        node = next;
    }
}

/* ── piper-create / piper-free! / piper-synth? / piper-version ────────── */

static curry_val fn_piper_create(int ac, curry_val *av, void *ud) {
    (void)ud;
    const char *model_path = curry_string(av[0]);
    const char *config_path = (ac > 1 && curry_is_true(av[1])) ? curry_string(av[1]) : NULL;
    const char *espeak_data_path = (ac > 2 && curry_is_true(av[2])) ? curry_string(av[2]) : NULL;

    /* piper_create is a C++ implementation behind an extern "C" API that
     * does NOT catch its own internal exceptions (confirmed: a
     * nonexistent model_path makes it try to nlohmann::json::parse an
     * empty/missing config file, throw a json::parse_error, and that
     * exception unwinds straight across the C boundary with nothing to
     * catch it -- std::terminate, SIGABRT, the whole process dies, not
     * just this call). We can't add a try/catch from this side (this
     * file is plain C, and rewriting it to C++ just to add exception
     * handling around six functions is disproportionate to the actual
     * gap). Pre-checking the two file paths WE control covers the
     * single most likely real-world trigger -- a typo'd or missing
     * path -- with a normal catchable curry_error instead of a crash;
     * it does not, and cannot, cover every possible internal exception
     * (e.g. a syntactically present but corrupt .onnx/.json file could
     * still crash the process -- that's an upstream libpiper
     * robustness gap, not something fixable from a C wrapper). */
    if (access(model_path, F_OK) != 0)
        curry_error("piper-create: no such model file: %s", model_path);
    if (config_path && access(config_path, F_OK) != 0)
        curry_error("piper-create: no such config file: %s", config_path);

    piper_synthesizer *synth = piper_create(model_path, config_path, espeak_data_path);
    if (!synth)
        curry_error("piper-create: failed to load voice model: %s", model_path);
    track_synth(synth);
    return ptr_to_val("piper-synth", synth);
}

static curry_val fn_piper_synth_p(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_bool(val_is_tagged(av[0], "piper-synth"));
}

static curry_val fn_piper_free(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!val_is_tagged(av[0], "piper-synth"))
        curry_error("piper-free!: not a piper synth handle");
    piper_synthesizer *synth = (piper_synthesizer *)val_to_ptr(av[0]);
    untrack_synth(synth);
    piper_free(synth);
    return curry_void();
}

static curry_val fn_piper_version(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac; (void)av;
    return curry_make_string(piper_version());
}

/* ── Shared: resolve speaker-id/length-scale into synthesize options ──── */

static void resolve_options(piper_synthesizer *synth, curry_val speaker_id, curry_val length_scale,
                             piper_synthesize_options *out, bool *out_has_options) {
    *out = piper_default_synthesize_options(synth);
    *out_has_options = false;
    if (curry_is_true(speaker_id)) {
        out->speaker_id = (int)curry_fixnum(speaker_id);
        *out_has_options = true;
    }
    if (curry_is_true(length_scale)) {
        out->length_scale = (float)curry_float(length_scale);
        *out_has_options = true;
    }
}

/* ── Platform audio output — one tiny abstraction, two backends ───────── */

#if defined(PIPER_HAVE_COREAUDIO)

/* Three buffers in flight, filled/enqueued from the playback thread and
 * released back by CoreAudio's own completion callback (which runs on
 * CoreAudio's internal thread, not ours -- the semaphore is what hands
 * control back to the playback thread once a buffer is free to reuse).
 * No curry_val ever touches this struct or the callback -- pure C/
 * CoreAudio state, matching this whole module's GC-safety design (see
 * this file's own header comment). */
#define PIPER_CA_NUM_BUFFERS 3
typedef struct {
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[PIPER_CA_NUM_BUFFERS];
    dispatch_semaphore_t buffer_free[PIPER_CA_NUM_BUFFERS];
    int next_buffer;
    bool started;
} AudioOutput;

static void ca_output_callback(void *ud, AudioQueueRef q, AudioQueueBufferRef buf) {
    (void)q;
    AudioOutput *out = ud;
    for (int i = 0; i < PIPER_CA_NUM_BUFFERS; i++)
        if (out->buffers[i] == buf) { dispatch_semaphore_signal(out->buffer_free[i]); return; }
}

static AudioOutput *audio_output_open(int sample_rate) {
    AudioOutput *out = calloc(1, sizeof(AudioOutput));
    if (!out) return NULL;

    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel = 32;
    fmt.mChannelsPerFrame = 1;
    fmt.mBytesPerFrame = sizeof(float);
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = sizeof(float);

    if (AudioQueueNewOutput(&fmt, ca_output_callback, out, NULL, NULL, 0, &out->queue) != noErr) {
        free(out);
        return NULL;
    }
    /* 32768 frames (~0.68s at 48kHz) per buffer -- generous enough that a
     * typical piper chunk fits in one buffer without splitting; chunks
     * larger than this are split across multiple enqueue calls below. */
    for (int i = 0; i < PIPER_CA_NUM_BUFFERS; i++) {
        AudioQueueAllocateBuffer(out->queue, 32768 * sizeof(float), &out->buffers[i]);
        out->buffer_free[i] = dispatch_semaphore_create(1);
    }
    return out;
}

static void audio_output_write(AudioOutput *out, const float *samples, size_t num_samples) {
    if (!out) return;
    size_t offset = 0;
    while (offset < num_samples) {
        int i = out->next_buffer;
        out->next_buffer = (out->next_buffer + 1) % PIPER_CA_NUM_BUFFERS;
        /* Block until this buffer slot's previous enqueue has been
         * consumed by the device -- bounded, cooperative backpressure,
         * not a busy loop. */
        dispatch_semaphore_wait(out->buffer_free[i], DISPATCH_TIME_FOREVER);

        AudioQueueBufferRef buf = out->buffers[i];
        size_t max_frames = buf->mAudioDataBytesCapacity / sizeof(float);
        size_t n = num_samples - offset;
        if (n > max_frames) n = max_frames;

        memcpy(buf->mAudioData, samples + offset, n * sizeof(float));
        buf->mAudioDataByteSize = (UInt32)(n * sizeof(float));
        AudioQueueEnqueueBuffer(out->queue, buf, 0, NULL);

        if (!out->started) {
            AudioQueueStart(out->queue, NULL);
            out->started = true;
        }
        offset += n;
    }
}

static void audio_output_close(AudioOutput *out) {
    if (!out) return;
    /* Drain: wait for every buffer to be handed back by the callback
     * (i.e. actually played), THEN stop -- AudioQueueStop's own
     * "immediate" flag would otherwise cut off whatever's still queued. */
    for (int i = 0; i < PIPER_CA_NUM_BUFFERS; i++)
        dispatch_semaphore_wait(out->buffer_free[i], DISPATCH_TIME_FOREVER);
    AudioQueueStop(out->queue, true);
    AudioQueueDispose(out->queue, true);
    free(out);
}

#elif defined(PIPER_HAVE_ALSA)

typedef struct {
    snd_pcm_t *pcm;
} AudioOutput;

static AudioOutput *audio_output_open(int sample_rate) {
    AudioOutput *out = calloc(1, sizeof(AudioOutput));
    if (!out) return NULL;
    if (snd_pcm_open(&out->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        free(out);
        return NULL;
    }
    if (snd_pcm_set_params(out->pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                            1 /* mono -- piper voices are single-channel */,
                            (unsigned int)sample_rate, 1 /* allow soft resample */,
                            500000 /* 0.5s latency */) < 0) {
        snd_pcm_close(out->pcm);
        free(out);
        return NULL;
    }
    return out;
}

static void audio_output_write(AudioOutput *out, const float *samples, size_t num_samples) {
    if (!out) return;
    /* snd_pcm_writei blocks (interleaved-write, RW_INTERLEAVED mode) --
     * ALSA itself provides the backpressure CoreAudio's semaphore dance
     * above has to build by hand. */
    snd_pcm_sframes_t written = 0;
    const float *p = samples;
    snd_pcm_uframes_t remaining = num_samples;
    while (remaining > 0) {
        written = snd_pcm_writei(out->pcm, p, remaining);
        if (written == -EPIPE) {
            snd_pcm_prepare(out->pcm); /* underrun recovery */
            continue;
        }
        if (written < 0) break; /* other error -- give up on this chunk */
        p += written;
        remaining -= (snd_pcm_uframes_t)written;
    }
}

static void audio_output_close(AudioOutput *out) {
    if (!out) return;
    snd_pcm_drain(out->pcm); /* blocks until the device has actually played everything queued */
    snd_pcm_close(out->pcm);
    free(out);
}

#else

/* No native audio backend on this platform -- piper-speak-async will
 * raise (see fn_piper_speak_async) rather than silently synthesizing
 * into nothing. piper-save (WAV-file output) still works everywhere. */
typedef struct { int unused; } AudioOutput;
static AudioOutput *audio_output_open(int sample_rate) { (void)sample_rate; return NULL; }
static void audio_output_write(AudioOutput *out, const float *samples, size_t num_samples) {
    (void)out; (void)samples; (void)num_samples;
}
static void audio_output_close(AudioOutput *out) { (void)out; }

#endif

/* ── piper-speak-async / piper-handle? / piper-wait / piper-stop! / piper-alive? ── */

typedef struct {
    piper_synthesizer *synth;   /* borrowed -- caller's PiperSynth must
                                    outlive this playback, same convention
                                    as e.g. a socket outliving a port
                                    taken from it elsewhere in this codebase */
    char *text;                 /* owned (strdup) */
    piper_synthesize_options options;
    bool has_options;

    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done;
    bool stop_requested;
    bool had_error;
    char error_msg[256];
} PiperPlayback;

/* ── Live-playback tracking: same atexit safety net as live-synth
 * tracking above, but simpler -- there is no "piper-handle-free!"
 * primitive (a playback handle behaves like a (curry posix) process
 * handle: it stays valid indefinitely after piper-wait, with no
 * explicit release call, reclaimed automatically at process exit).
 * So every handle just stays tracked from creation until exit, no
 * untrack step needed anywhere. At exit, only free handles whose
 * playback thread has actually finished (pb->done) -- one still
 * running (process exiting while a fire-and-forget speak-async is
 * still mid-flight, never waited/stopped) is left as a known, bounded
 * leak rather than risk destroying a mutex/cond a detached thread
 * might still be using concurrently. */
static pthread_mutex_t live_playbacks_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct PlaybackNode { PiperPlayback *pb; struct PlaybackNode *next; } PlaybackNode;
static PlaybackNode *live_playbacks = NULL;

static void track_playback(PiperPlayback *pb) {
    PlaybackNode *node = malloc(sizeof(PlaybackNode));
    pthread_mutex_lock(&live_playbacks_lock);
    node->pb = pb;
    node->next = live_playbacks;
    live_playbacks = node;
    pthread_mutex_unlock(&live_playbacks_lock);
    pthread_once(&atexit_once, register_exit_cleanup_once);
}

static void free_leaked_playbacks_at_exit(void) {
    pthread_mutex_lock(&live_playbacks_lock);
    PlaybackNode *node = live_playbacks;
    live_playbacks = NULL;
    pthread_mutex_unlock(&live_playbacks_lock);
    while (node) {
        PlaybackNode *next = node->next;
        pthread_mutex_lock(&node->pb->lock);
        bool done = node->pb->done;
        pthread_mutex_unlock(&node->pb->lock);
        if (done) {
            pthread_mutex_destroy(&node->pb->lock);
            pthread_cond_destroy(&node->pb->cond);
            free(node->pb);
        }
        free(node);
        node = next;
    }
}

static void *playback_thread_fn(void *arg) {
    PiperPlayback *pb = arg;
    AudioOutput *out = NULL;

    int rc = piper_synthesize_start(pb->synth, pb->text,
                                     pb->has_options ? &pb->options : NULL);
    if (rc != PIPER_OK) {
        pthread_mutex_lock(&pb->lock);
        pb->had_error = true;
        snprintf(pb->error_msg, sizeof(pb->error_msg),
                 "piper_synthesize_start failed (code %d)", rc);
        pthread_mutex_unlock(&pb->lock);
        goto finish;
    }

    for (;;) {
        pthread_mutex_lock(&pb->lock);
        bool stop = pb->stop_requested;
        pthread_mutex_unlock(&pb->lock);
        if (stop) break;

        piper_audio_chunk chunk;
        int r = piper_synthesize_next(pb->synth, &chunk);
        /* PIPER_DONE is returned on the SAME call that delivers the final
         * chunk's samples (confirmed against libpiper's own reference
         * caller, src/main/utils/wavfile.cpp: it ignores the return code
         * entirely and drives its loop off chunk.is_last, processing
         * chunk.samples on every call including the one returning
         * PIPER_DONE). Treating PIPER_DONE as "nothing to process, stop
         * now" -- the original bug here -- silently discarded that last
         * chunk's audio on every call, losing all audio outright for any
         * text short enough to finish in a single chunk. Only a genuine
         * negative error code (PIPER_ERR_GENERIC) is a real error. */
        if (r != PIPER_OK && r != PIPER_DONE) {
            pthread_mutex_lock(&pb->lock);
            pb->had_error = true;
            snprintf(pb->error_msg, sizeof(pb->error_msg),
                     "piper_synthesize_next failed (code %d)", r);
            pthread_mutex_unlock(&pb->lock);
            break;
        }
        if (!out) {
            out = audio_output_open(chunk.sample_rate);
            if (!out) {
                pthread_mutex_lock(&pb->lock);
                pb->had_error = true;
                snprintf(pb->error_msg, sizeof(pb->error_msg),
                         "could not open the default audio output device");
                pthread_mutex_unlock(&pb->lock);
                break;
            }
        }
        audio_output_write(out, chunk.samples, chunk.num_samples);
        if (chunk.is_last) break;
    }
    audio_output_close(out);

finish:
    free(pb->text);
    pb->text = NULL;
    pthread_mutex_lock(&pb->lock);
    pb->done = true;
    pthread_cond_broadcast(&pb->cond);
    pthread_mutex_unlock(&pb->lock);
    return NULL;
}

static curry_val fn_piper_speak_async(int ac, curry_val *av, void *ud) {
    (void)ud;
    if (!val_is_tagged(av[0], "piper-synth"))
        curry_error("piper-speak-async: not a piper synth handle");
#if !defined(PIPER_HAVE_COREAUDIO) && !defined(PIPER_HAVE_ALSA)
    curry_error("piper-speak-async: no native audio output backend on this platform "
                "(use piper-save to write a WAV file instead)");
#endif
    piper_synthesizer *synth = (piper_synthesizer *)val_to_ptr(av[0]);
    const char *text = curry_string(av[1]);
    curry_val speaker_id = ac > 2 ? av[2] : curry_make_bool(false);
    curry_val length_scale = ac > 3 ? av[3] : curry_make_bool(false);

    PiperPlayback *pb = calloc(1, sizeof(PiperPlayback));
    if (!pb) curry_error("piper-speak-async: out of memory");
    pb->synth = synth;
    pb->text = strdup(text);
    if (!pb->text) { free(pb); curry_error("piper-speak-async: out of memory"); }
    resolve_options(synth, speaker_id, length_scale, &pb->options, &pb->has_options);
    pthread_mutex_init(&pb->lock, NULL);
    pthread_cond_init(&pb->cond, NULL);

    if (pthread_create(&pb->thread, NULL, playback_thread_fn, pb) != 0) {
        pthread_mutex_destroy(&pb->lock);
        pthread_cond_destroy(&pb->cond);
        free(pb->text);
        free(pb);
        curry_error("piper-speak-async: failed to start playback thread");
    }
    pthread_detach(pb->thread); /* piper-wait joins via the cond var, not pthread_join */
    track_playback(pb);

    return ptr_to_val("piper-handle", pb);
}

static curry_val fn_piper_handle_p(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    return curry_make_bool(val_is_tagged(av[0], "piper-handle"));
}

/* piper-wait used to free `pb` (including its pthread_mutex_t/
 * pthread_cond_t) once pb->done was confirmed true, on the theory that
 * "the playback thread is provably done touching pb, so it's safe."
 * That was true for the playback thread, but wrong for the CALLER:
 * (curry tts)'s own handle-usage pattern (mirrored from real (curry
 * posix) process handles, which stay valid indefinitely after
 * process-wait -- you can call process-alive? on an already-waited
 * process at any time) calls piper-stop!/piper-wait/piper-alive? in
 * sequence on the SAME handle, same as it always has for the process-
 * handle backends. Freeing inside piper-wait made that sequence a
 * live use-after-free -- caught by piper_tests.scm's own "tts-
 * speaking? is #f after tts-stop+tts-wait" check silently returning
 * the wrong (stale/undefined) answer instead of crashing outright.
 * Fixed by NOT freeing here -- pb now stays valid after wait, exactly
 * like a process handle -- and reclaiming it instead via the same
 * live-tracking + atexit() safety net already used for piper-synth
 * handles (see track_synth/free_leaked_synths_at_exit above), so
 * repeated piper-speak-async use over a long-lived process doesn't
 * accumulate unbounded live handles either. */
static curry_val fn_piper_wait(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!val_is_tagged(av[0], "piper-handle"))
        curry_error("piper-wait: not a piper playback handle");
    PiperPlayback *pb = (PiperPlayback *)val_to_ptr(av[0]);

    pthread_mutex_lock(&pb->lock);
    while (!pb->done)
        pthread_cond_wait(&pb->cond, &pb->lock);
    bool had_error = pb->had_error;
    char msg[256];
    if (had_error) memcpy(msg, pb->error_msg, sizeof(msg));
    pthread_mutex_unlock(&pb->lock);

    if (had_error) curry_error("piper-wait: %s", msg);
    return curry_void();
}

static curry_val fn_piper_stop(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!val_is_tagged(av[0], "piper-handle"))
        curry_error("piper-stop!: not a piper playback handle");
    PiperPlayback *pb = (PiperPlayback *)val_to_ptr(av[0]);
    pthread_mutex_lock(&pb->lock);
    pb->stop_requested = true;
    pthread_mutex_unlock(&pb->lock);
    return curry_void();
}

static curry_val fn_piper_alive_p(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!val_is_tagged(av[0], "piper-handle"))
        curry_error("piper-alive?: not a piper playback handle");
    PiperPlayback *pb = (PiperPlayback *)val_to_ptr(av[0]);
    pthread_mutex_lock(&pb->lock);
    bool done = pb->done;
    pthread_mutex_unlock(&pb->lock);
    return curry_make_bool(!done);
}

/* ── piper-save: blocking, no thread, writes a 16-bit PCM WAV file ────── */

/* Standard "placeholder sizes, patch after writing all data" streaming
 * WAV-write technique -- piper's own synthesis is chunked/streaming with
 * no upfront total-length, so the header's two size fields (RIFF chunk
 * size, data chunk size) can't be known until synthesis finishes. */
static void write_wav_header(FILE *f, int sample_rate, uint32_t data_bytes) {
    uint32_t riff_size = 36 + data_bytes;
    uint16_t channels = 1, bits_per_sample = 16, block_align = 2, audio_format = 1;
    uint32_t byte_rate = (uint32_t)sample_rate * block_align;

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

static int16_t float_to_pcm16(float s) {
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return (int16_t)(s * 32767.0f);
}

static curry_val fn_piper_save(int ac, curry_val *av, void *ud) {
    (void)ud;
    if (!val_is_tagged(av[0], "piper-synth"))
        curry_error("piper-save: not a piper synth handle");
    piper_synthesizer *synth = (piper_synthesizer *)val_to_ptr(av[0]);
    const char *text = curry_string(av[1]);
    const char *path = curry_string(av[2]);
    curry_val speaker_id = ac > 3 ? av[3] : curry_make_bool(false);
    curry_val length_scale = ac > 4 ? av[4] : curry_make_bool(false);

    piper_synthesize_options options;
    bool has_options;
    resolve_options(synth, speaker_id, length_scale, &options, &has_options);

    FILE *f = fopen(path, "wb");
    if (!f) curry_error("piper-save: could not open %s for writing", path);
    /* Placeholder header -- sample_rate isn't known until the first
     * chunk, so this gets fully rewritten by write_wav_header below once
     * synthesis is done. Reserving the 44 bytes now keeps the data
     * writes at a fixed offset. */
    char zeros[44] = {0};
    fwrite(zeros, 1, sizeof(zeros), f);

    int rc = piper_synthesize_start(synth, text, has_options ? &options : NULL);
    if (rc != PIPER_OK) {
        fclose(f);
        curry_error("piper-save: piper_synthesize_start failed (code %d)", rc);
    }

    int sample_rate = 0;
    uint32_t data_bytes = 0;
    int16_t *pcm_buf = NULL;
    size_t pcm_buf_cap = 0;

    for (;;) {
        piper_audio_chunk chunk;
        int r = piper_synthesize_next(synth, &chunk);
        /* See playback_thread_fn's matching comment: PIPER_DONE carries
         * valid samples in the same call, not a separate empty final
         * call -- only a genuine negative error code is an error. */
        if (r != PIPER_OK && r != PIPER_DONE) {
            free(pcm_buf);
            fclose(f);
            curry_error("piper-save: piper_synthesize_next failed (code %d)", r);
        }
        if (sample_rate == 0) sample_rate = chunk.sample_rate;

        if (chunk.num_samples > pcm_buf_cap) {
            int16_t *grown = realloc(pcm_buf, chunk.num_samples * sizeof(int16_t));
            if (!grown) {
                free(pcm_buf);
                fclose(f);
                curry_error("piper-save: out of memory");
            }
            pcm_buf = grown;
            pcm_buf_cap = chunk.num_samples;
        }
        for (size_t i = 0; i < chunk.num_samples; i++)
            pcm_buf[i] = float_to_pcm16(chunk.samples[i]);
        /* chunk.num_samples == 0 is legal (an empty/silent chunk) and
         * leaves pcm_buf possibly still NULL (never grown yet) --
         * fwrite's own contract only requires a valid pointer when
         * nmemb > 0, but skip the call entirely rather than lean on
         * that, since passing NULL to fwrite even with size 0 is a
         * corner some stricter libc implementations could plausibly
         * flag. */
        if (chunk.num_samples > 0)
            fwrite(pcm_buf, sizeof(int16_t), chunk.num_samples, f);
        data_bytes += (uint32_t)(chunk.num_samples * sizeof(int16_t));

        if (chunk.is_last) break;
    }
    free(pcm_buf);

    if (sample_rate == 0) sample_rate = 22050; /* no chunks at all (empty text) -- a benign fallback, never divides by zero downstream */
    write_wav_header(f, sample_rate, data_bytes);
    fclose(f);
    return curry_void();
}

/* ── Module registration ───────────────────────────────────────────────── */

void curry_module_init(CurryVM *vm) {
    curry_define_fn(vm, "piper-create",      fn_piper_create,      1, 3, NULL);
    curry_define_fn(vm, "piper-synth?",      fn_piper_synth_p,     1, 1, NULL);
    curry_define_fn(vm, "piper-free!",       fn_piper_free,        1, 1, NULL);
    curry_define_fn(vm, "piper-version",     fn_piper_version,     0, 0, NULL);
    curry_define_fn(vm, "piper-speak-async", fn_piper_speak_async, 2, 4, NULL);
    curry_define_fn(vm, "piper-handle?",     fn_piper_handle_p,    1, 1, NULL);
    curry_define_fn(vm, "piper-wait",        fn_piper_wait,        1, 1, NULL);
    curry_define_fn(vm, "piper-stop!",       fn_piper_stop,        1, 1, NULL);
    curry_define_fn(vm, "piper-alive?",      fn_piper_alive_p,     1, 1, NULL);
    curry_define_fn(vm, "piper-save",        fn_piper_save,        3, 5, NULL);
}
