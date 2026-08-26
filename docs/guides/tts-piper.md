# Text-to-speech with Piper: building, speaking, and adding voices

This guide walks through getting neural TTS working end-to-end with curry's
`'piper` backend: building `libpiper` itself, building curry against it on
macOS and Linux, speaking/saving audio through `(curry tts)`, and sourcing or
adding new voice models.

For the exhaustive API reference, see [`module-piper.md`](../reference/module-piper.md)
(low-level `(curry piper)` primitives) and [`module-tts.md`](../reference/module-tts.md)
(the cross-backend `(curry tts)` layer piper plugs into). This guide is the
narrative "how do I actually do this" version; those two are the field-level
lookup.

---

## Why a separate build step at all

Piper (`(curry piper)`, wrapping [`libpiper`](https://github.com/OHF-Voice/piper1-gpl))
is the only `(curry tts)` backend that isn't a thin wrapper around a
command-line tool. `'macos-say` and `'espeak-ng` just spawn `say`/`espeak-ng`
as subprocesses — nothing to build, nothing to link. `'piper` is a real C
module doing native audio output and streaming synthesis through `libpiper`'s
own C API, and `libpiper` has no system package yet (no `apt`/`brew` formula),
so it has to be built from source once before curry's own build can link
against it. That's the extra step below — everything after it is ordinary
curry usage.

## Part 1: Build `libpiper`

Same steps on macOS and Linux — `libpiper`'s own CMake build handles the
platform differences internally, including downloading `onnxruntime`'s shared
libraries and building `espeak-ng` from source (used for phonemization). You
don't install either of those separately.

```bash
git clone https://github.com/OHF-Voice/piper1-gpl
cd piper1-gpl/libpiper
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/install
cmake --build build --config Release
cmake --install build --config Release
```

After this, `install/include/piper.h` and `install/lib/libpiper.*` +
`libonnxruntime.*` exist — that's everything curry's own build needs next.

You can install to a default system prefix instead of a local `install/`
directory if you'd rather not pass `-DPIPER_ROOT` in Part 2 — e.g.
`-DCMAKE_INSTALL_PREFIX=/usr/local` on either platform. Whether that requires
`sudo cmake --install build` depends on write permissions on that prefix.

## Part 2: Build curry against it

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_PIPER=ON \
  -DPIPER_ROOT=/path/to/piper1-gpl/libpiper/install
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

Omit `-DPIPER_ROOT` if you installed to a default prefix like `/usr/local` —
`find_path`/`find_library` search there automatically. Direct-to-speaker
playback (`tts-speak`, `piper-speak-async`) uses CoreAudio's `AudioQueue`
API, a system framework that's always present — nothing extra to install or
link manually.

If you're combining this with the other optional modules (crypto, Qt6, LLVM,
etc.), `-DBUILD_MODULE_PIPER=ON` composes with all of them in one configure
call; see the top-level `CLAUDE.md`/README for the full flag list.

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_PIPER=ON \
  -DPIPER_ROOT=/path/to/piper1-gpl/libpiper/install
cmake --build build -j$(nproc)
```

One extra consideration on Linux: direct-to-speaker playback needs ALSA.

```bash
# Debian/Ubuntu
sudo apt install libasound2-dev
# Fedora
sudo dnf install alsa-lib-devel
```

If ALSA isn't found at configure time, curry prints a `WARNING` (not a hard
failure) — the module still builds, `piper-save` (WAV file output) still
works with no audio device involved, and only `piper-speak-async`/`tts-speak`
raise a clear runtime error instead of crashing. Install `libasound2-dev`
first if you want direct playback, since retrofitting it means reconfiguring
from scratch (CMake caches the ALSA search result).

### Confirm it worked

```bash
./build/curry -e '(import (curry piper)) (display (piper-version)) (newline)'
```

If `PIPER_ROOT`, `piper.h`, `libpiper`, or `libonnxruntime` can't be found,
configure fails immediately with a message pointing back at Part 1 — it
never silently skips the module the way some optional-dependency modules do.

## Part 3: Get a voice

Piper voices are pairs of files: `<name>.onnx` (the model) and
`<name>.onnx.json` (its config — sample rate, phoneme map, speaker count).
Both must sit in the same directory.

The easiest way to get one is Piper's own downloader (a one-time Python step —
nothing at runtime imports Python; the resulting `.onnx`/`.onnx.json` files
are all curry ever touches):

```bash
pip install piper-tts
python3 -m piper.download_voices              # list all available voices
python3 -m piper.download_voices en_US-lessac-medium
```

This drops `en_US-lessac-medium.onnx` + `.onnx.json` into the current
directory. Voice names follow Piper's own convention:
`<language>_<REGION>-<name>-<quality>`, e.g. `en_GB-alan-medium`,
`de_DE-thorsten-high`, `fr_FR-siwis-medium`. Quality tiers (`x_low`, `low`,
`medium`, `high`) trade model size/CPU cost for naturalness.

If you'd rather browse before picking, the full voice catalogue (with sample
audio for each) is published on Hugging Face at
[`rhasspy/piper-voices`](https://huggingface.co/rhasspy/piper-voices) — the
same source `download_voices` itself pulls from. You can download a pair of
`.onnx`/`.onnx.json` files directly from there with any HTTP client instead
of using the Python tool, if you'd rather not install `piper-tts` at all;
functionally identical either way, since curry only ever reads the two local
files.

### Telling curry where to find voices

`(current-piper-voice-dir)` is a parameter (same "call with no args to read,
one argument to set" idiom as `current-tts-voice`) — it defaults to the
current directory at import time, matching Piper's own `--data-dir` default.
Point it at wherever you downloaded voices:

```scheme
(import (curry tts) (curry tts piper))
(current-piper-voice-dir "/home/me/piper-voices")
(tts-voices #:backend 'piper)
; => (("en_US-lessac-medium" . #f) ("en_GB-alan-medium" . #f))
```

The `#f` in place of a locale is real — piper voices don't expose locale
metadata the way `say`/`espeak-ng` do without parsing each voice's own
`.onnx.json`, so `tts-voices` reports `#f` there for this backend
specifically.

## Part 4: Speak

```scheme
(import (curry tts) (curry tts piper))
(current-piper-voice-dir "/home/me/piper-voices")

(tts-speak "hello from piper" #:backend 'piper #:voice "en_US-lessac-medium")

;; Render to a WAV file instead of playing it
(tts-save "hello from piper" "greeting.wav"
          #:backend 'piper #:voice "en_US-lessac-medium")

;; Non-blocking, with cancellation
(define h (tts-speak-async "a long paragraph..." #:backend 'piper
                            #:voice "en_US-lessac-medium"))
(tts-speaking? h)   ; => #t
(tts-stop h)
(tts-wait h)
```

If you want piper as your default backend rather than passing `#:backend
'piper` on every call:

```scheme
(current-tts-backend 'piper)
(tts-speak "no backend argument needed now")
```

`#:rate` (words per minute) works the same as the other backends, but it's a
heuristic here — piper's native knob is `length_scale`, a speed multiplier,
and there's no exact WPM conversion for a neural model the way there
genuinely is for `say`/`espeak-ng`. Leave it unset for the model's own
natural pace unless you have a specific reason to tune it.

Once loaded, a voice's ONNX model stays cached in memory (keyed by voice
directory + name) so repeated `tts-speak` calls with the same voice don't pay
the model-load cost again — unlike `say`/`espeak-ng`, which spawn cheaply
every call, loading an ONNX model is real, avoidable cost if it happened on
every utterance.

## Part 5: Using a voice's speaker index (multi-speaker models)

Some models (e.g. many `_low` quality tiers trained on multi-speaker corpora)
expose more than one speaker in a single `.onnx` file. The high-level
`(curry tts)` API doesn't expose a `#:speaker` keyword — for that you drop
down to the low-level `(curry piper)` API directly:

```scheme
(import (curry piper))
(define synth (piper-create "/home/me/piper-voices/some-multispeaker.onnx" #f #f))
(piper-save synth "hello" "out.wav" 3 #f)   ; speaker-id 3, default length-scale
(piper-free! synth)
```

Check the voice's `.onnx.json` (`num_speakers` field) to know the valid
speaker-id range.

## Troubleshooting

**`tts: no piper voices found in <dir>`** — `(current-piper-voice-dir)` is
pointing somewhere without any `*.onnx` files, or you haven't downloaded one
yet (Part 3).

**`tts: no such piper voice: <name>`** — the `.onnx` file for that exact name
isn't in the current `(current-piper-voice-dir)`. Voice names are matched by
file basename (`<name>.onnx`), case-sensitive.

**`piper-speak-async` raises at runtime, but `piper-save` works fine** — on
Linux, this means ALSA wasn't found when curry was configured. Install
`libasound2-dev`/`alsa-lib-devel` and reconfigure+rebuild from scratch (see
Part 2).

**Process aborts (`SIGABRT`) on exit after using piper** — this means a
`piper-synth` from `piper-create` was never released with `piper-free!`, and
`onnxruntime`'s process-global singleton crashed the process at exit while a
synthesizer was still alive. `(curry tts piper)`'s own `%get-synth` cache
keeps synths alive for the process lifetime deliberately, so this normally
only bites low-level `(curry piper)` usage that calls `piper-create` directly
without a matching `piper-free!` — always pair them (see Part 5's example).
There's an `atexit()` safety net that force-frees anything still outstanding,
so an occasional missed `piper-free!` degrades to a leak rather than a
guaranteed crash, but don't rely on it.

**Espeak-ng phonemization data not found** — `(curry tts piper)` auto-locates
`espeak-ng-data` at import time by checking
`/opt/homebrew/share/espeak-ng-data`, `/usr/local/share/espeak-ng-data`, and
`/usr/share/espeak-ng-data` in that order. If yours is somewhere else (e.g. a
non-standard `libpiper` install prefix), set it explicitly:

```scheme
(current-piper-espeak-data-path "/path/to/espeak-ng-data")
```

## See also

- [`module-piper.md`](../reference/module-piper.md) — full `(curry piper)` API reference
- [`module-tts.md`](../reference/module-tts.md) — full `(curry tts)` cross-backend API reference
- [`module-ffi.md`](../reference/module-ffi.md) — why this is a C module rather than built on `(curry ffi)`
