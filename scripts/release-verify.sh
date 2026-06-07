#!/usr/bin/env bash
# Post-release verification: uninstalls curry, taps/updates the formula,
# reinstalls, and confirms the installed version matches the expected tag.
#
# Usage: bash scripts/release-verify.sh vX.Y.Z
#
# Requires: brew (macOS), network access to GitHub.

set -euo pipefail

EXPECTED_TAG="${1:-}"
if [[ -z "$EXPECTED_TAG" ]]; then
    echo "Usage: $0 vX.Y.Z" >&2
    exit 1
fi
EXPECTED_VER="${EXPECTED_TAG#v}"

TAP="deconstructo/curry"
FORMULA_FULL="$TAP/curry"
GITHUB_REPO="https://github.com/deconstructo/curry.git"

pass() { echo "✓  $*"; }
fail() { echo "❌  $*" >&2; EXIT_CODE=1; }
EXIT_CODE=0

echo "=== curry post-release verification for $EXPECTED_TAG ==="
echo ""

# ── 1. uninstall existing install ────────────────────────────────────────────

if brew list --formula "$FORMULA_FULL" &>/dev/null 2>&1 ||
   brew list --formula curry &>/dev/null 2>&1; then
    echo "Uninstalling current curry install..."
    brew uninstall "$FORMULA_FULL" 2>/dev/null || brew uninstall curry 2>/dev/null || true
    pass "Uninstalled"
else
    echo "curry not currently installed — skipping uninstall"
fi

# ── 2. ensure the tap points at the right repo ───────────────────────────────

echo ""
echo "Ensuring tap $TAP is registered..."
if brew tap | grep -q "^$TAP$"; then
    pass "Tap $TAP already registered"
else
    echo "Tapping $TAP from $GITHUB_REPO ..."
    brew tap "$TAP" "$GITHUB_REPO"
    pass "Tapped $TAP"
fi

# ── 3. update the tap so the formula reflects the latest push ─────────────────

echo ""
echo "Updating tap to pull latest formula..."
brew update --auto-update 2>/dev/null || brew update
pass "brew update done"

# ── 4. verify the formula URL resolves to the expected tag ───────────────────

echo ""
FORMULA_URL=$(brew info --json "$FORMULA_FULL" 2>/dev/null \
              | python3 -c "import sys,json; d=json.load(sys.stdin)[0]; print(d.get('urls',{}).get('stable',{}).get('url',''))")
if [[ "$FORMULA_URL" == *"$EXPECTED_TAG"* ]]; then
    pass "Formula URL contains $EXPECTED_TAG: $FORMULA_URL"
else
    fail "Formula URL ($FORMULA_URL) does not reference $EXPECTED_TAG"
    echo "     The formula may not have been pushed yet, or brew update hasn't synced."
fi

# ── 5. install ────────────────────────────────────────────────────────────────

echo ""
echo "Installing $FORMULA_FULL ..."
brew install "$FORMULA_FULL"

# ── 6. verify installed version ──────────────────────────────────────────────

echo ""
CURRY_BIN=$(brew --prefix "$FORMULA_FULL" 2>/dev/null)/bin/curry
if [[ ! -x "$CURRY_BIN" ]]; then
    CURRY_BIN=$(command -v curry 2>/dev/null || true)
fi

if [[ -z "$CURRY_BIN" ]]; then
    fail "curry binary not found after install"
else
    INSTALLED_VER=$("$CURRY_BIN" --version 2>&1 | grep -o '[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*' | head -1)
    if [[ "$INSTALLED_VER" == "$EXPECTED_VER" ]]; then
        pass "Installed version matches: $INSTALLED_VER"
    else
        fail "Version mismatch: expected $EXPECTED_VER, got '$INSTALLED_VER'"
    fi
fi

# ── 7. smoke test ─────────────────────────────────────────────────────────────

echo ""
echo "Running smoke test..."
SMOKE=$("$CURRY_BIN" -e '(display (+ 1 2)) (newline)' 2>/dev/null || true)
if [[ "$SMOKE" == "3" ]]; then
    pass "Smoke test: (+ 1 2) = 3"
else
    fail "Smoke test failed: expected '3', got '$SMOKE'"
fi

# ── summary ───────────────────────────────────────────────────────────────────

echo ""
echo "=== Summary ==="
if [[ $EXIT_CODE -eq 0 ]]; then
    echo "✓  curry $EXPECTED_VER installed and verified"
else
    echo "❌  Verification failed — see errors above"
fi

exit $EXIT_CODE
