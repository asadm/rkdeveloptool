#!/usr/bin/env bash
set -euo pipefail

cd libusb && git apply ../patches/libusb-webusb-emscripten.patch
