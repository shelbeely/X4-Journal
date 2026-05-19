# Build Guide — Configuration & Usage

## Overview

This guide explains how to build each firmware variant, configure the OTA manifest
URL and channel, run the agent scripts, and understand the known limitations and
hardware assumptions.

---

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html) CLI (`pio`) or PlatformIO IDE extension.
- Python ≥ 3.8 (used by PlatformIO and agent scripts).
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
pio run --environment x4_journal
```

Output: `.pio/build/x4_journal/<project>.bin`

---

### 2. Dev Diagnostics Build

Enables the HTTP diagnostics API and verbose logging for developer iteration.

```sh
./tools/agent_build_dev.sh
```

Or manually:
```sh
pio run --environment x4_journal_dev
```

Output: `.pio/build/x4_journal_dev/<project>.bin`

---

### 3. Agent Diagnostics Build

Strictest OTA health gate. Intended for AI-agent-assisted development where every
subsystem must pass before the firmware is accepted.

```sh
pio run --environment x4_journal_agent
```

Output: `.pio/build/x4_journal_agent/<project>.bin`

---

## Configuring OTA Manifest URL and Channel

### Via `platformio.ini` `build_flags` (recommended)

Add to the `[env:x4_journal]` section:
```ini
build_flags =
    ...existing flags...
    -DCONFIG_OTA_MANIFEST_URL=\"https://ota.example.com/manifest.json\"
    -DCONFIG_OTA_CHANNEL=\"dev\"
```

### Via `sdkconfig.defaults` (if using ESP-IDF directly)

```
CONFIG_OTA_MANIFEST_URL="https://ota.example.com/manifest.json"
CONFIG_OTA_CHANNEL="dev"
```

---

## Flashing the Device

Initial flash (first time, requires USB):
```sh
pio run --environment x4_journal -t upload
pio device monitor
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
pio device monitor
```

Filter for structured markers:
```sh
pio device monitor | grep '\[X4\]'
```

Key markers to watch after OTA:
- `[X4] OTA_PENDING_VERIFY` — device booted into new slot
- `[X4] OTA_MARK_VALID` — firmware accepted
- `[X4] OTA_ROLLBACK_REQUESTED reason=<stage>` — health check failed, rolling back
- `[X4] HEALTH_OK` / `[X4] HEALTH_FAILED` — overall health result

---

## Safe Mode Entry

1. Hold the POWER button (GPIO3) while powering on.
2. Hold for ≥ 3 seconds.
3. Release. The serial monitor will show `[X4] SAFE_MODE_ENTERED`.
4. The device starts Wi-Fi, enables OTA, and shows recovery status on the e-paper display.

To exit safe mode, power cycle without holding the button.

---

## Known Limitations and Hardware Assumptions

> This section must be updated once the actual firmware source is inspected.
> The entries below are placeholders derived from the spec.

| Item | Assumption / Limitation |
|------|------------------------|
| Display driver | SSD1677 (confirmed). Driven by `EInkDisplay` SDK lib at 800×480. Partial refresh falls back to full refresh as `EInkDisplay` does not expose a partial-refresh API. |
| Safe mode GPIO | GPIO3 — the POWER button on the X4 hardware (`X4_POWER_BTN_PIN` in `hardware_pins.h`). Active LOW with internal pull-up. |
| Partition sizes | Each OTA slot is 1.5 MB (0x17F000). SPIFFS remains at 0x310000 with 960 KB. |
| ESP-IDF version | Rollback APIs (`esp_ota_mark_app_invalid_rollback_and_reboot`) are available in ESP-IDF ≥ 4.x. PlatformIO `espressif32` platform includes a compatible version. |
| Battery monitor | `min_battery_percent` in the OTA manifest is parsed but not enforced unless a battery reading is available. The X4 has a battery ADC on GPIO1; the `power.h` module provides percentage readings. |
| Web server | Arduino `WebServer` on port 80. All new API modules register routes in `web_server_start()`. |
| Camera feedback | `tools/capture_display.sh` does not exist; `camera_verify` is always `"unavailable"` until implemented. |
| TLS certificates | `CONFIG_OTA_SERVER_CERT_PEM` must be set for production. An empty value disables certificate verification (development only). |
| Log buffer | `GET /api/logs` returns `501` if the log buffer was not initialised. `log_buffer_init()` is called at the start of `setup()`. |
| Dev API auth | If `CONFIG_X4_DIAG_API_TOKEN` is empty, `/api/dev/*` endpoints have no authentication. Acceptable for development; must be configured before production use. |
| First-time flash | The new dual-OTA partition table can only be written via USB. Devices with the old factory-only layout must be reflashed before OTA is functional. |

---

## Related Documents

- `architecture.md` — system overview and layer diagram
- `ota.md` — OTA configuration details
- `health-checks.md` — health check stages
- `safe-mode.md` — safe mode entry and configuration
- `dev-diagnostics.md` — diagnostics flag details
- `agent-scripts.md` — agent script specifications
- `safety-rules.md` — build-time safety invariants
