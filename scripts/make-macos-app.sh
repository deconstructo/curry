#!/usr/bin/env bash
# make-macos-app.sh — bundle a Curry Scheme script as a self-contained macOS .app
#
# Assembles MyApp.app with the curry binary, all modules, their non-system
# Homebrew dylib dependencies, and — when the qt6 module is present —
# all Qt6 frameworks, plugins, and platform drivers via macdeployqt.
#
# Usage:
#   scripts/make-macos-app.sh [options] [output-dir]
#
# Required:
#   --script FILE        Main .scm entry-point
#
# Options:
#   --name NAME          App name (default: script basename without extension)
#   --extra FILE         Extra file copied into Resources/ (repeat as needed)
#   --icon FILE          App icon: .icns used as-is; .png auto-converted via sips
#   --version VER        CFBundleVersion (default: 1.0)
#   --bundle-id ID       CFBundleIdentifier (default: com.curry.<lowercased-name>)
#   --build-dir DIR      CMake build directory (default: ./build)
#   --sign IDENTITY      codesign identity: '-' = ad-hoc, '' = skip (default: -)
#   --gc-max-heap N      Bake --gc-max-heap N into the launcher (e.g. 512M, 2G)
#   --no-qt              Skip Qt6 bundling even if qt6.so is present
#   output-dir           Where to place AppName.app (default: .)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── defaults ──────────────────────────────────────────────────────────────────
APP_NAME=""
MAIN_SCRIPT=""
EXTRA_FILES=()
ICON_FILE=""
APP_VERSION="1.0"
BUNDLE_ID=""
BUILD_DIR="$REPO_ROOT/build"
SIGN_IDENTITY="-"
GC_MAX_HEAP=""
NO_QT=false
OUTPUT_DIR="."

# ── helpers ───────────────────────────────────────────────────────────────────
info() { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  ✓ %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m  ! %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
step() { printf '\033[0;36m  → %s\033[0m\n' "$*"; }

usage() {
  sed -n 's/^# \{0,2\}//p' "$0" | sed -n '2,/^$/p'
  exit 1
}

# Add an LC_RPATH entry only if it isn't already present.
add_rpath() {
  local rpath="$1" bin="$2"
  if ! otool -l "$bin" 2>/dev/null | grep -qF "path $rpath "; then
    install_name_tool -add_rpath "$rpath" "$bin" 2>/dev/null || true
  fi
}

# ── argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)        APP_NAME="$2";       shift 2 ;;
    --script)      MAIN_SCRIPT="$2";    shift 2 ;;
    --extra)       EXTRA_FILES+=("$2"); shift 2 ;;
    --icon)        ICON_FILE="$2";      shift 2 ;;
    --version)     APP_VERSION="$2";    shift 2 ;;
    --bundle-id)   BUNDLE_ID="$2";      shift 2 ;;
    --build-dir)   BUILD_DIR="$2";      shift 2 ;;
    --sign)        SIGN_IDENTITY="$2";  shift 2 ;;
    --gc-max-heap) GC_MAX_HEAP="$2";    shift 2 ;;
    --no-qt)       NO_QT=true;          shift   ;;
    --help|-h)     usage ;;
    -*)            die "Unknown option: $1 (run with --help)" ;;
    *)             OUTPUT_DIR="$1"; shift ;;
  esac
done

# ── validation ────────────────────────────────────────────────────────────────
[[ -n "$MAIN_SCRIPT" ]] || die "--script FILE is required"
[[ -f "$MAIN_SCRIPT" ]] || die "Script not found: $MAIN_SCRIPT"

CURRY_BIN="$BUILD_DIR/curry"
MODS_DIR="$BUILD_DIR/mods/curry"
[[ -f "$CURRY_BIN" ]]  || die "curry binary not found at $CURRY_BIN — run cmake --build first"
[[ -d "$MODS_DIR"  ]]  || die "Modules directory not found: $MODS_DIR"

if [[ -z "$APP_NAME" ]]; then
  APP_NAME="$(basename "$MAIN_SCRIPT")"
  APP_NAME="${APP_NAME%.scm}"
  APP_NAME="${APP_NAME%.scc}"
fi

BUNDLE_ID="${BUNDLE_ID:-com.curry.$(echo "$APP_NAME" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9.-')}"
APP_BUNDLE="$OUTPUT_DIR/$APP_NAME.app"

[[ -d "$OUTPUT_DIR" ]] || die "Output directory does not exist: $OUTPUT_DIR"

# ── detect Qt6 module ─────────────────────────────────────────────────────────
QT6_SO="$MODS_DIR/qt6.so"
BUNDLE_QT=false
MACDEPLOYQT=""
if [[ -f "$QT6_SO" ]] && ! $NO_QT; then
  BUNDLE_QT=true
  # Prefer the qtbase macdeployqt (same version the module was built against)
  for candidate in \
    "$(brew --prefix qtbase 2>/dev/null)/bin/macdeployqt" \
    "$(brew --prefix qt@6   2>/dev/null)/bin/macdeployqt" \
    "$(which macdeployqt 2>/dev/null)" ; do
    [[ -x "$candidate" ]] && { MACDEPLOYQT="$candidate"; break; }
  done
  [[ -n "$MACDEPLOYQT" ]] || die "qt6.so found but macdeployqt not found. Install qt@6 or pass --no-qt"
  ok "macdeployqt: $MACDEPLOYQT"
