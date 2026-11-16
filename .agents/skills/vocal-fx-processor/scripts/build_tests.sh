#!/usr/bin/env bash
set -euo pipefail

# Build and run headless DSP tests (no audio hardware needed).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || echo "$SCRIPT_DIR/../..")"

cd "$REPO_ROOT"

# DaisySP (for the ReverbSc reverb): use a local clone if there is one, so no download is needed.
DAISYSP_DIR="${DAISYSP_DIR:-/Users/ww/src/DaisySP}"
DAISYSP_ARG=()
[ -f "$DAISYSP_DIR/DaisySP-LGPL/Source/Effects/reverbsc.cpp" ] && DAISYSP_ARG=(-DFETCHCONTENT_SOURCE_DIR_DAISYSP="$DAISYSP_DIR")

echo "==> Building headless DSP tests..."
cmake -B build-tests -S . -DBUILD_DESKTOP=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ${DAISYSP_ARG[@]+"${DAISYSP_ARG[@]}"}
cmake --build build-tests -j4

echo "==> Running tests..."
ctest --test-dir build-tests --output-on-failure

echo "==> All tests passed."
