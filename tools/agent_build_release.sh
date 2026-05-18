#!/usr/bin/env bash
# tools/agent_build_release.sh
# Build production firmware with all diagnostics explicitly disabled.
# See docs/agent-scripts.md and docs/build-guide.md for details.

set -euo pipefail

echo "[agent_build_release] Starting release firmware build (all diagnostics disabled)..."

idf.py \
  -DCONFIG_X4_DEV_DIAGNOSTICS=n \
  -DCONFIG_X4_AGENT_DIAGNOSTICS=n \
  -DCONFIG_X4_VERBOSE_DISPLAY_DIAGNOSTICS=n \
  -DCONFIG_X4_DIAG_HTTP_API=n \
  build

BIN_PATH=$(find build -maxdepth 1 -name "*.bin" | head -n 1)
if [ -z "$BIN_PATH" ]; then
  echo "[agent_build_release] ERROR: No .bin file found in ./build after build." >&2
  exit 1
fi

echo "[agent_build_release] Release build succeeded: $BIN_PATH"
