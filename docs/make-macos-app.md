# `scripts/make-macos-app.sh` — macOS Application Bundler

Packages a Curry Scheme script as a self-contained macOS `.app` bundle.
The result can be double-clicked in Finder, distributed as a `.dmg`, or
submitted to the Mac App Store (with a paid Apple Developer account and
additional entitlement configuration).

No Curry installation is required on the target machine — the binary,
all modules, and all non-system dylib dependencies are embedded in the bundle.

## Prerequisites

- A **built Curry binary**: run `cmake --build build` first.
- **macOS 12 (Monterey)** or later on the target machine.
- `codesign` (included with Xcode Command Line Tools).
- For Qt6 apps: `macdeployqt` from the same Qt version used to build
  the `qt6` module (`brew install qt@6` or `brew install qtbase`).

## Usage

```
scripts/make-macos-app.sh [options] [output-dir]
```

`output-dir` defaults to the current directory if omitted.

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--script FILE` | *(required)* | Main `.scm` entry-point |
| `--name NAME` | Script basename | App name shown in Finder and the menu bar |
| `--extra FILE` | — | Extra file copied into `Resources/` (repeat for multiple) |
| `--icon FILE` | — | App icon: `.icns` used as-is; `.png` auto-converted via `sips` |
| `--version VER` | `1.0` | `CFBundleVersion` string |
| `--bundle-id ID` | `com.curry.<name>` | `CFBundleIdentifier` |
| `--build-dir DIR` | `./build` | CMake build directory |
| `--sign IDENTITY` | `-` (ad-hoc) | `codesign` identity — pass `""` to skip signing entirely |
| `--gc-max-heap N` | — | Bake `--gc-max-heap N` into the launcher (e.g. `256M`, `2G`) |
| `--no-qt` | — | Skip Qt6 framework bundling even if `qt6.so` is present |

## Quickstart

```bash
# Minimal — non-GUI script
scripts/make-macos-app.sh --script examples/mcp_math.scm

# Named app dropped onto the Desktop
scripts/make-macos-app.sh \
  --script myapp.scm \
  --name "My App" \
  --version 1.0 \
  ~/Desktop

# Open it immediately
open ~/Desktop/MyApp.app
```

## Qt6 GUI application

```bash
scripts/make-macos-app.sh \
  --script examples/mandelbrot.scm \
  --name Mandelbrot \
  --icon art/mandelbrot.png \
  --version 2.0 \
  --gc-max-heap 512M \
  ~/Desktop
```

Qt6 is detected automatically: if `build/mods/curry/qt6.so` exists and
`--no-qt` is not passed, `macdeployqt` is run to pull in all Qt frameworks,
platform plugins (`platforms/libqcocoa.dylib`), image format plugins,
icon engines, and style plugins. These land in `Contents/Frameworks/` and
`Contents/PlugIns/` and are found without any environment configuration on
the user's machine.

The `qt6.so` module itself is also processed so its Qt framework references
are rewritten from absolute Homebrew paths to `@rpath`-relative paths
that resolve inside the bundle.

## Bundle structure

```
MyApp.app/
  Contents/
    Info.plist               ← app metadata
    MacOS/
      MyApp                  ← launcher shell script
      curry                  ← embedded binary
    Frameworks/              ← non-system dylibs + Qt6 frameworks
      libgc.1.dylib
      libgmp.10.dylib
      QtCore.framework/
      QtGui.framework/
      QtWidgets.framework/
      … (all transitive deps)
    PlugIns/                 ← Qt platform/image plugins (Qt6 apps only)
      platforms/
      imageformats/
      …
    Resources/
      main.scc               ← compiled bytecode of your .scm
      mods/curry/            ← all Curry modules (.so files)
        json.so
        qt6.so
        sqlite.so
        …
      AppIcon.icns           ← (if --icon was provided)
      …                      ← (any --extra files)
