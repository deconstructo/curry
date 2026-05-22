/*
 * curry_tts -- Text-to-speech module for Curry Scheme via eSpeak NG.
 *
 * Scheme API:
 *   (tts-speak text)              -> void  (synchronous playback)
 *   (tts-speak text lang)         -> void  (set voice/language first)
 *   (tts->pcm text)               -> bytevector of 16-bit LE PCM samples
 *   (tts-sample-rate)             -> integer (Hz)
 *   (tts-set-rate! wpm)           -> void  (80-450 words/min)
 *   (tts-set-pitch! pitch)        -> void  (0-100, 50 = normal)
 *   (tts-set-volume! vol)         -> void  (0-200, 100 = normal)
 *   (tts-set-voice! name)         -> void  (e.g. "en", "en-us", "fr")
 *   (tts-voices)                  -> list of voice-name strings
 */

#include <curry.h>
#include <espeak-ng/speak_lib.h>
#include <string.h>
#include <stdlib.h>

/* ---- persistent settings ---- */
static int  g_sample_rate = 0;
static int  g_rate        = espeakRATE_NORMAL;
static int  g_pitch       = 50;
static int  g_volume      = 100;
static char g_voice[64]   = "en";

typedef enum { MODE_NONE, MODE_PLAYBACK, MODE_RETRIEVAL } TtsMode;
static TtsMode g_mode = MODE_NONE;

/* ---- PCM accumulation buffer (used in RETRIEVAL mode) ---- */
static short  *g_pcm_data = NULL;
static size_t  g_pcm_len  = 0;
static size_t  g_pcm_cap  = 0;

static int pcm_callback(short *wav, int numsamples, espeak_EVENT *events) {
    (void)events;
    if (!wav || numsamples <= 0) return 0;
    size_t need = g_pcm_len + (size_t)numsamples;
    if (need > g_pcm_cap) {
        size_t newcap = need * 2 + 8192;
        short *p = realloc(g_pcm_data, newcap * sizeof(short));
        if (!p) return 1;
        g_pcm_data = p;
        g_pcm_cap  = newcap;
    }
    memcpy(g_pcm_data + g_pcm_len, wav, (size_t)numsamples * sizeof(short));
    g_pcm_len += (size_t)numsamples;
    return 0;
}

static void apply_settings(void) {
    espeak_SetParameter(espeakRATE,   g_rate,   0);
    espeak_SetParameter(espeakPITCH,  g_pitch,  0);
    espeak_SetParameter(espeakVOLUME, g_volume, 0);
    espeak_SetVoiceByName(g_voice);
}

static void ensure_playback(void) {
    if (g_mode == MODE_PLAYBACK) return;
    if (g_mode != MODE_NONE) espeak_Terminate();
    int sr = espeak_Initialize(AUDIO_OUTPUT_SYNCH_PLAYBACK, 0, NULL, 0);
    if (sr < 0) curry_error("tts: espeak_Initialize failed");
    g_sample_rate = sr;
    g_mode        = MODE_PLAYBACK;
    apply_settings();
}

static void ensure_retrieval(void) {
    if (g_mode == MODE_RETRIEVAL) return;
    if (g_mode != MODE_NONE) espeak_Terminate();
    int sr = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, NULL, 0);
    if (sr < 0) curry_error("tts: espeak_Initialize (retrieval) failed");
    g_sample_rate = sr;
    g_mode        = MODE_RETRIEVAL;
    espeak_SetSynthCallback(pcm_callback);
    apply_settings();
}

/* ---- (tts-speak text [lang]) ---- */
static curry_val fn_tts_speak(int ac, curry_val *av, void *ud) {
    (void)ud;
    if (!curry_is_string(av[0]))
        curry_error("tts-speak: expected string text");
    if (ac >= 2) {
        if (!curry_is_string(av[1]))
            curry_error("tts-speak: lang must be a string");
        const char *lang = curry_string(av[1]);
        strncpy(g_voice, lang, sizeof(g_voice) - 1);
        g_voice[sizeof(g_voice) - 1] = '\0';
        if (g_mode == MODE_PLAYBACK) espeak_SetVoiceByName(g_voice);
        else g_mode = MODE_NONE;
    }
    ensure_playback();
    const char *text = curry_string(av[0]);
    espeak_ERROR rc = espeak_Synth(text, strlen(text) + 1,
                                   0, POS_CHARACTER, 0,
                                   espeakCHARS_UTF8, NULL, NULL);
    if (rc != EE_OK)
        curry_error("tts-speak: synthesis failed (error %d)", (int)rc);
    return curry_void();
}

/* ---- (tts->pcm text) -> bytevector ---- */
static curry_val fn_tts_to_pcm(int ac, curry_val *av, void *ud) {
    (void)ud; (void)ac;
    if (!curry_is_string(av[0]))
        curry_error("tts->pcm: expected string text");
    ensure_retrieval();
    g_pcm_len = 0;
    const char *text = curry_string(av[0]);
    espeak_ERROR rc = espeak_Synth(text, strlen(text) + 1,
                                   0, POS_CHARACTER, 0,
                                   espeakCHARS_UTF8, NULL, NULL);
    if (rc != EE_OK)
        curry_error("tts->pcm: synthesis failed (error %d)", (int)rc);
    size_t nbytes = g_pcm_len * sizeof(short);
    curry_val bv = curry_make_bytevector((uint32_t)nbytes, 0);
    const uint8_t *src = (const uint8_t *)g_pcm_data;
    for (uint32_t i = 0; i < (uint32_t)nbytes; i++)
        curry_bytevector_set(bv, i, src[i]);
    return bv;
}

