# Module: (curry tts)

*unreleased*

Cross-backend text-to-speech. Three backends: `'macos-say` (macOS's built-in `say` command), `'espeak-ng` (Linux, via `espeak-ng` or the older `espeak` binary name), and `'piper` (neural TTS via `libpiper`, see [`module-piper.md`](module-piper.md)). The first two are plain command-line tools run through `(curry posix)`'s `process-run`/`process-start` — no Objective-C, no framework linking, no new C module, and no shell-injection surface: `process-run`/`process-start` are backed by `posix_spawn` directly, never a shell, so the text to speak, voice names, and file paths all pass through as literal argv data, never interpreted by anything. `'piper` is different — a real C module (`(curry piper)`) doing native audio output, and (unlike the other two) not compiled in by default; see [`module-piper.md`](module-piper.md) for why and how to enable it.

Install: `say` ships with macOS — nothing to install. On Linux, `apt install espeak-ng` (Debian/Ubuntu) or the equivalent for your distro; the older `espeak` package also works if that's what's available. `'piper` needs a separate build step — see [`module-piper.md`](module-piper.md).

## Backend registration

`(curry tts)`'s backend table was originally a small fixed set (`'macos-say`/`'espeak-ng`, both always compiled in) with no registration mechanism. `'piper` doesn't fit that shape — `(curry tts piper)` may not exist as an importable library at all, depending on whether curry was built with `-DBUILD_MODULE_PIPER=ON`, so `(curry tts)` can't unconditionally import it the way it does the other two (that import would fail outright — unbound library — on an ordinary build). The registry is now open: `(import (curry tts piper))` registers `'piper` into the table as a side effect of the import (via `tts-register-backend!`, exported alongside the widened `make-tts-backend` constructor for any future optionally-compiled backend to use the same way).

### `(make-tts-backend available? speak-async save voices)`
### `(make-tts-backend available? speak-async save voices wait stop speaking?)`

The `<tts-backend>` record constructor. `wait`/`stop`/`speaking?` are optional, defaulting to `#f` — `#f` means "this backend's own `speak-async` returns an ordinary `(curry posix)` process handle, dispatch `tts-wait`/`tts-stop`/`tts-speaking?` to `process-wait`/`process-kill`/`process-alive?` directly" (`'macos-say`/`'espeak-ng`'s own case, unchanged by this). Supply real procs only if `speak-async`'s return value *isn't* a process handle — `'piper`'s own `speak-async` is a background pthread doing native audio output, not a subprocess, so it supplies `piper-wait`/`piper-stop!`/`piper-alive?` here. A backend that supplies these must also register a fourth "is this my handle" predicate (see `(curry piper)`'s own `piper-handle?`) — `tts-wait`/`tts-stop`/`tts-speaking?` check `process-handle?` first (the fast, common-case path, zero registry lookup) and only search the registry for a matching backend when that's `#f`.

### `(tts-register-backend! sym backend)`

Adds `backend` (a `<tts-backend>` from `make-tts-backend`) under `sym` into the table `tts-backends`/`tts-backend-available?`/`%lookup-backend` all read from. Not something ordinary code needs to call directly — it's what a library like `(curry tts piper)` calls on itself at import time.

## Import

```scheme
(import (curry tts))
```

## Backend selection

A caller can let the backend auto-select or force one explicitly — this is not just an auto-detected choice, since you may genuinely want a specific engine (e.g. `espeak-ng` on macOS if it's installed) regardless of platform.

### `(current-tts-backend)` → symbol
### `(current-tts-backend sym)`

A real parameter object (`make-parameter`, the same "call with no args to get, one argument to set" idiom `current-number-notation` already uses): `(current-tts-backend)` reads the active default, `(current-tts-backend 'espeak-ng)` sets it permanently for the rest of the session, and `(parameterize ((current-tts-backend 'espeak-ng)) ...)` scopes an override to a dynamic extent. Seeded at import time from `(os-name)` — `'macos-say` on Darwin, `'espeak-ng` everywhere else.

Every procedure below also accepts a `#:backend sym` keyword argument for a one-off override, without touching `current-tts-backend` at all.

### `(current-tts-voice)` → string or `#f`
### `(current-tts-voice name)`

Same parameter idiom, for `#:voice`: unset (`#f`) by default, meaning "let the backend pick its own default voice." Set it once and every subsequent `tts-speak`/`tts-save`/`tts-speak-async` call that doesn't pass its own `#:voice` uses it.

### `(current-tts-rate)` → exact positive integer or `#f`
### `(current-tts-rate n)`

Same idiom, for `#:rate` (words per minute).

### `(current-tts-language)` → string or `#f`
### `(current-tts-language lang)`

A step removed from `current-tts-voice`: `say`/`espeak-ng` both take a *voice name*, not a locale, so this is never passed to the backend directly. Instead, when a call resolves no `#:voice` (neither the call's own nor `current-tts-voice`), the active backend's `tts-voices` list is searched for the first entry whose locale starts with `lang`, and that voice's name is used. Lets you say `(current-tts-language "fr")` once instead of knowing the exact voice name (`"Thomas"` on macOS, `"fr-fr"` on espeak-ng) a given backend/machine happens to expose. Raises `'tts-error` if no voice matches. An explicit `#:voice` (call-level or `current-tts-voice`) always takes priority over `current-tts-language`.

```scheme
(current-tts-voice #f)
(current-tts-language "en")
(tts-speak "hello world")     ; picks a matching voice automatically
(tts-speak "bonjour" #:voice "Thomas")   ; explicit #:voice still wins
```

### `(tts-backends)` → list of symbols

The full set of registered backends, e.g. `(macos-say espeak-ng)` — `'macos-say`/`'espeak-ng` are always both registered regardless of platform (only their *availability* differs); `'piper` only appears here at all once `(curry tts piper)` has been imported (see "Backend registration" above).

### `(tts-backend-available? sym)` → boolean

A `PATH` lookup only — never spawns a process. `'macos-say` is available only on macOS; `'espeak-ng` is available wherever `espeak-ng` or `espeak` is on `PATH`. Unlike every other procedure below, an unrecognized `sym` here returns `#f` rather than raising — a pure query naturally answers "no" for something that doesn't exist, rather than treating it as a usage error.

## Speaking

### `(tts-speak text . kwargs)`

Blocks until the utterance finishes playing through the system's default audio output device.

```scheme
(import (curry tts))
(tts-speak "hello world")
(tts-speak "hello" #:voice "Alice" #:rate 200)
(tts-speak "hello" #:backend 'espeak-ng)
```

- `#:voice name` — validated against the active backend's own `tts-voices` output before being passed through; an unrecognized name raises `'tts-error` rather than being trusted blindly (see [Errors](#errors)).
- `#:rate n` — words per minute (`say -r`/`espeak-ng -s`); must be a positive exact integer, or `'tts-error` is raised rather than passing something the underlying CLI can't parse straight through.
- `#:backend sym` — one-off override of `current-tts-backend` for this call only.

### `(tts-speak-async text . kwargs)` → process handle

Same options, non-blocking — returns immediately while the utterance plays in the background. The returned handle is an ordinary `(curry posix)` process handle.

### `(tts-wait h)`

Blocks until the utterance started by `tts-speak-async` finishes.

### `(tts-stop h)`

Cancels a still-playing utterance immediately (sends `SIGTERM` to the underlying process). `tts-stop` alone doesn't reap the process — pair it with `tts-wait` (as in the example below), same as `(curry posix)`'s own `process-kill`/`process-wait` pairing; a `tts-stop` with no follow-up `tts-wait` leaves a zombie process for the life of the curry process.

### `(tts-speaking? h)` → boolean

Non-blocking check — `#t` while `h` is still playing.

```scheme
(define h (tts-speak-async "a long paragraph worth of text..."))
(tts-speaking? h)   ; => #t
(tts-stop h)         ; cancel it early
(tts-wait h)
(tts-speaking? h)   ; => #f
```

## Rendering to a file

### `(tts-save text path . kwargs)`

Same `#:voice`/`#:rate`/`#:backend` options as `tts-speak`, but renders to `path` instead of playing anything — no sound at all. Blocks until the file is written. Format follows each backend's own default (AIFF for `say`, WAV for `espeak-ng`); pass a backend-specific extension in `path` to match.

```scheme
(tts-save "hello world" "greeting.aiff")
(tts-save "hello" "greeting.wav" #:backend 'espeak-ng #:voice "en-us")
```

## Voices

### `(tts-voices . kwargs)` → list of `(name . locale) `

For the active (or `#:backend`-forced) backend. `name` is exactly what that backend expects back as a `#:voice` value.

```scheme
(tts-voices)
; => (("Alice" . "it_IT") ("Moira (English (Ireland))" . "en_IE") ...)   ; macOS
(tts-voices #:backend 'espeak-ng)
; => (("en-us" . "en-us") ("fr-fr" . "fr-fr") ("ga" . "ga") ...)  ; espeak-ng
```

Note the two backends use different `name` conventions: macOS's `say` uses real display names distinct from locale (`"Moira (English (Ireland))"` for `en_IE`, `"Thomas"` for `fr_FR`), so both fields carry distinct information there — but espeak-ng's own `-v` flag only ever accepts its Language column, never its VoiceName column (confirmed directly: passing a VoiceName like `"English_(America)"` fails with "the specified espeak-ng voice does not exist"), so both `name` and `locale` are the same language-tag string for that backend (`"en-us"`, `"fr-fr"`, `"ga"`, ...). A `#:voice` value is never portable between backends either way, only within one.

## Errors

### `tts-error` condition (field: `backend`)

Raised (via `(curry conditions)`) for: an unknown `#:backend` symbol, an unrecognized `#:voice` value, or a non-zero exit from the underlying `say`/`espeak-ng` process. `condition-field`'s `backend` names which backend the failure came from.

```scheme
(import (curry tts) (curry conditions))
(guard (e (#t (display (condition-field e 'backend)) (newline)))
  (tts-speak "hi" #:backend 'not-a-real-backend))
; => not-a-real-backend
```

## Notes

- Neither backend supports capturing spoken audio as an in-memory bytevector — only playback through the default audio device or rendering to a file on disk.
- `tts-voices`/`tts-save`/`tts-speak` all spawn a real subprocess per call (`say -v ?`/`espeak-ng --voices` to list voices); there's no persistent "connection" or daemon the way `(curry sql)`'s backends have, since neither `say` nor `espeak-ng` needs one. Note `tts-speak`/`tts-save`/`tts-speak-async` spawn **two** subprocesses, not one, whenever a voice ends up in play — either `#:voice`/`current-tts-voice` given directly (validated against a live `tts-voices` listing before speaking/rendering), or `current-tts-language` set (which itself calls `tts-voices` once to resolve a matching name — that name is already known-good off that same list, so it isn't re-validated against a second fetch). Leave voice/language unset (use the backend's own default) if that per-call cost matters for a tight loop.
- On macOS, a voice's own display name can contain spaces and nested parentheses (e.g. `"Moira (English (Ireland))"`) — `tts-voices`' own parser locates each line's locale tag (always `xx_XX`) via regex rather than splitting on whitespace, specifically to handle this correctly.

## See also

- [`tts-piper.md`](../guides/tts-piper.md) — narrative walkthrough for the `'piper` backend: building on macOS/Linux, speaking, sourcing and adding voices
- [`module-posix.md`](module-posix.md) — `process-run`/`process-start`, the subprocess primitives this module is built on
- [`module-sql.md`](module-sql.md) — the other cross-backend layer in curry, similar shape (explicit backend selection, one shared API surface)
