# Language Pack System — `(curry lang)`

Curry Scheme supports pluggable language packs that let programmers write code
in languages other than English. Special-form names, built-in procedure names,
and error messages can all be translated. Only one language pack is active at a
time; Akkadian is pre-registered at startup and is the built-in default.

---

## System Overview

The translation pipeline has two layers:

**Special forms** — at eval time, before dispatching on the operator of any
form, `eval()` calls `lang_translate(sym)`. If a mapping exists for that
symbol in the active pack, the canonical English symbol is substituted. This
happens before the special-form switch, so translated names work identically to
their English counterparts:

```scheme
(set-active-language! "irish")
(sainmhínigh x 42)   ; same as (define x 42)
(taispeáin x)        ; same as (display x)
```

**Procedures** — when a language is activated, `env_define` is called for each
mapped name in the global environment, binding the translated symbol to the same
value as the canonical one. This makes translated names usable as first-class
procedure values:

```scheme
(mapáil (feidhm (x) (* x x)) '(1 2 3))
; => (1 4 9)
```

Both layers are populated at activation time. Loading a pack with
`register-language!` while that pack is already active immediately applies its
aliases; switching packs with `set-active-language!` applies the new pack's
aliases and removes nothing (the runtime makes no attempt to un-bind old names).

### Translation timing

```
(sainmhínigh x 42)
       │
       ▼
  eval(form, env)
       │
       ├─ lang_translate('sainmhínigh) → 'define      ; hot-path, lock-free
       │
       ├─ dispatch on 'define          ; normal special-form handling
       │
       └─ bind x = 42 in env
```

Translation (`lang_translate`) is on the hot path and is lock-free.
`g_active` is `_Atomic(LangPack *)` with acquire/release semantics. Registration
and activation take `g_lang_mtx`; `env_define` is called outside that lock to
avoid lock-order hazards with the environment mutex.

---

## Built-in Procedures

These are registered unconditionally in `builtins.c` and available without
importing any module.

### `(register-language! spec)`

Register a language pack. `spec` is an association list with the following keys:

| Key | Type | Description |
|-----|------|-------------|
| `id` | string | Unique identifier, e.g. `"irish"` |
| `display-name` | string | Human-readable name, e.g. `"Gaeilge (Modern Irish)"` |
| `intro` | string | Message printed when the language is activated |
| `error-preamble` | string | Prefix for runtime error messages |
| `mappings` | list | List of `(foreign canonical "note")` triples |

Each mapping triple is `(foreign-symbol canonical-symbol "human-readable note")`.
The note is for humans — teachers, linguists, tooling — and is not used by the
runtime.

If a pack with the same `id` is already registered it is replaced. If that pack
is currently active, procedure aliases are refreshed immediately.

```scheme
(register-language!
  `((id           . "tok-pisin")
    (display-name . "Tok Pisin")
    (intro        . "Tok Pisin i wok nau.")
    (error-preamble . "Bagarap:")
    (mappings .
      ((stretim  define  "give a name, make it straight")
       (wokim    lambda  "make, construct — a function makes something")
       (sapos    if      "suppose — conditional")))))
```

### `(set-active-language! id-or-#f)`

Activate a registered language pack by its `id` string. Pass `#f` to deactivate
and fall back to English names only. The `intro` string is displayed on
activation (to the current output port).

```scheme
(set-active-language! "irish")    ; activates Irish
(set-active-language! #f)         ; back to English
```

### `(active-language)`

Returns the `id` string of the currently active pack, or `#f` if no language
pack is active (English / no translation).

```scheme
(active-language)   ; => "irish"
```

### `(registered-languages)`

Returns a list of `id` strings for all registered packs, including the built-in
Akkadian pack.

```scheme
(registered-languages)
; => ("akkadian" "irish" "greek")
```

### `(language-info id)`

Returns an association list with keys `id`, `display-name`, `intro`,
`error-preamble`, and `mappings` for the named pack, or `#f` if no pack with
that id is registered.

```scheme
(language-info "irish")
; => ((id . "irish") (display-name . "Gaeilge (Modern Irish)") ...)
```

### `(language-intro id)`

Returns the `intro` string for the named pack, or `#f` if not found. Useful for
displaying a welcome message without activating the language.

---

## `(curry lang)` Module Procedures

Import with `(import (curry lang))`. These procedures handle fetching, caching,
and generating language pack files.

### `lang:registry-url`

A string: the base URL from which packs are fetched. Baked in at CMake
configure time using the current git branch name, so it always points to the
correct branch of the Curry repository:

```
https://raw.githubusercontent.com/deconstructo/curry/main/langs
```

Override with the environment variable `CURRY_LANG_REGISTRY_URL`.

### `lang:cache-dir`

A string: the local directory where installed packs are stored.
Defaults to `~/.curry/langs/`. Override by setting `HOME`.

### `(lang:list-available)`

Fetches `index.scm` from `lang:registry-url`, evaluates it, and returns the
`lang-index` list declared there. Requires network access.

```scheme
(import (curry lang))
(lang:list-available)
; => (("irish" "Gaeilge (Modern Irish)")
;     ("greek" "Ελληνική Κλασική (Classical Greek)")
;     ("latin" "Latina Classica (Classical Latin)")
;     ("warlpiri" "Warlpiri (Yapa)"))
```

### `(lang:fetch id)`

Fetches the raw `.scm` source for language pack `id` from the registry and
returns it as a string. Does not install or load it. Raises on network error.

### `(lang:install! id)`

Fetches the pack and saves it to `lang:cache-dir` as `id.scm`. Does not activate
it. Returns the local file path. Requires network access; subsequent `lang:load!`
calls work offline.

```scheme
(lang:install! "greek")   ; => "/Users/you/.curry/langs/greek.scm"
```

### `(lang:load! id)`

Loads a previously installed pack from `lang:cache-dir`. Raises if the pack has
not been installed. Does not activate it; call `(set-active-language! id)` after
loading if you want to activate it.

```scheme
(lang:load! "greek")
(set-active-language! "greek")
```

### `(lang:load-file! path)`

Loads a language pack from any local `.scm` file path. The file is read and
evaluated in the interaction environment. The input port is closed even if `eval`
raises, so it is safe to use with partial or experimental pack files.

```scheme
(lang:load-file! "langs/warlpiri.scm")
```

### `(lang:generate spec)`

Generates a `.scm` source string from an alist spec in the same format as
`register-language!`. Returns the string; does not write to disk. Intended for
tooling that builds pack files programmatically.

```scheme
(display (lang:generate
  `((id . "esperanto")
    (display-name . "Esperanto")
    (intro . "Saluton!")
    (error-preamble . "Eraro:")
    (mappings . ((difini define "give definition")
                 (se     if     "se — if"))))))
