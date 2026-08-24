#!/usr/bin/env bash
# Build the CedarLogic core for the browser.
#
# The output lands in web/public/engine/ because the web shell is its only
# consumer -- a separate dist/ plus a copy step would just be two places for a
# stale artifact to hide.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
OUT_DIR="${SCRIPT_DIR}/../../web/public/engine"

if [ -z "${EMSDK:-}" ] && ! command -v emcc >/dev/null 2>&1; then
	if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
		# shellcheck disable=SC1091
		source "$HOME/emsdk/emsdk_env.sh"
	else
		echo "Error: Emscripten not found. Install it, or set EMSDK." >&2
		exit 1
	fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==> Configuring..."
emcmake cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null

echo "==> Building..."
emmake make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

mkdir -p "$OUT_DIR"
cp cedarlogic-gui.js cedarlogic-gui.wasm "$OUT_DIR/"

echo ""
echo "==> $(cd "$OUT_DIR" && pwd)"
ls -lh "$OUT_DIR"/cedarlogic-gui.* | awk '{printf "    %-24s %s\n", $9, $5}'
