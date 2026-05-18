# Agent Scripts Spec

## Overview

This document specifies the shell scripts in the `tools/` directory that an AI coding
agent (or a human developer) uses to build firmware, publish OTA artifacts, check
device health, and run a full OTA iteration cycle.

All scripts must:
- Be executable (`chmod +x`).
- Exit with a non-zero status code on any failure.
- Print clear, human-readable progress output.
- Never require manual editing of hardcoded values; all configuration comes from
  environment variables or the project's build configuration.
- Never commit secrets to the repository.

---

## Environment Variables

Scripts share these common environment variables. Set them in your shell or a
`.env` file that is **not** committed to the repository.

| Variable | Default | Description |
|----------|---------|-------------|
| `X4_DEVICE_IP` | — | IP address of the device on the local network |
| `X4_OTA_ARTIFACT_DIR` | `./build/ota` | Local directory where OTA artifacts are staged |
| `X4_OTA_BASE_URL` | — | Base HTTPS URL where OTA artifacts are served |
| `X4_OTA_CHANNEL` | `dev` | OTA channel string written into `manifest.json` |
| `X4_MIN_BATTERY_PERCENT` | `40` | Minimum battery % written into `manifest.json` |
| `X4_BUILD_NUMBER` | auto | Build number used in version string; auto-incremented if not set |
| `X4_DEVICE_API_TOKEN` | — | Auth token for `/api/dev/*` endpoints (if configured) |

---

## `tools/agent_build.sh`

**Purpose:** Build the production firmware. Exits nonzero on any build failure.

