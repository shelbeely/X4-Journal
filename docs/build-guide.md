# Build Guide — Configuration & Usage

## Overview

This guide explains how to build each firmware variant, configure the OTA manifest
URL and channel, run the agent scripts, and understand the known limitations and
hardware assumptions.

---

## Prerequisites

- ESP-IDF installed and `idf.py` on your PATH (version ≥ 5.0 recommended).
- Python ≥ 3.8 (used by idf.py and agent scripts).
- `jq` installed (used by agent scripts for JSON parsing).
- `sha256sum` or `shasum` available (used by `agent_publish_ota.sh`).
- `curl` available (used by agent scripts for HTTP requests).

---

## Build Configurations

### 1. Production / Release Build

All diagnostics are off. This is the firmware variant that should be deployed to
end users.

```sh
./tools/agent_build_release.sh
```

Or manually:
```sh
idf.py -DCONFIG_X4_DEV_DIAGNOSTICS=n \
        -DCONFIG_X4_AGENT_DIAGNOSTICS=n \
        -DCONFIG_X4_VERBOSE_DISPLAY_DIAGNOSTICS=n \
        -DCONFIG_X4_DIAG_HTTP_API=n \
        build
```

Output: `build/<project>.bin`

---

### 2. Dev Diagnostics Build

Enables the HTTP diagnostics API and verbose logging for developer iteration.

```sh
./tools/agent_build_dev.sh
```

Or manually:
```sh
idf.py -DCONFIG_X4_DEV_DIAGNOSTICS=y \
        -DCONFIG_X4_DIAG_HTTP_API=y \
        build
```

Output: `build/<project>.bin`

---

### 3. Agent Diagnostics Build

Strictest OTA health gate. Intended for AI-agent-assisted development where every
subsystem must pass before the firmware is accepted.

```sh
idf.py -DCONFIG_X4_DEV_DIAGNOSTICS=y \
        -DCONFIG_X4_AGENT_DIAGNOSTICS=y \
        -DCONFIG_X4_DIAG_HTTP_API=y \
        build
```

Output: `build/<project>.bin`

---

## Configuring OTA Manifest URL and Channel

### Via sdkconfig.defaults (recommended for a project)

Add to `sdkconfig.defaults`:
```
CONFIG_OTA_MANIFEST_URL="https://ota.example.com/manifest.json"
CONFIG_OTA_CHANNEL="dev"
```

### Via idf.py build override (for a single build)

```sh
idf.py -DCONFIG_OTA_MANIFEST_URL="https://ota.example.com/manifest.json" \
        -DCONFIG_OTA_CHANNEL="dev" \
        build
```

### Via menuconfig (interactive)

```sh
idf.py menuconfig
# Navigate to: Component config → X4 OTA → Manifest URL / Channel
```

---

## Flashing the Device

Initial flash (first time, requires USB):
```sh
idf.py flash monitor
```

After the first flash, subsequent updates use OTA. USB is only needed if OTA is broken
or the device has no valid OTA slot.

---

## Running the Agent Scripts

All scripts are in `tools/`. Make them executable first:
```sh
chmod +x tools/agent_*.sh
```

### Set environment variables

```sh
export X4_DEVICE_IP=192.168.1.55
export X4_OTA_BASE_URL="https://ota.example.com/firmware"
export X4_OTA_CHANNEL=dev
export X4_MIN_BATTERY_PERCENT=40
# Optional: for authenticated dev API endpoints
export X4_DEVICE_API_TOKEN=your-token-here
```

### Build firmware
```sh
./tools/agent_build.sh          # production build
./tools/agent_build_dev.sh      # dev diagnostics build
./tools/agent_build_release.sh  # explicit release build
```

### Publish OTA artifact
```sh
./tools/agent_publish_ota.sh
# Outputs manifest.json and .bin to $X4_OTA_ARTIFACT_DIR (default: ./build/ota)
# Prints the manifest URL on success
```

### Check device health
```sh
./tools/agent_check_device.sh
# Queries GET /api/health and prints a summary
# Exits 0 if healthy, 1 if not
```

### Full OTA cycle (build → publish → apply → verify)
```sh
./tools/agent_ota_cycle.sh
# Combines all steps; prints OTA CYCLE PASSED or OTA CYCLE FAILED
```

### Health check only
```sh
./tools/agent_healthcheck.sh
```

### Fetch recent logs
```sh
./tools/agent_logs.sh
```

---

## Serial Monitor

When the device IP is not available or for low-level debugging:

```sh
idf.py monitor
```

Filter for structured markers:
```sh
idf.py monitor | grep '\[X4\]'
```

Key markers to watch after OTA:
- `[X4] OTA_PENDING_VERIFY` — device booted into new slot
- `[X4] OTA_MARK_VALID` — firmware accepted
- `[X4] OTA_ROLLBACK_REQUESTED reason=<stage>` — health check failed, rolling back
- `[X4] HEALTH_OK` / `[X4] HEALTH_FAILED` — overall health result

---

## Safe Mode Entry

1. Hold the boot button (GPIO0 by default, or `CONFIG_SAFE_MODE_GPIO`) while powering on.
2. Hold for ≥ 3 seconds (configurable via `CONFIG_SAFE_MODE_HOLD_MS`).
3. Release. The serial monitor will show `[X4] SAFE_MODE_ENTERED`.
4. The device starts Wi-Fi, enables OTA, and shows recovery status on the e-paper display.

To exit safe mode, power cycle without holding the button.

---

## Known Limitations and Hardware Assumptions

> This section must be updated once the actual firmware source is inspected.
> The entries below are placeholders derived from the spec.

| Item | Assumption / Limitation |
|------|------------------------|
| Display driver | Unknown until firmware is inspected. Spec assumes a GDEW-family e-paper driver. |
| Safe mode GPIO | Defaults to GPIO0 (standard ESP32 boot button). Verify against X4 schematic. |
| Partition sizes | Spec assumes ≥ 1.5 MB per OTA slot. Verify actual partition CSV. |
| ESP-IDF version | Rollback APIs (`esp_ota_mark_app_invalid_rollback_and_reboot`) require ESP-IDF ≥ 4.x. |
| Battery monitor | Spec references `min_battery_percent` in the manifest. Battery reading is only enforced if a battery ADC/gauge driver exists. |
| Web server | Spec provides HTTP API endpoints. These are only registered if a web server (e.g., ESP-IDF `httpd`) is included in the build. |
| Camera feedback | `tools/capture_display.sh` is referenced in `display-diagnostics.md`. It does not exist yet; `camera_verify` will be `"unavailable"` until implemented. |
| TLS certificates | `CONFIG_OTA_SERVER_CERT_PEM` must be set for production. An empty value disables certificate verification (development only). |
| Log buffer | `GET /api/logs` returns `501` if an in-memory log ring buffer is not implemented. |
| Dev API auth | If `CONFIG_X4_DIAG_API_TOKEN` is empty, `/api/dev/*` endpoints have no authentication. This is acceptable for development but must be documented in release notes. |

---

## Related Documents

- `architecture.md` — system overview and layer diagram
- `ota.md` — OTA configuration details
- `health-checks.md` — health check stages
- `safe-mode.md` — safe mode entry and configuration
- `dev-diagnostics.md` — diagnostics flag details
- `agent-scripts.md` — agent script specifications
- `safety-rules.md` — build-time safety invariants
