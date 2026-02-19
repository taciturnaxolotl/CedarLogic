#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Source Emscripten environment
if [ -z "${EMSDK:-}" ]; then
	if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
		source "$HOME/emsdk/emsdk_env.sh"
	else
		echo "Error: Emscripten SDK not found. Set EMSDK or install to ~/emsdk"
		exit 1
	fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==> Configuring with Emscripten CMake..."
emcmake cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building WASM module..."
emmake make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

echo ""
echo "==> Build complete!"
echo "    Output files:"
echo "      ${BUILD_DIR}/cedarlogic.js"
echo "      ${BUILD_DIR}/cedarlogic.wasm"

# Copy to dist for easy serving
DIST_DIR="${SCRIPT_DIR}/dist"
mkdir -p "$DIST_DIR"
cp "$BUILD_DIR"/cedarlogic.js "$DIST_DIR/"
cp "$BUILD_DIR"/cedarlogic.wasm "$DIST_DIR/"

# Copy the test page if it exists
if [ -f "$SCRIPT_DIR/test.html" ]; then
	cp "$SCRIPT_DIR/test.html" "$DIST_DIR/"
fi

echo "    Copied to: ${DIST_DIR}/"
