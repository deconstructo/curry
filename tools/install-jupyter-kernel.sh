#!/usr/bin/env bash
#
# install-jupyter-kernel.sh — register the curry_jupyter binary as a
# Jupyter kernelspec so notebooks/JupyterLab/`jupyter console` can see it.
#
# The kernel.json argv must contain an absolute path to a specific build's
# curry_jupyter binary, which is why this isn't a static file checked into
# the repo -- it's generated per-install here instead.
#
# interrupt_mode is "message" (not the jupyter_client default "signal") --
# curry_jupyter has no SIGINT handler, so a plain OS-signal interrupt would
# just kill the whole kernel process (default SIGINT action) instead of
# stopping the busy cell. "message" makes Jupyter send a proper
# interrupt_request over the control channel instead; see
# docs/reference/jupyter-kernel.md's "Execution model".
#
# Usage:
#   tools/install-jupyter-kernel.sh [path/to/curry_jupyter] [--user]
#
# Requires the `jupyter` CLI (kernelspec install) on PATH -- e.g. from the
# same conda-forge env used to build the kernel:
#   micromamba create -n curry-jupyter -c conda-forge xeus xeus-zmq xtl \
#     cppzmq nlohmann_json jupyter_client jupyter
set -euo pipefail

BINARY="${1:-build/curry_jupyter}"
shift || true
EXTRA_ARGS=("$@")

if [[ ! -x "$BINARY" ]]; then
  echo "error: $BINARY not found or not executable -- build it first:" >&2
  echo "  cmake -B build -DBUILD_JUPYTER_KERNEL=ON -DCMAKE_PREFIX_PATH=<conda-env-prefix>" >&2
  echo "  cmake --build build --target curry_jupyter" >&2
  exit 1
fi
BINARY="$(cd "$(dirname "$BINARY")" && pwd)/$(basename "$BINARY")"

if ! command -v jupyter >/dev/null 2>&1; then
  echo "error: jupyter CLI not found on PATH (see script header for how to get it)" >&2
  exit 1
fi

STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT
mkdir -p "$STAGE_DIR/curry"
# Escape backslash and double-quote before embedding into a JSON string --
# $BINARY came from `dirname`+`pwd`, which won't contain either in normal
# use, but a raw substitution into JSON is worth doing correctly regardless.
JSON_BINARY="${BINARY//\\/\\\\}"
JSON_BINARY="${JSON_BINARY//\"/\\\"}"
cat > "$STAGE_DIR/curry/kernel.json" <<EOF
{
  "argv": ["$JSON_BINARY", "-f", "{connection_file}"],
  "display_name": "Curry Scheme",
  "language": "scheme",
  "interrupt_mode": "message"
}
EOF

jupyter kernelspec install "$STAGE_DIR/curry" --name curry "${EXTRA_ARGS[@]}"
echo "Installed kernelspec pointing at: $BINARY"
