# Module: (curry tts)

*unreleased*

Cross-backend text-to-speech. Two backends today: `'macos-say` (macOS's built-in `say` command) and `'espeak-ng` (Linux, via `espeak-ng` or the older `espeak` binary name). Both are plain command-line tools run through `(curry posix)`'s `process-run`/`process-start` — no Objective-C, no framework linking, no new C module, and no shell-injection surface: `process-run`/`process-start` are backed by `posix_spawn` directly, never a shell, so the text to speak, voice names, and file paths all pass through as literal argv data, never interpreted by anything.

Install: `say` ships with macOS — nothing to install. On Linux, `apt install espeak-ng` (Debian/Ubuntu) or the equivalent for your distro; the older `espeak` package also works if that's what's available.

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

A step removed from `current-tts-voice`: `say`/`espeak-ng` both take a *voice name*, not a locale, so this is never passed to the backend directly. Instead, when a call resolves no `#:voice` (neither the call's own nor `current-tts-voice`), the active backend's `tts-voices` list is searched for the first entry whose locale starts with `lang`, and that voice's name is used. Lets you say `(current-tts-language "fr")` once instead of knowing the exact voice name (`"Thomas"` on macOS, `"French_(France)"` on espeak-ng) a given backend/machine happens to expose. Raises `'tts-error` if no voice matches. An explicit `#:voice` (call-level or `current-tts-voice`) always takes priority over `current-tts-language`.

```scheme
(current-tts-voice #f)
(current-tts-language "en")
(tts-speak "hello world")     ; picks a matching voice automatically
(tts-speak "bonjour" #:voice "Thomas")   ; explicit #:voice still wins
```

### `(tts-backends)` → list of symbols

The full set of registered backends, e.g. `(macos-say espeak-ng)` — the same two on every platform; only their *availability* differs.

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
(tts-save "hello" "greeting.wav" #:backend 'espeak-ng #:voice "en")
```

## Voices

### `(tts-voices . kwargs)` → list of `(name . locale) `

For the active (or `#:backend`-forced) backend. `name` is exactly what that backend expects back as a `#:voice` value.

```scheme
(tts-voices)
; => (("Alice" . "it_IT") ("Moira (English (Ireland))" . "en_IE") ...)   ; macOS
(tts-voices #:backend 'espeak-ng)
; => (("English_(America)" . "en-us") ("French_(France)" . "fr-fr") ...)  ; espeak-ng
```

Note the two backends use different `name` and `locale` conventions (macOS: display names, `xx_XX` locale tags; espeak-ng: underscore-separated names, `xx-xx`/`xx` language tags) — a `#:voice` value is never portable between backends, only within one.

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

- [`module-posix.md`](module-posix.md) — `process-run`/`process-start`, the subprocess primitives this module is built on
- [`module-sql.md`](module-sql.md) — the other cross-backend layer in curry, similar shape (explicit backend selection, one shared API surface)
