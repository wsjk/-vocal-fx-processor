#!/usr/bin/env bash
set -euo pipefail

# Cross-compile the benchmark firmware (WeAct RP2350B Core by default; PICO_BOARD=pico2 or pico to change).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || echo "$SCRIPT_DIR/../..")"

cd "$REPO_ROOT"

# Auto-detect toolchain
for p in /Applications/CMake.app/Contents/bin /opt/homebrew/bin /usr/local/bin; do
    [ -x "$p/cmake" ] && export PATH="$p:$PATH" && break
done

for p in /Users/ww/tools/xpack-arm-none-eabi-gcc-*/bin /opt/homebrew/bin /usr/local/bin; do
    [ -d "$p" ] && export PATH="$p:$PATH" && break
done

# Pico SDK
export PICO_SDK_PATH="${PICO_SDK_PATH:-/Users/ww/src/pico-sdk}"
if [ ! -d "$PICO_SDK_PATH" ]; then
    echo "ERROR: Pico SDK not found at $PICO_SDK_PATH"
    echo "  git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk"
    exit 1
fi

echo "==> Building benchmark firmware for ${PICO_BOARD:-weact_studio_rp2350b_core}..."
cmake -S pico -B pico/build -DCMAKE_BUILD_TYPE=Release ${PICO_BOARD:+-DPICO_BOARD=$PICO_BOARD}
cmake --build pico/build -j4

echo "==> Firmware built: pico/build/vocal_pico_bench.uf2"
