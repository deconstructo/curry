# Module: `(curry ncurses)`

*unreleased*

Terminal UI via ncurses — pure Scheme + `(curry ffi)`, no C build step. Requires `BUILD_FFI=ON` at curry's own build time (links libffi) and ncurses installed on the *running* system, `dlopen`'d at import time following the same pattern as `(curry hdf5)`.

## Build / install

Requires `BUILD_FFI=ON`:

```bash
cmake -B build -DBUILD_FFI=ON
```

ncurses itself is a runtime dependency, not linked at build time:

```bash
# macOS: already installed system-wide (or `brew install ncurses` for a newer one)
# Debian/Ubuntu
sudo apt install libncurses-dev
# Fedora/RHEL
sudo dnf install ncurses-devel
```

## Import

```scheme
(import (curry ncurses))
```

## Design

One idiomatic layer, not a raw transliteration of the C API — that's what CHICKEN Scheme's `ncurses` egg already is (thin 1:1 bindings, C naming, manual `attron`/`attroff` pairing, magic-number key codes, no window abstraction beyond a raw pointer). Here: windows are records, not raw pointers; attributes apply via `with-attributes` rather than manual on/off pairing; `ncurses-getch` translates known key codes to plain symbols (`'up`, `'down`, `'enter`, ...) while still returning a raw integer for anything it doesn't recognize.

There is deliberately no separate "raw bindings" escape hatch exported — the underlying `%nc-*` foreign bindings are private. Keeping this module to one clean surface, rather than the layered raw/idiomatic split `(curry qt6)` uses, is an intentional simplicity choice for this module specifically.

The `printw`/`wprintw`/`mvprintw` family (variadic, printf-style) is **not bound** — `(curry ffi)` has no variadic-call support at all, and `ncurses-add-string!` plus Scheme's own `string-append`/`number->string` cover the same ground without needing it.

## Two rough edges from the underlying C library, not this module

- **`initscr()` can abort the process outright** on a fatal terminal-setup failure (no `TERM`, missing terminfo entry) rather than return an error Scheme could catch. There is no way around this from userland — it's documented C library behavior.
- **Attribute bit values and the color-pair packing scheme are technically a build-time constant of whatever ncurses was compiled with** (`NCURSES_ATTR_SHIFT`). This module hardcodes the near-universal convention (shift = 8), confirmed against a real system `curses.h` and the same assumption essentially every non-C ncurses binding (Python ctypes, Go, ...) makes, since there's no runtime function that reports this value to query it instead. Color pairs are set via `wcolor_set` — a real function taking a pair *number* directly — specifically to avoid needing to know the `COLOR_PAIR(n)` macro's bit-packing at all, which is the one place this scheme could actually vary (extended-color builds with more than 256 pairs use an indirection table under `wcolor_set` rather than changing the bit layout, so this module never needs to care).

## Session

### `(ncurses-init!)` → `<ncurses-window>`

Calls `initscr()`, then applies the boilerplate essentially every curses program wants immediately: `cbreak()`, `noecho()`, `keypad()` (arrow/function keys enabled) on the returned window. Returns the main ("stdscr") window. Raises if `initscr()` returns NULL.

### `(ncurses-end!)`

Calls `endwin()`, restoring normal terminal state.

### `(call-with-ncurses proc)`

Runs `(proc main-window)`; `ncurses-end!` always runs afterward, including when `proc` raises. The single most common ncurses program bug is leaving the terminal in raw/no-echo mode after a crash — this is the one thing worth guaranteeing rather than leaving to every caller to remember. Prefer this over calling `ncurses-init!`/`ncurses-end!` directly.

```scheme
(import (curry ncurses))
(call-with-ncurses
  (lambda (win)
    (ncurses-add-string! win 0 0 "hello")
    (ncurses-refresh! win)
    (ncurses-getch win)))
```

## Windows

### `(ncurses-window? x)` → boolean

