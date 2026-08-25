# Module: `(curry piper)`

*unreleased*

Native bindings to [Piper](https://github.com/OHF-Voice/piper1-gpl) (`libpiper`), a neural text-to-speech engine — wired into `(curry tts)` as the `'piper` backend (`(curry tts piper)`). Unlike `(curry tts)`'s other two backends (`'macos-say`, `'espeak-ng`, both plain command-line tools spawned through `(curry posix)`), this is a real C module: `libpiper`'s own C API streams synthesized audio in chunks via structs with raw `float*` sample arrays (`piper_audio_chunk`), which is exactly the "deep struct traversal" case `docs/reference/module-ffi.md` itself says to write a C module for rather than force through the generic FFI layer.

## Why a C module, not FFI

`(curry ffi)` is for simple scalar-in/scalar-out calls with no struct traversal. `libpiper` needs:
- `piper_synthesize_next` filling a 9-field struct per call (sample data, phoneme alignment arrays, an `is_last` flag) in a streaming loop until synthesis is done.
- `piper_default_synthesize_options`/`piper_create_with_options` passing structs by value/pointer, one of them a versioned struct (`struct_size` field).

Building this through curry's generic FFI primitives (`peek-bytes`, pinned bytevectors, manual struct-offset arithmetic) would be slower, harder to get right, and harder to review than a real C module wrapping the same six functions directly.

## Build / install

`libpiper` has no system package yet (no `apt`/`brew` formula) — build it from source:

```bash
git clone https://github.com/OHF-Voice/piper1-gpl
cd piper1-gpl/libpiper
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/install
cmake --build build --config Release
cmake --install build --config Release
```

This automatically downloads `onnxruntime`'s shared libraries and builds `espeak-ng` from source (used for phonemization) — no separate install step for either. `install/include/piper.h` and `install/lib/libpiper.*` + `libonnxruntime.*` are what curry's own build needs next.

Then configure curry itself:

```bash
cmake -B build -DBUILD_MODULE_PIPER=ON -DPIPER_ROOT=/path/to/piper1-gpl/libpiper/install
cmake --build build
```

`PIPER_ROOT` is only a search hint (`find_path`/`find_library` `HINTS`) — if you installed `libpiper` to a default system prefix instead, omit it. If either `piper.h`, `libpiper`, or `libonnxruntime` can't be found, configure fails with a clear message pointing back at the steps above, rather than silently skipping the module.

**Linux only:** `libasound`/`alsa-lib` (`libasound2-dev` on Debian/Ubuntu, `alsa-lib-devel` on Fedora) is needed for direct-to-speaker playback (`tts-speak`/`piper-speak-async`) specifically — `piper-save` (WAV file output) works without it. If ALSA isn't found, curry's configure step prints a warning (not a hard failure) and `piper-speak-async` raises a clear runtime error instead of crashing; everything else still builds and works.

**macOS:** direct-to-speaker playback uses CoreAudio's `AudioQueue` API — a system framework, always present, nothing extra to install.

## Voice models

Download a voice with Piper's own tooling (needs Python + `pip install piper-tts`, but only for this one-time download step — nothing at runtime imports Python):

```bash
python3 -m piper.download_voices          # list available voices
python3 -m piper.download_voices en_US-lessac-medium
```

This downloads `en_US-lessac-medium.onnx` + `en_US-lessac-medium.onnx.json` into the current directory by default. `(current-piper-voice-dir)` (a parameter, same idiom as every other `(curry tts)` setting) controls where curry looks for voices — defaults to `(current-directory)` at import time, matching Piper's own `--data-dir` default exactly.

## Import

```scheme
(import (curry piper))          ; low-level primitives
(import (curry tts piper))      ; registers 'piper into (curry tts)'s backend table
(import (curry tts))            ; then use tts-speak/tts-save/etc. with #:backend 'piper
```

`(curry tts)` does **not** unconditionally import `(curry tts piper)` the way it does the macOS/espeak-ng backends, since this library may not exist at all depending on how curry was built — `(import (curry tts piper))` explicitly is what registers it (see `(curry tts)`'s own doc for the registry design this required).

```scheme
(import (curry tts) (curry tts piper))
(current-piper-voice-dir "/home/me/piper-voices")
(tts-speak "hello from piper" #:backend 'piper #:voice "en_US-lessac-medium")
```

## Low-level API (`(curry piper)`)

```scheme
(piper-create model-path config-path-or-#f espeak-data-path-or-#f) -> synth
(piper-synth? v) -> bool
(piper-free! synth) -> void
(piper-version) -> string

(piper-speak-async synth text speaker-id-or-#f length-scale-or-#f) -> handle
(piper-handle? v) -> bool
(piper-wait h) -> void       ; blocks until playback finishes
(piper-stop! h) -> void      ; cooperative -- signals the playback loop to stop
                              ; between chunks, doesn't forcibly kill anything
(piper-alive? h) -> bool

(piper-save synth text path speaker-id-or-#f length-scale-or-#f) -> void  ; blocking
```

`piper-speak-async` synthesizes on a background thread and feeds each audio chunk directly to the platform's audio output device as it's produced (CoreAudio `AudioQueue` on macOS, ALSA on Linux) — no temp file, no external player process. `piper-save` writes a 16-bit PCM WAV file instead, blocking, no audio device involved.

**`piper-wait` is also where a `piper-speak-async` handle's small backing struct (including its mutex/condition-variable) actually gets freed** — call it exactly once per handle, the same "must be released, no GC finalizer" contract `piper-free!` already has for a `piper-synth`. A handle that's never waited on (pure fire-and-forget, calling only `piper-stop!`/`piper-alive?` or nothing further) leaks that same small, fixed-size struct until process exit — there's no other point in the API where "nothing will touch this handle again" can be determined safely. `(curry tts)`'s own `tts-speak` (`tts-speak-async` + a wait) always takes the correct path automatically; this only matters if you call `piper-speak-async` directly and skip `piper-wait`.

### Threading and GC safety

The background thread `piper-speak-async` spawns never constructs a `curry_val` or touches any GC-heap pointer — the text is copied with `strdup` (not borrowed via `curry_string`'s GC-heap pointer) before the thread starts, so the thread has no dependency on curry's GC being aware of it at all (unlike e.g. `modules/mcp/mcp.c`'s per-connection threads, which *do* construct `curry_val`s off the main thread and therefore *do* need `gc_register_thread()`). Everything the background thread produces for the calling thread to read (`done`/`stop_requested`/error state) is plain C state behind a mutex; `piper-wait`/`piper-alive?` only ever build `curry_val`s back on the calling (VM) thread, after reading that state.

## `(curry tts piper)` — the `(curry tts)` backend

Registers `'piper` into `(curry tts)`'s backend table as a side effect of being imported (see above). Two real differences from `'macos-say`/`'espeak-ng`, both consequences of Piper being a neural model loaded from a file rather than a system command:

- **`#:voice`** means an `.onnx` model file's basename (e.g. `"en_US-lessac-medium"`), discovered by listing `(current-piper-voice-dir)` for `*.onnx` files — there's no runtime "list installed voices" command the way `espeak-ng --voices` gives one. `tts-voices #:backend 'piper` returns `(name . #f)` pairs (no locale metadata available without parsing each voice's own `.onnx.json` config). Loaded synthesizers are cached by voice name, since reloading an ONNX model on every `tts-speak` call would be real, avoidable cost the other two backends don't have (spawning `say`/`espeak-ng` fresh each call is cheap by comparison).
- **`#:rate`** (words per minute, same unit as the other two backends) has no native Piper equivalent — Piper's own knob is `length_scale`, a speed *multiplier* (0.5 = twice as fast). `(curry tts piper)` converts using a fixed reference-WPM constant (150), which is necessarily a heuristic, not an exact WPM the way it genuinely is for `say`/`espeak-ng`.

`tts-speak`/`tts-wait`/`tts-stop`/`tts-speaking?` all work identically to the other two backends despite `piper-speak-async`'s handle not being a real OS process — `(curry tts)`'s own dispatch checks `process-handle?` first (the common case, unchanged for `'macos-say`/`'espeak-ng`), falling back to whichever registered backend's own `handle?` predicate recognizes the handle otherwise.

## See also

- [`module-tts.md`](module-tts.md) — the full `(curry tts)` API this backend plugs into
- [`module-ffi.md`](module-ffi.md) — "When to use the FFI vs writing a C module," the design question this whole module answers
