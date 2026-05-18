#!/usr/bin/env bash
# tools/agent_healthcheck.sh
# Standalone health check: query the device health endpoint or print serial instructions.
# Thin wrapper around agent_check_device.sh for use in agent pipelines.
# See docs/agent-scripts.md for details.
#
# Optional environment:
#   X4_DEVICE_IP         Device IP address
#   X4_DEVICE_API_TOKEN  Auth token for /api/* endpoints

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/agent_check_device.sh"
