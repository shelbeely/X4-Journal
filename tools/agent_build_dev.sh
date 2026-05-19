#!/usr/bin/env bash
# tools/agent_build_dev.sh
# Build firmware with development diagnostics flags enabled.
# See docs/agent-scripts.md and docs/build-guide.md for details.

set -euo pipefail

echo "[agent_build_dev] Starting dev diagnostics firmware build..."

idf.py \
  -DCONFIG_X4_DEV_DIAGNOSTICS=y \
  -DCONFIG_X4_DIAG_HTTP_API=y \
  build

BIN_PATH=$(find build -maxdepth 1 -name "*.bin" | head -n 1)
if [ -z "$BIN_PATH" ]; then
  echo "[agent_build_dev] ERROR: No .bin file found in ./build after build." >&2
  exit 1
fi

echo "[agent_build_dev] Dev build succeeded: $BIN_PATH"