### `(ncurses-window-new h w y x)` → `<ncurses-window>`

Wraps `newwin`. Raises if it fails (bad dimensions/position).

### `(ncurses-window-delete! win)`

**Calling any other function on `win` after this is a use-after-free at the C level** (`delwin` frees the underlying `WINDOW*`, and `<ncurses-window>` has no liveness tracking — the record just keeps holding the now-dangling pointer). This is inherent to a raw-pointer FFI binding without a GC-integrated handle-invalidation layer, the same as every other FFI-wrapped-pointer module in curry (e.g. `(curry hdf5)`'s file/dataset handles); don't use a window after deleting it.

### `(ncurses-move! win y x)`

### `(ncurses-add-string! win str)` / `(ncurses-add-string! win y x str)`

The 4-arg form moves the cursor first, then writes — the common case of "put this text at this position" in one call. `str` crosses into C as a raw, uncopied pointer (`(curry ffi)`'s `c-string` marshaling); an embedded NUL byte truncates the displayed text at that point rather than raising or corrupting memory — `waddstr` just stops reading at the first NUL, same as any C string function would.

### `(ncurses-refresh! win)` / `(ncurses-clear! win)` / `(ncurses-erase! win)`

### `(ncurses-box! win)`

Draws a border using ncurses' default line-drawing characters.

### `(ncurses-window-height win)` / `(ncurses-window-width win)`

## Input

### `(ncurses-getch win)` → symbol | char | integer

Returns a symbol for a recognized special key (`'up 'down 'left 'right 'home 'backspace 'delete 'insert 'page-up 'page-down 'enter 'end 'f1 'f2 'f3 'f4`), a character for a plain printable/ASCII code, or the raw integer for anything else (including `-1` from a `nodelay` window with no input ready). Key codes are taken from a real `curses.h`, not guessed.

## Attributes

### `(ncurses-attr-on! win attr)` / `(ncurses-attr-off! win attr)` / `(ncurses-attr-set! win attr)`

### `(with-attributes win attr body ...)`

Applies `attr` for the duration of `body`, then always turns it back off — even if `body` raises.

```scheme
(with-attributes win (bitwise-or a-bold a-underline)
  (ncurses-add-string! win 0 0 "bold and underlined"))
```

### Attribute constants

`a-normal` `a-bold` `a-underline` `a-reverse` `a-blink` `a-dim` `a-standout` `a-altcharset` `a-invis` `a-protect` `a-italic` (a ncurses extension, not present in every build). Combine with `bitwise-or`.

## Colors

### `(ncurses-start-color!)` → boolean

`#t` if the terminal supports color and color mode is now active. Uses `start_color()`'s own `OK`/`ERR` return code rather than calling `has_colors()` separately — `has_colors()` returns ncurses' narrow `bool` C type, which `(curry ffi)` has no exact-width return marshaling for; `start_color()`'s plain `int` return has no such ambiguity.

### `(ncurses-init-color-pair! pair fg bg)`

`fg`/`bg` accept either a symbol (`'black 'red 'green 'yellow 'blue 'magenta 'cyan 'white`) or a raw `0`–`7` integer.

### `(ncurses-set-color! win pair)`

## Example

[`examples/ncurses_demo.scm`](../../examples/ncurses_demo.scm) — attributes, a bordered sub-window, colors, and key-symbol translation. Run it directly in a real terminal (not through a pipe or non-interactive harness):

```bash
./build/curry examples/ncurses_demo.scm
```

## See also

- [`module-ffi.md`](module-ffi.md) — the FFI layer this module is built on; documents `define-foreign`'s type-tag limitations (no struct-by-value, no callbacks, no variadic functions) that shaped this module's scope (no `printw` family)
- [`module-hdf5.md`](module-hdf5.md) — the module this one's "pure Scheme + FFI, runtime `dlopen`, no build step" pattern is taken from directly