**Behavior:**
1. Run `idf.py build` (or the project's equivalent build command).
2. Exit with the build tool's exit code.
3. On success, print the path to the generated `.bin` file.

**Usage:**
```sh
./tools/agent_build.sh
```

---

## `tools/agent_build_dev.sh`

**Purpose:** Build firmware with development diagnostics flags enabled.

**Behavior:**
1. Run `idf.py build` with the following overrides:
   - `CONFIG_X4_DEV_DIAGNOSTICS=y`
   - `CONFIG_X4_DIAG_HTTP_API=y`
2. Exit nonzero on failure.
3. On success, print the path to the generated `.bin` file.

**Usage:**
```sh
./tools/agent_build_dev.sh
```

---

## `tools/agent_build_release.sh`

**Purpose:** Build production firmware with all diagnostics explicitly disabled.

**Behavior:**
1. Run `idf.py build` with the following overrides:
   - `CONFIG_X4_DEV_DIAGNOSTICS=n`
   - `CONFIG_X4_AGENT_DIAGNOSTICS=n`
   - `CONFIG_X4_VERBOSE_DISPLAY_DIAGNOSTICS=n`
   - `CONFIG_X4_DIAG_HTTP_API=n`
2. Exit nonzero on failure.
3. On success, print the path to the generated `.bin` file.

**Usage:**
```sh
./tools/agent_build_release.sh
```

---

## `tools/agent_publish_ota.sh`

**Purpose:** Copy or upload the built firmware binary to the OTA artifact location
and generate a `manifest.json` file. No manual editing required.

**Behavior:**
1. Locate the built `.bin` file (default: `./build/<project>.bin`).
2. Determine the version string from the firmware binary's app description or from
   `$X4_BUILD_NUMBER`.
3. Compute the SHA-256 hash of the `.bin` file.
4. Generate `manifest.json`:
   ```json
   {
     "device": "xteink-x4",
     "channel": "<X4_OTA_CHANNEL>",
     "version": "<version>",
     "url": "<X4_OTA_BASE_URL>/<version>.bin",
     "sha256": "<sha256>",
     "min_battery_percent": <X4_MIN_BATTERY_PERCENT>,
     "notes": "Built by agent_publish_ota.sh"
   }
   ```
5. Copy or upload the `.bin` file and `manifest.json` to `$X4_OTA_ARTIFACT_DIR`
   (and optionally to a remote server if upload commands are configured).
6. Print the manifest URL on success.
7. Exit nonzero on any failure.

**Usage:**
```sh
export X4_OTA_BASE_URL="https://ota.example.com/firmware"
./tools/agent_publish_ota.sh
```

---

## `tools/agent_check_device.sh`

**Purpose:** Query the device health endpoint and report its status.

**Behavior:**
1. If `$X4_DEVICE_IP` is set, attempt `GET http://$X4_DEVICE_IP/api/health`.
2. Parse the JSON response and print a summary:
   - Overall `status` field.
   - Any failed stages (`last_failed_stage`, `last_failed_reason`).
   - `version`, `slot`, `pending_verify`.
3. Exit 0 if `status == "ok"`, exit 1 otherwise.
4. If the device is unreachable or no IP is configured, print instructions for
   reading serial logs:
   ```
   Device not reachable. To check health via serial:
     1. Connect USB and open a serial monitor at 115200 baud.
     2. Look for: [X4] HEALTH_OK or [X4] HEALTH_FAILED
     3. After OTA, look for: [X4] OTA_MARK_VALID or [X4] OTA_ROLLBACK_REQUESTED
   ```

**Usage:**
```sh
export X4_DEVICE_IP=192.168.1.55
./tools/agent_check_device.sh
```

---

## `tools/agent_ota_cycle.sh`

**Purpose:** Full OTA iteration cycle: build → publish → trigger → wait → verify.

**Behavior:**
1. Call `tools/agent_build.sh` (or `agent_build_dev.sh` if `X4_BUILD_DEV=1`). Exit on failure.
2. Call `tools/agent_publish_ota.sh`. Exit on failure.
3. If `$X4_DEVICE_IP` is set, call `POST /api/ota/check` to trigger a manifest check.
4. If an update is available, call `POST /api/ota/apply` to start the download.
5. Wait for the device to reboot (poll `GET /api/health` with a timeout of 120 seconds;
   wait for the endpoint to become unreachable, then reachable again).
6. Call `tools/agent_check_device.sh` to verify health.
7. Print `OTA CYCLE PASSED` and exit 0 on success.
8. Print `OTA CYCLE FAILED: <reason>` and exit 1 on failure.

If the device IP is not set, print instructions for triggering OTA manually:
```
No device IP set. To trigger OTA manually:
  1. Ensure the manifest URL is reachable from the device.
  2. On the device web UI or serial console, trigger POST /api/ota/check.
  3. After reboot, run: ./tools/agent_check_device.sh
```

**Usage:**
```sh
export X4_DEVICE_IP=192.168.1.55
./tools/agent_ota_cycle.sh
```

---

## `tools/agent_healthcheck.sh`

**Purpose:** Standalone health check — query the health endpoint or parse serial logs.

**Behavior:**
1. If `$X4_DEVICE_IP` is set, call `GET /api/health` and print the result.
2. Otherwise, print serial log parsing instructions.
3. Exit 0 if health is `ok`, exit 1 otherwise.

This script is a thin wrapper around `agent_check_device.sh` and is provided as a
stable entry point for agent pipelines that only need a health result.

**Usage:**
```sh
./tools/agent_healthcheck.sh
```

---

## `tools/agent_logs.sh`

**Purpose:** Fetch recent firmware logs from the device.

**Behavior:**
1. If `$X4_DEVICE_IP` is set, call `GET /api/logs` (or `GET /api/dev/logs` if
   `$X4_DEVICE_API_TOKEN` is set) and print the log lines.
2. If the endpoint returns `501 Not Implemented`, print:
   ```
   Log buffer not available via API. Connect USB and monitor serial at 115200 baud.
   Filter for lines starting with [X4] for structured output.
   ```
3. Exit 0 on success, exit 1 on error.

**Usage:**
```sh
export X4_DEVICE_IP=192.168.1.55
./tools/agent_logs.sh
```

---

## Script Dependency Summary

```
agent_ota_cycle.sh
  ├── agent_build.sh  (or agent_build_dev.sh)
  ├── agent_publish_ota.sh
  └── agent_check_device.sh
        └── (calls GET /api/health)

agent_healthcheck.sh
  └── (calls GET /api/health or prints serial instructions)

agent_logs.sh
  └── (calls GET /api/logs or prints serial instructions)
```

---

## Related Documents

- `build-guide.md` — how to configure environment variables and run these scripts
- `ota.md` — OTA state machine these scripts interact with
- `health-checks.md` — health check stages parsed by `agent_check_device.sh`
- `serial-log-markers.md` — markers to look for when using serial fallback
- `api.md` — API endpoints called by these scripts
