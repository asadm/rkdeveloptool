#!/usr/bin/env bash
set -euo pipefail

# Install and activate Emscripten locally, then load its environment.

if [[ -n "${BASH_SOURCE-}" ]]; then
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
elif [[ -n "${ZSH_VERSION-}" ]]; then
  SCRIPT_DIR="$(cd "$(dirname "${(%):-%N}")" && pwd)"
else
  SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
fi

pushd "${SCRIPT_DIR}/emsdk" >/dev/null

echo ">> Installing Emscripten (latest)"
./emsdk install latest

echo ">> Activating Emscripten (latest)"
./emsdk activate latest

echo ">> Loading environment"
source ./emsdk_env.sh
popd >/dev/null

if [[ -n "${BASH_SOURCE-}" && "${BASH_SOURCE[0]}" != "$0" ]]; then
  echo "Emscripten environment is active."
else
  echo "Emscripten installed. To use it in your current shell, run: source activate.sh"
fi
