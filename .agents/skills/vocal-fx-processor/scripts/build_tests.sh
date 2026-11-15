#!/usr/bin/env bash
set -euo pipefail

# Build and run headless DSP tests (no audio hardware needed).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || echo "$SCRIPT_DIR/../..")"

cd "$REPO_ROOT"

echo "==> Building headless DSP tests..."
cmake -B build-tests -S . -DBUILD_DESKTOP=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-tests -j4

echo "==> Running tests..."
ctest --test-dir build-tests --output-on-failure

echo "==> All tests passed."
