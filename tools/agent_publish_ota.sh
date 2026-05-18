#!/usr/bin/env bash
# tools/agent_publish_ota.sh
# Copy/upload built firmware binary to OTA artifact location and generate manifest.json.
# No manual editing required. All values come from environment variables or the build.
# See docs/agent-scripts.md and docs/build-guide.md for details.
#
# Required environment:
#   X4_OTA_BASE_URL     Base HTTPS URL where OTA artifacts will be served
#
# Optional environment:
#   X4_OTA_ARTIFACT_DIR  Local staging directory (default: ./build/ota)
#   X4_OTA_CHANNEL       OTA channel string (default: dev)
#   X4_MIN_BATTERY_PERCENT  Minimum battery % (default: 40)
#   X4_BUILD_NUMBER      Build number override; auto-detected if not set

set -euo pipefail

OTA_BASE_URL="${X4_OTA_BASE_URL:?ERROR: X4_OTA_BASE_URL must be set}"
OTA_ARTIFACT_DIR="${X4_OTA_ARTIFACT_DIR:-./build/ota}"
OTA_CHANNEL="${X4_OTA_CHANNEL:-dev}"
MIN_BATTERY="${X4_MIN_BATTERY_PERCENT:-40}"

# Locate built binary
BIN_PATH=$(find build -maxdepth 1 -name "*.bin" | head -n 1)
if [ -z "$BIN_PATH" ]; then
  echo "[agent_publish_ota] ERROR: No .bin file found in ./build. Run agent_build.sh first." >&2
  exit 1
fi

# Determine version string
if [ -n "${X4_BUILD_NUMBER:-}" ]; then
  BUILD_NUMBER="$X4_BUILD_NUMBER"
else
  # Auto-increment: read last build number from artifact dir, increment by 1
  COUNTER_FILE="$OTA_ARTIFACT_DIR/.build_number"
  mkdir -p "$OTA_ARTIFACT_DIR"
  if [ -f "$COUNTER_FILE" ]; then
    BUILD_NUMBER=$(( $(cat "$COUNTER_FILE") + 1 ))
  else
    BUILD_NUMBER=1
  fi
  echo "$BUILD_NUMBER" > "$COUNTER_FILE"
fi

VERSION="x4-agent-${OTA_CHANNEL}-${BUILD_NUMBER}"
BIN_FILENAME="${VERSION}.bin"

echo "[agent_publish_ota] Version: $VERSION"
echo "[agent_publish_ota] Binary:  $BIN_PATH"

# Compute SHA-256
if command -v sha256sum &>/dev/null; then
  SHA256=$(sha256sum "$BIN_PATH" | awk '{print $1}')
elif command -v shasum &>/dev/null; then
  SHA256=$(shasum -a 256 "$BIN_PATH" | awk '{print $1}')
else
  echo "[agent_publish_ota] ERROR: Neither sha256sum nor shasum found." >&2
  exit 1
fi

echo "[agent_publish_ota] SHA-256: $SHA256"

# Stage artifacts
mkdir -p "$OTA_ARTIFACT_DIR"
cp "$BIN_PATH" "$OTA_ARTIFACT_DIR/$BIN_FILENAME"

FIRMWARE_URL="${OTA_BASE_URL%/}/${BIN_FILENAME}"

# Generate manifest.json
MANIFEST_PATH="$OTA_ARTIFACT_DIR/manifest.json"
cat > "$MANIFEST_PATH" <<EOF
{
  "device": "xteink-x4",
  "channel": "${OTA_CHANNEL}",
  "version": "${VERSION}",
  "url": "${FIRMWARE_URL}",
  "sha256": "${SHA256}",
  "min_battery_percent": ${MIN_BATTERY},
  "notes": "Built by agent_publish_ota.sh"
}
EOF

echo "[agent_publish_ota] Manifest written: $MANIFEST_PATH"
echo "[agent_publish_ota] Firmware URL:     $FIRMWARE_URL"
echo "[agent_publish_ota] Done. Upload $OTA_ARTIFACT_DIR/ to your OTA server."