/* ---- (tts-sample-rate) ---- */
static curry_val fn_tts_sample_rate(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    if (g_mode == MODE_NONE) ensure_playback();
    return curry_make_fixnum(g_sample_rate);
}

/* ---- (tts-set-rate! wpm) ---- */
static curry_val fn_tts_set_rate(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0]))
        curry_error("tts-set-rate!: expected integer words-per-minute");
    intptr_t wpm = curry_fixnum(av[0]);
    if (wpm < espeakRATE_MINIMUM || wpm > espeakRATE_MAXIMUM)
        curry_error("tts-set-rate!: rate %ld out of range %d-%d",
                    (long)wpm, espeakRATE_MINIMUM, espeakRATE_MAXIMUM);
    g_rate = (int)wpm;
    if (g_mode != MODE_NONE) espeak_SetParameter(espeakRATE, g_rate, 0);
    return curry_void();
}

/* ---- (tts-set-pitch! pitch) ---- */
static curry_val fn_tts_set_pitch(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0]))
        curry_error("tts-set-pitch!: expected integer 0-100");
    intptr_t p = curry_fixnum(av[0]);
    if (p < 0 || p > 100)
        curry_error("tts-set-pitch!: pitch %ld out of range 0-100", (long)p);
    g_pitch = (int)p;
    if (g_mode != MODE_NONE) espeak_SetParameter(espeakPITCH, g_pitch, 0);
    return curry_void();
}

/* ---- (tts-set-volume! vol) ---- */
static curry_val fn_tts_set_volume(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_fixnum(av[0]))
        curry_error("tts-set-volume!: expected integer 0-200");
    intptr_t v = curry_fixnum(av[0]);
    if (v < 0 || v > 200)
        curry_error("tts-set-volume!: volume %ld out of range 0-200", (long)v);
    g_volume = (int)v;
    if (g_mode != MODE_NONE) espeak_SetParameter(espeakVOLUME, g_volume, 0);
    return curry_void();
}

/* ---- (tts-set-voice! name) ---- */
static curry_val fn_tts_set_voice(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_string(av[0]))
        curry_error("tts-set-voice!: expected string voice name");
    const char *name = curry_string(av[0]);
    strncpy(g_voice, name, sizeof(g_voice) - 1);
    g_voice[sizeof(g_voice) - 1] = '\0';
    if (g_mode != MODE_NONE) {
        espeak_ERROR rc = espeak_SetVoiceByName(g_voice);
        if (rc != EE_OK)
            curry_error("tts-set-voice!: voice \"%s\" not found", g_voice);
    }
    return curry_void();
}

/* ---- (tts-voices) -> list of voice-name strings ---- */
static curry_val fn_tts_voices(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    if (g_mode == MODE_NONE) ensure_playback();
    const espeak_VOICE **vlist = espeak_ListVoices(NULL);
    curry_val result = curry_nil();
    if (!vlist) return result;
    int n = 0;
    while (vlist[n]) n++;
    for (int i = n - 1; i >= 0; i--) {
        if (vlist[i]->name)
            result = curry_make_pair(curry_make_string(vlist[i]->name), result);
    }
    return result;
}

/* ---- (tts-voice-table) -> list of (name . identifier) pairs ---- */
static curry_val fn_tts_voice_table(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    if (g_mode == MODE_NONE) ensure_playback();
    const espeak_VOICE **vlist = espeak_ListVoices(NULL);
    curry_val result = curry_nil();
    if (!vlist) return result;
    int n = 0;
    while (vlist[n]) n++;
    for (int i = n - 1; i >= 0; i--) {
        if (vlist[i]->name && vlist[i]->identifier) {
            curry_val pair = curry_make_pair(
                curry_make_string(vlist[i]->name),
                curry_make_string(vlist[i]->identifier));
            result = curry_make_pair(pair, result);
        }
    }
    return result;
}

/* ---- Module init ---- */
void curry_module_init(CurryVM *vm) {
#define DEF(n, f, mn, mx) curry_define_fn(vm, n, f, mn, mx, NULL)
    DEF("tts-speak",       fn_tts_speak,       1, 2);
    DEF("tts->pcm",        fn_tts_to_pcm,      1, 1);
    DEF("tts-sample-rate", fn_tts_sample_rate, 0, 0);
    DEF("tts-set-rate!",   fn_tts_set_rate,    1, 1);
    DEF("tts-set-pitch!",  fn_tts_set_pitch,   1, 1);
    DEF("tts-set-volume!", fn_tts_set_volume,  1, 1);
    DEF("tts-set-voice!",  fn_tts_set_voice,   1, 1);
    DEF("tts-voices",      fn_tts_voices,      0, 0);
    DEF("tts-voice-table", fn_tts_voice_table, 0, 0);
#undef DEF

    curry_define_val(vm, "tts-rate-min",    curry_make_fixnum(espeakRATE_MINIMUM));
    curry_define_val(vm, "tts-rate-max",    curry_make_fixnum(espeakRATE_MAXIMUM));
    curry_define_val(vm, "tts-rate-normal", curry_make_fixnum(espeakRATE_NORMAL));
}