```

### Launcher script

`Contents/MacOS/MyApp` is a small shell script:

```bash
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export CURRY_MODULE_PATH="$DIR/../Resources/mods"
exec "$DIR/curry" --gc-max-heap 512M "$DIR/../Resources/main.scc" "$@"
```

It sets `CURRY_MODULE_PATH` so modules are found inside the bundle, then
`exec`s the embedded curry binary with the compiled script. Any arguments
passed to the launcher (e.g. from the command line) are forwarded to
`command-line-args` in the script.

## Dylib dependency bundling

The script performs a **recursive `otool -L` scan** of the curry binary and
every `.so` module. Any library rooted outside `/usr/lib/` or `/System/`
(i.e. Homebrew dependencies) is:

1. Copied into `Contents/Frameworks/`
2. Given a self-referential install name: `@rpath/libname.dylib`
3. Rewritten in the referencing binary: absolute path → `@rpath/libname.dylib`
4. Recursed into (in case it has its own Homebrew deps)

The curry binary and every module receive `@executable_path/../Frameworks`
as an `LC_RPATH` entry. Because `@executable_path` always resolves to
`Contents/MacOS/` — even from a `dlopen`'d module — all `@rpath` lookups
find `Contents/Frameworks/` correctly.

## Codesigning

By default the bundle is **ad-hoc signed** (`--sign -`). Ad-hoc signing:

- Passes Gatekeeper on the **local machine** and within a team sharing
  bundles over a local network or internal download.
- Is **not** accepted for public distribution — users outside your team
  will see a Gatekeeper rejection unless they explicitly allow it in
  System Preferences → Privacy & Security.

For public distribution, provide a Developer ID:

```bash
scripts/make-macos-app.sh --script myapp.scm \
  --sign "Developer ID Application: Your Name (XXXXXXXXXX)"
```

After signing with a Developer ID, notarize the bundle with
`xcrun notarytool` and staple the ticket with `xcrun stapler`.

To skip signing entirely (useful in CI before a separate signing step):

```bash
scripts/make-macos-app.sh --script myapp.scm --sign ""
```

## Icons

Pass any `.icns` file directly, or any `.png` — the script converts it
automatically using `sips` and `iconutil` (both ship with macOS):

```bash
# PNG: generates all required resolutions (16×16 through 512×512@2x)
scripts/make-macos-app.sh --script myapp.scm --icon artwork/icon.png

# Pre-built .icns: used as-is
scripts/make-macos-app.sh --script myapp.scm --icon artwork/icon.icns
```

For best results, provide a PNG of at least 1024×1024 pixels.

## Extra resource files

Use `--extra` (repeated) to include additional files in `Contents/Resources/`.
These are accessible from the script at runtime via a path relative to the
compiled `.scc`:

```bash
scripts/make-macos-app.sh \
  --script myapp.scm \
  --extra data/config.json \
  --extra data/wordlist.txt
```

From the script, find them alongside the launcher:

```scheme
; Resources/ is three levels up from the launcher's perspective,
; but the launcher sets the working directory — use an absolute path.
(define res-dir
  (string-append
    (or (get-environment-variable "CURRY_RESOURCE_DIR") ".")
    "/"))
```

A simpler convention: have the launcher export `CURRY_RESOURCE_DIR`:

```bash
# Add to the launcher (or pass via --extra a wrapper script):
export CURRY_RESOURCE_DIR="$DIR/../Resources"
```

## Baking a heap limit

For compute-heavy applications (numerical simulations, large symbolic CAS
expressions), bake in a heap cap so the OS doesn't page-swap under the user:

```bash
scripts/make-macos-app.sh \
  --script sicm-explorer.scm \
  --gc-max-heap 2G
```

This adds `--gc-max-heap 2G` to the launcher `exec` line. The same limit can
be applied at runtime from Scheme with `(gc-set-max-heap! (* 2 1024 1024 1024))`.

## Reducing bundle size

A full bundle with Qt6 is typically 120–160 MB. To reduce it:

- Pass `--no-qt` if your script does not use `(import (curry qt6))`.
  This skips macdeployqt entirely, bringing the bundle down to ~30–50 MB
  (mostly libplplot and its Pango/Cairo/X11 stack).
- After bundling, delete unused modules from `Contents/Resources/mods/curry/`.
  Each `.so` is only loaded on `import` — unused ones add no runtime cost,
  but do add ~1–5 MB each to disk size.
- `strip -x Contents/MacOS/curry` reduces the binary size, but removes
  debug symbols (useful for release builds, not for development).

## Limitations

- **macOS only.** For Linux, use the CPack `.deb`/`.rpm` targets:
  `cmake --build build && cpack --config build/CPackConfig.cmake`.
- **No App Store sandboxing.** The bundle as produced is not sandboxed
  and cannot be submitted to the Mac App Store without additional
  entitlements, capability declarations, and review of each module's
  network/file/IPC usage.
- **Qt6 version pinning.** The bundled Qt frameworks must match the version
  used to build `qt6.so`. If you upgrade Qt via Homebrew, rebuild curry
  with `cmake --build build` before re-running this script.
- **No universal binary.** The bundle contains the architecture of the
  machine it was built on (arm64 on Apple Silicon, x86_64 on Intel).
  To produce a universal binary, build curry twice with the appropriate
  `-DCMAKE_OSX_ARCHITECTURES` flag and `lipo` the results.
