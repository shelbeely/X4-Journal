#!/usr/bin/env bash
# tools/agent_build.sh
# Build production firmware. Exits nonzero on failure.
# See docs/agent-scripts.md and docs/build-guide.md for details.

set -euo pipefail

echo "[agent_build] Starting production firmware build..."

idf.py build

BIN_PATH=$(find build -maxdepth 1 -name "*.bin" | head -n 1)
if [ -z "$BIN_PATH" ]; then
  echo "[agent_build] ERROR: No .bin file found in ./build after build." >&2
  exit 1
fi

echo "[agent_build] Build succeeded: $BIN_PATH"
