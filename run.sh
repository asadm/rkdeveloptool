#!/usr/bin/env bash
set -euo pipefail

# Build the WASM/WebUSB bundle and serve the demo page.
# Requires Emscripten in PATH (emcmake/emmake) and a WebUSB-capable browser.

BUILD_DIR="${BUILD_DIR:-build-wasm}"
PORT="${PORT:-1337}"
TARGET="rkDevelopTool_Mac"

if ! command -v emcmake >/dev/null; then
  echo "emcmake not found. Load the Emscripten environment before running." >&2
  exit 1
fi

echo ">> Configuring with emcmake (dir: ${BUILD_DIR})"
emcmake cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release

echo ">> Building"
cmake --build "${BUILD_DIR}"

JS_OUT="${BUILD_DIR}/${TARGET}.js"
WASM_OUT="${BUILD_DIR}/${TARGET}.wasm"

if [[ ! -f "${JS_OUT}" || ! -f "${WASM_OUT}" ]]; then
  echo "Build artifacts not found: ${JS_OUT} / ${WASM_OUT}" >&2
  exit 1
fi

mkdir -p docs
cp "${JS_OUT}" "docs/${TARGET}.js"
cp "${WASM_OUT}" "docs/${TARGET}.wasm"

echo ">> Artifacts copied to docs/ (serve manually; ensure .wasm uses application/wasm)"