fi

# ── create bundle skeleton ────────────────────────────────────────────────────
info "Creating $APP_BUNDLE"
rm -rf "$APP_BUNDLE"
CONTENTS="$APP_BUNDLE/Contents"
mkdir -p \
  "$CONTENTS/MacOS" \
  "$CONTENTS/Frameworks" \
  "$CONTENTS/Resources/mods/curry"

# ── compile main script to bytecode ──────────────────────────────────────────
info "Compiling $MAIN_SCRIPT"
COMPILED_SCC="$CONTENTS/Resources/main.scc"
CURRY_MODULE_PATH="$MODS_DIR" \
  "$CURRY_BIN" -c "$MAIN_SCRIPT" -o "$COMPILED_SCC"
ok "Compiled → Resources/main.scc"

# ── copy extra resource files ─────────────────────────────────────────────────
for f in "${EXTRA_FILES[@]+"${EXTRA_FILES[@]}"}"; do
  [[ -f "$f" ]] || die "Extra file not found: $f"
  cp "$f" "$CONTENTS/Resources/"
  step "Extra: $(basename "$f")"
done

# ── copy curry binary ─────────────────────────────────────────────────────────
info "Copying curry binary"
cp "$CURRY_BIN" "$CONTENTS/MacOS/curry"
chmod 755 "$CONTENTS/MacOS/curry"

