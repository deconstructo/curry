#!/usr/bin/env bash
# bump-release.sh — tag a release and update Formula/curry.rb
#
# Usage: scripts/bump-release.sh <new-version> [--push]
#
#   <new-version>  e.g. 0.7.4.0
#   --push         also push the updated formula commit to origin/main
#
# What it does:
#   1. Validates the working tree is clean
#   2. Creates and pushes the git tag (triggers GitHub tarball creation)
#   3. Polls until the tarball is available, then computes SHA256
#   4. Updates Formula/curry.rb (url, sha256, version)
#   5. Commits the formula change
#   6. Optionally pushes the commit

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FORMULA="$REPO_ROOT/Formula/curry.rb"
GITHUB_REPO="deconstructo/curry"

# ── args ──────────────────────────────────────────────────────────────────────
if [[ $# -lt 1 ]]; then
  echo "Usage: $(basename "$0") <new-version> [--push]" >&2
  exit 1
fi

NEW_VER="$1"
PUSH_AFTER=false
[[ "${2-}" == "--push" ]] && PUSH_AFTER=true

TAG="v${NEW_VER}"
TARBALL_URL="https://github.com/${GITHUB_REPO}/archive/refs/tags/${TAG}.tar.gz"

# ── helpers ───────────────────────────────────────────────────────────────────
info()  { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
ok()    { printf '\033[1;32m  ✓ %s\033[0m\n' "$*"; }
die()   { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

# BSD sed needs: sed -i '' 's/.../.../' file
# GNU sed needs: sed -i 's/.../.../' file  (no separate empty-string arg)
if sed --version 2>/dev/null | grep -q GNU; then
  sedi() { sed -i "$@"; }
else
  sedi() { sed -i '' "$@"; }
fi

# ── preflight ─────────────────────────────────────────────────────────────────
cd "$REPO_ROOT"

info "Checking working tree"
if ! git diff --quiet || ! git diff --cached --quiet; then
  die "Working tree has uncommitted changes — commit or stash first."
fi
ok "Clean working tree"

if git tag --list | grep -qx "$TAG"; then
  die "Tag $TAG already exists locally."
fi

CURRENT_VER=$(grep '^  version ' "$FORMULA" | sed 's/.*"\(.*\)"/\1/')
info "Current formula version: $CURRENT_VER → new: $NEW_VER"

# ── tag & push ────────────────────────────────────────────────────────────────
info "Creating tag $TAG"
git tag "$TAG"
ok "Tag created"

info "Pushing tag to origin"
git push origin "$TAG"
ok "Tag pushed — GitHub is building the tarball"

# ── wait for tarball ──────────────────────────────────────────────────────────
info "Waiting for tarball to become available at GitHub"
MAX_TRIES=24   # 2 min
DELAY=5
for ((i=1; i<=MAX_TRIES; i++)); do
  HTTP=$(curl -sLo /dev/null -w '%{http_code}' "$TARBALL_URL")
  if [[ "$HTTP" == "200" ]]; then
    ok "Tarball is ready (attempt $i)"
    break
  fi
  if [[ $i -eq $MAX_TRIES ]]; then
    die "Tarball not available after $((MAX_TRIES * DELAY))s (last HTTP $HTTP). Check the tag pushed correctly."
  fi
  printf '  [%d/%d] HTTP %s — retrying in %ds…\n' "$i" "$MAX_TRIES" "$HTTP" "$DELAY"
  sleep "$DELAY"
done

# ── compute SHA256 ────────────────────────────────────────────────────────────
info "Computing SHA256"
SHA256=$(curl -sL "$TARBALL_URL" | shasum -a 256 | awk '{print $1}')
[[ -n "$SHA256" ]] || die "SHA256 came back empty."
ok "SHA256: $SHA256"

# ── update CMakeLists.txt ─────────────────────────────────────────────────────
CMAKELISTS="$REPO_ROOT/CMakeLists.txt"
info "Updating $CMAKELISTS"
sedi "s|project(curry VERSION [^ ]* LANGUAGES|project(curry VERSION ${NEW_VER} LANGUAGES|" \
  "$CMAKELISTS"
sedi "s|set(CPACK_PACKAGE_VERSION[^)]*)|set(CPACK_PACKAGE_VERSION           \"${NEW_VER}\")|" \
  "$CMAKELISTS"
sedi "s|set(CPACK_DEBIAN_PACKAGE_RELEASE[^)]*)|set(CPACK_DEBIAN_PACKAGE_RELEASE    \"1\")|" \
  "$CMAKELISTS"
sedi "s|# curry-scheme_[0-9][0-9._]*_|# curry-scheme_${NEW_VER}_|" \
  "$CMAKELISTS"
ok "CMakeLists.txt updated"

# ── update src/main.c ─────────────────────────────────────────────────────────
MAIN_C="$REPO_ROOT/src/main.c"
info "Updating $MAIN_C"
sedi "s|#define CURRY_VERSION \"[^\"]*\"|#define CURRY_VERSION \"${NEW_VER}\"|" \
  "$MAIN_C"
ok "src/main.c updated"

# ── update formula ────────────────────────────────────────────────────────────
info "Updating $FORMULA"

# url line
sedi "s|url \"https://github.com/${GITHUB_REPO}/archive/refs/tags/v[^\"]*\"|url \"${TARBALL_URL}\"|" \
  "$FORMULA"

# sha256 line (first occurrence — the release sha, not any resource block)
sedi "s|sha256 \"[a-f0-9]*\"|sha256 \"${SHA256}\"|" \
  "$FORMULA"

# version line
sedi "s|version \"[^\"]*\"|version \"${NEW_VER}\"|" \
  "$FORMULA"

# update the comment block that shows the example commands
sedi "s|/tags/v[0-9][^\"]*\.tar\.gz|/tags/${TAG}.tar.gz|g" \
  "$FORMULA"

ok "Formula updated"

# verify the changes look sane
echo
grep -n "url\|sha256\|version" "$FORMULA" | grep -v "^.*#"
echo

# ── commit ────────────────────────────────────────────────────────────────────
info "Committing version files"
git add "$FORMULA" "$CMAKELISTS" "$MAIN_C"
git commit -m "chore: bump version to ${NEW_VER}"
ok "Committed"

if $PUSH_AFTER; then
  info "Pushing commit to origin"
  git push origin main
  ok "Pushed"
else
  echo
  echo "  To push the formula commit:  git push origin main"
fi

echo
ok "Release ${TAG} done — formula is at ${NEW_VER}"