```

---

## Error Preamble

When a language pack is active, runtime errors use that pack's `error-preamble`
string in place of the built-in Akkadian preamble. Compare:

```
; No active language (default Akkadian):
𒀭 ḫiṭītu — unbound variable: x

; With Irish active:
Earráid mhór: unbound variable: x

; With Greek active:
Ἁμαρτία μεγάλη: unbound variable: x

; With Latin active:
Error magnus: unbound variable: x
```

---

## Writing a Language Pack

A language pack is a plain `.scm` file that calls `register-language!`. The
format is self-contained and readable by anyone who knows the target language —
no programming background is needed to review or correct the vocabulary.

### The `.scm` format

```scheme
(import (curry lang))

(register-language!
  `((id           . "your-language-id")
    (display-name . "Your Language Name")
    (intro        . "A welcome message in your language.")
    (error-preamble . "Error prefix in your language:")
    (mappings     .
      ((your-word-for-define  define  "cultural/conceptual note")
       (your-word-for-lambda  lambda  "cultural/conceptual note")
       ...))))
```

The third element of each mapping triple is a note for humans — explaining the
etymology, cultural significance, or translation reasoning. It is not used by
the runtime. Descriptions in the notes column are how the pack teaches the
language, not how the runtime interprets it.

### Using `tools/lang-pack-gen`

The `lang-pack-gen` tool generates a `.scm` pack file from a three-column CSV:

```
indigenous_name,english_canonical,description
yirdi,lambda,pattern / way of doing
nyinaja,define,give a name to
kuja,if,when / if (subordinating conjunction)
```

The header row is required; the description column is optional.

```bash
curry tools/lang-pack-gen \
  --id warlpiri \
  --display-name "Warlpiri (Yapa)" \
  --intro "Yapa yimi Warlpiri kurlangu!" \
  --error-preamble "Ngurra-kurlu karlipa yimi:" \
  --csv warlpiri.csv \
  --output langs/warlpiri.scm
```

If `--output` is omitted, the generated source is written to stdout.

This tool is itself a Curry Scheme script (`#!/usr/bin/env curry`) so it runs
anywhere Curry is installed.

---

## The Registry

`lang:list-available` fetches `langs/index.scm` from the registry URL. That
file declares a `lang-index` list:

```scheme
(define lang-index
  '(("irish"    "Gaeilge (Modern Irish)")
    ("greek"    "Ελληνική Κλασική (Classical Greek)")
    ("latin"    "Latina Classica (Classical Latin)")
    ("warlpiri" "Warlpiri (Yapa)")))
```

`lang:install!` then fetches `langs/irish.scm`, `langs/greek.scm`, etc. from
the same base URL.

The registry URL is baked in at CMake configure time using
`git rev-parse --abbrev-ref HEAD`, so a build from the `main` branch fetches
from `.../curry/main/langs/`, and a build from a feature branch fetches from
that branch's `langs/` directory. This means packs under development are always
matched to their corresponding runtime.

Override the registry URL at any time with `CURRY_LANG_REGISTRY_URL`:

```bash
CURRY_LANG_REGISTRY_URL="file:///path/to/local/langs" curry my-script.scm
```

---

## Thread Safety

- `g_active` — `_Atomic(LangPack *)` with acquire/release ordering. Translation
  (`lang_translate`) reads this lock-free on every eval cycle.
- `g_lang_mtx` — a mutex protecting the registry (the list of all registered
  packs) and the activation operation. `register-language!` and
  `set-active-language!` both acquire this mutex.
- `env_define` — called outside `g_lang_mtx` when installing procedure aliases,
  to avoid a lock-order hazard with the environment's own lock.

Consequence: it is safe to call `lang_translate` from any thread concurrently
with another thread registering or activating a language. The worst case is that
a thread reads a stale `g_active` for one eval cycle before the new pack
becomes visible.

---

## Example Session

```scheme
;;; Download and install
(import (curry lang))
(lang:install! "irish")

;;; Load and activate
(lang:load! "irish")
(set-active-language! "irish")
; Fáilte! Tá an Ghaeilge gníomhach anois.

;;; Write in Irish
(sainmhínigh cearnach (feidhm (n) (* n n)))
(taispeáin (cearnach 7))
; 49

;;; Actors in Irish
(sainmhínigh aisteoir (beir (feidhm () (receive (msg) (taispeáin msg)))))
(seol aisteoir "dia duit")
; dia duit

;;; Switch back to English
(set-active-language! #f)
(define y 100)    ; works again as normal
```

---

## See Also

- [`akkadian-reference.md`](akkadian-reference.md) — complete listing of all
  303 Akkadian/cuneiform synonyms built into the runtime
- [`lang-packs.md`](lang-packs.md) — guide to the five available language packs
  with translation philosophy and cultural background