# ── copy modules ──────────────────────────────────────────────────────────────
info "Copying modules"
cp "$MODS_DIR"/*.so "$CONTENTS/Resources/mods/curry/" 2>/dev/null || true
step "$(ls "$CONTENTS/Resources/mods/curry/"*.so 2>/dev/null | wc -l | tr -d ' ') modules copied"

# ── bundle non-system Homebrew dylibs ─────────────────────────────────────────
# Recursively copies Homebrew dylibs into Contents/Frameworks/ and rewires
# @install_name references to use @rpath so the bundle is self-contained.
# System libs (/usr/lib, /System/) are left as external references.

info "Bundling dylib dependencies"

bundle_dylib_deps() {
  local binary="$1"
  local fwdir="$CONTENTS/Frameworks"

  while IFS= read -r dep; do
    # Leave system libs alone
    [[ "$dep" == /usr/lib/* ]]    && continue
    [[ "$dep" == /System/* ]]     && continue
    [[ "$dep" == /usr/local/lib/libreadline* ]] && continue
    [[ "$dep" == @* ]]            && continue
    [[ -z "$dep" ]]               && continue
    [[ ! -f "$dep" ]]             && { warn "Dep not found: $dep (skipping)"; continue; }

    local libname
    libname="$(basename "$dep")"

    # Skip if already bundled (avoids infinite recursion on mutual deps)
    [[ -f "$fwdir/$libname" ]] && {
      # Still rewrite the reference in this binary even if lib was already copied
      install_name_tool -change "$dep" "@rpath/$libname" "$binary" 2>/dev/null || true
      continue
    }

    step "Bundling $libname"
    cp "$dep" "$fwdir/$libname"
    chmod 755 "$fwdir/$libname"

    # Fix the lib's own install name so dependents can address it via @rpath
    install_name_tool -id "@rpath/$libname" "$fwdir/$libname" 2>/dev/null || true

    # Rewrite the reference in the binary that depends on this lib
    install_name_tool -change "$dep" "@rpath/$libname" "$binary" 2>/dev/null || true

    # Recurse: the copied lib may itself have Homebrew deps
    bundle_dylib_deps "$fwdir/$libname"
  done < <(otool -L "$binary" 2>/dev/null | awk 'NR>1{print $1}')
}

# Wire @executable_path/../Frameworks as the rpath for the curry binary and
# every module. @executable_path always resolves to Contents/MacOS/ regardless
# of whether it's referenced from the main binary or a dlopen'd module.
bundle_dylib_deps "$CONTENTS/MacOS/curry"
add_rpath "@executable_path/../Frameworks" "$CONTENTS/MacOS/curry"

for so in "$CONTENTS/Resources/mods/curry"/*.so; do
  [[ -f "$so" ]] || continue
  bundle_dylib_deps "$so"
  add_rpath "@executable_path/../Frameworks" "$so"
done
ok "Dylib dependencies bundled"

# ── Qt6 frameworks via macdeployqt ────────────────────────────────────────────
if $BUNDLE_QT; then
  info "Bundling Qt6 frameworks via macdeployqt"
  # macdeployqt must find the main executable in MacOS/ to write Info.plist
  # paths correctly.  We give it both curry and qt6.so so it discovers all
  # Qt framework dependencies.
  "$MACDEPLOYQT" "$APP_BUNDLE" \
    -executable="$CONTENTS/MacOS/curry" \
    -executable="$CONTENTS/Resources/mods/curry/qt6.so" \
    -no-strip \
    2>&1 | sed 's/^/  /'

  # macdeployqt may not add @executable_path/../Frameworks to modules it
  # patched — ensure it's present so Qt framework @rpath lookups succeed.
  add_rpath "@executable_path/../Frameworks" "$CONTENTS/MacOS/curry"
  for so in "$CONTENTS/Resources/mods/curry"/*.so; do
    [[ -f "$so" ]] || continue
    add_rpath "@executable_path/../Frameworks" "$so"
  done
  ok "Qt6 frameworks bundled"
fi

# ── icon ──────────────────────────────────────────────────────────────────────
ICON_DEST=""
if [[ -n "$ICON_FILE" ]]; then
  info "Processing icon"
  case "${ICON_FILE##*.}" in
    icns)
      cp "$ICON_FILE" "$CONTENTS/Resources/AppIcon.icns"
      ICON_DEST="AppIcon"
      ok "Icon copied"
      ;;
    png|PNG)
      ICONSET=$(mktemp -d)
      trap 'rm -rf "$ICONSET"' EXIT
      for sz in 16 32 128 256 512; do
        sips -z "$sz" "$sz" "$ICON_FILE" \
          --out "$ICONSET/icon_${sz}x${sz}.png"       >/dev/null 2>&1
        sips -z $((sz*2)) $((sz*2)) "$ICON_FILE" \
          --out "$ICONSET/icon_${sz}x${sz}@2x.png"    >/dev/null 2>&1
      done
      # iconutil wants a .iconset directory
      ICONSET_DIR="$(mktemp -d).iconset"
      mv "$ICONSET" "$ICONSET_DIR"
      iconutil -c icns "$ICONSET_DIR" -o "$CONTENTS/Resources/AppIcon.icns"
      rm -rf "$ICONSET_DIR"
      ICON_DEST="AppIcon"
      ok "PNG converted to .icns"
      ;;
    *)
      warn "Unknown icon format '${ICON_FILE##*.}' — skipping (use .icns or .png)"
      ;;
  esac
fi

# ── Info.plist ────────────────────────────────────────────────────────────────
info "Writing Info.plist"
ICON_KEY=""
[[ -n "$ICON_DEST" ]] && ICON_KEY="
  <key>CFBundleIconFile</key>
  <string>${ICON_DEST}</string>"

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>
  <string>${APP_NAME}</string>
  <key>CFBundleDisplayName</key>
  <string>${APP_NAME}</string>
  <key>CFBundleIdentifier</key>
  <string>${BUNDLE_ID}</string>
  <key>CFBundleVersion</key>
  <string>${APP_VERSION}</string>
  <key>CFBundleShortVersionString</key>
  <string>${APP_VERSION}</string>
  <key>CFBundleExecutable</key>
  <string>${APP_NAME}</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>LSMinimumSystemVersion</key>
  <string>12.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>${ICON_KEY}
  <key>NSPrincipalClass</key>
  <string>NSApplication</string>
</dict>
</plist>
PLIST
ok "Info.plist written"

# ── launcher script ───────────────────────────────────────────────────────────
info "Writing launcher"
LAUNCHER="$CONTENTS/MacOS/$APP_NAME"

GC_FLAG=""
[[ -n "$GC_MAX_HEAP" ]] && GC_FLAG="--gc-max-heap $GC_MAX_HEAP"

cat > "$LAUNCHER" <<LAUNCHER
#!/bin/bash
# Auto-generated launcher for ${APP_NAME}.app — do not edit by hand.
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
export CURRY_MODULE_PATH="\$DIR/../Resources/mods"
exec "\$DIR/curry" ${GC_FLAG} "\$DIR/../Resources/main.scc" "\$@"
LAUNCHER
chmod 755 "$LAUNCHER"
ok "Launcher written → Contents/MacOS/$APP_NAME"

# ── codesign ──────────────────────────────────────────────────────────────────
if [[ -n "$SIGN_IDENTITY" ]]; then
  info "Codesigning (identity: $SIGN_IDENTITY)"
  codesign --deep --force --sign "$SIGN_IDENTITY" "$APP_BUNDLE" 2>&1 | sed 's/^/  /'
  ok "Codesigned"
else
  warn "Codesigning skipped (--sign was empty)"
fi

# ── summary ───────────────────────────────────────────────────────────────────
BUNDLE_SIZE="$(du -sh "$APP_BUNDLE" 2>/dev/null | cut -f1)"
echo
ok "Done — ${APP_BUNDLE}  (${BUNDLE_SIZE})"
echo
echo "  Run:   open '${APP_BUNDLE}'"
echo "  CLI:   '${APP_BUNDLE}/Contents/MacOS/${APP_NAME}'"
echo
