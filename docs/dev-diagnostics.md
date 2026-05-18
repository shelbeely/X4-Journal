# Development Diagnostics Flag Spec

## Overview

This document specifies the compile-time diagnostics flags, the diagnostics data object,
secret redaction rules, authentication requirements, and development API endpoints for
the Xteink X4 firmware.

All diagnostic features are **disabled by default** and must never be present in
production/release builds unless explicitly enabled.

---

## Compile-Time Flags

All flags are defined in `Kconfig` and default to `n` (disabled).

| Kconfig Symbol | Default | Description |
|----------------|---------|-------------|
| `CONFIG_X4_DEV_DIAGNOSTICS` | `n` | Master switch: enables all dev diagnostic subsystems |
| `CONFIG_X4_AGENT_DIAGNOSTICS` | `n` | Stricter OTA health gate for AI-agent-assisted development |
| `CONFIG_X4_VERBOSE_DISPLAY_DIAGNOSTICS` | `n` | Emit extra display log detail (framebuffer hash per operation, timing breakdowns) |
| `CONFIG_X4_DIAG_HTTP_API` | `n` | Expose `/api/dev/*` HTTP endpoints |

Dependency rules:
- `CONFIG_X4_DIAG_HTTP_API` requires `CONFIG_X4_DEV_DIAGNOSTICS=y`.
- `CONFIG_X4_AGENT_DIAGNOSTICS` requires `CONFIG_X4_DEV_DIAGNOSTICS=y`.
- `CONFIG_X4_VERBOSE_DISPLAY_DIAGNOSTICS` can be set independently but has no effect
  unless `CONFIG_X4_DEV_DIAGNOSTICS=y`.

When `CONFIG_X4_DEV_DIAGNOSTICS=n`:
- All `#ifdef CONFIG_X4_DEV_DIAGNOSTICS` blocks are excluded from the binary.
- No dev API endpoints are registered.
- No verbose log markers are emitted.
- Binary size and runtime overhead are unaffected.

---

## Serial Log: Diagnostics Banner

When `CONFIG_X4_DEV_DIAGNOSTICS=y`, the following marker is emitted early in boot,
before any other output:

```
[X4] DEV_DIAGNOSTICS_ENABLED agent=<0|1> verbose_display=<0|1> http_api=<0|1>
```

This allows monitoring scripts to know that extra diagnostic data will follow.

---

## Diagnostics Object Schema

The diagnostics object is returned by `GET /api/dev/status` and can be serialized to
the serial log on demand. All fields marked **redact** must be replaced with `"[REDACTED]"`
regardless of context.

```json
{
  "firmware_version": "x4-agent-dev-42",
  "git_commit": "a1b2c3d4",
  "build_timestamp": "2026-05-18T23:00:00Z",
  "target_chip": "esp32",
  "ota_slot": "ota_1",
  "ota_pending_verify": true,
  "rollback_available": true,
  "reset_reason": "poweron",
  "boot_count": 3,
  "crash_loop_detected": false,
  "heap_free": 123456,
  "heap_free_min": 98765,
  "wifi": {
    "state": "connected | disconnected | ap_mode | disabled",
    "ip": "192.168.1.55",
    "rssi": -62,
    "ssid": "MyNetwork",
    "password": "[REDACTED]"
  },
  "storage": {
    "state": "ok | failed | not_initialized",
    "nvs_used_entries": 12,
    "nvs_free_entries": 116,
    "spiffs_total": 196608,
    "spiffs_used": 32768
  },
  "display": {
    "driver": "gdew0213b74",
    "init_ok": true,
    "last_error": null,
    "framebuffer_size": 3844,
    "framebuffer_hash": "a3f1c2d4",
    "last_refresh_type": "full",
    "last_refresh_duration_ms": 1820
  },
  "input": {
    "state": "ok | failed | not_initialized",
    "safe_mode_button_gpio": 0,
    "last_button_event": "none"
  },
  "safe_mode": false,
  "ota_manifest_url": "[REDACTED if contains credentials]",
  "ota_channel": "dev",
  "ota_token": "[REDACTED]"
}
```

### Redaction Rules

The following fields and value types must **always** be replaced with `"[REDACTED]"`:

| Category | Examples |
|----------|---------|
| Wi-Fi passwords | `wifi.password`, any NVS key containing `pass` or `psk` |
| API tokens / bearer tokens | Any HTTP header value, NVS key containing `token` or `key` |
| Private keys / certificates | Any PEM-encoded block |
| Signed URLs | Any URL containing `?X-Amz-Signature=`, `?token=`, or similar query parameters |
| OTA credentials | `ota_token`, HTTP auth headers used for manifest fetch |

Redaction must be applied before serialization, not after. Never log a raw credential
and then attempt to remove it from the log.

---

## Authentication for Dev Endpoints

If the firmware already has any authentication or token-based access control mechanism
(e.g., an admin token stored in NVS, HTTP Basic Auth, or a session cookie), all
`/api/dev/*` endpoints **must** require that same authentication.

If no existing auth mechanism exists, the `/api/dev/*` endpoints should be protected
by a compile-time token configured via:

| Kconfig Symbol | Default | Description |
|----------------|---------|-------------|
| `CONFIG_X4_DIAG_API_TOKEN` | `""` | Bearer token for `/api/dev/*` endpoints; empty = no auth (development only) |

An empty token is acceptable during early development but must be documented as a
known limitation. See `build-guide.md`.

---

## Agent Diagnostics Mode — Stricter OTA Gate

When `CONFIG_X4_AGENT_DIAGNOSTICS=y`, the OTA health gate requires **all** of the
following stages to pass before calling `esp_ota_mark_app_valid_cancel_rollback()`:

- `wifi` — must be `ok` (not `failed`, not `skipped`)
- `ota` — must be `ok`
- `display` — must be `ok` (no `skipped` allowed)
- `input` — must be `ok` (no `skipped` allowed)
- `storage` — must be `ok`
- `manifest_fetch` — a live manifest fetch must succeed during health checks

This is stricter than the default health gate (see `health-checks.md`) which allows
`display` and `input` to be `skipped` in safe mode.

---

## Development API Endpoints

All endpoints below are compiled and registered **only** when `CONFIG_X4_DIAG_HTTP_API=y`.
They must not appear in release binaries.

### Status & Health

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/dev/status` | Full diagnostics object (see schema above) |
| `GET` | `/api/dev/health` | Health check results JSON (see `health-checks.md`) |
| `GET` | `/api/dev/logs` | Last N lines of the serial log buffer as JSON array of strings |
| `GET` | `/api/dev/ota` | OTA subsystem state: current slot, pending verify, last check result, manifest URL (redacted if credentialed) |
| `GET` | `/api/dev/display` | Display status object (see `display-diagnostics.md`) |
| `GET` | `/api/dev/display/screenshot.bmp` | Framebuffer as BMP; reuse existing screenshot support if present |

### Actions

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/api/dev/display/test-pattern` | Render a named test pattern; body: `{"pattern": "checkerboard"}` |
| `POST` | `/api/dev/ota/check` | Trigger an OTA manifest check; returns manifest JSON or rejection reason |
| `POST` | `/api/dev/ota/apply` | Apply a previously validated manifest (or re-validate and apply); body: `{"confirm": true}` |
| `POST` | `/api/dev/reboot` | Reboot device; body: `{"reason": "agent_request"}` |
| `POST` | `/api/dev/rollback` | Trigger OTA rollback if supported; body: `{"confirm": true}` |

### Prohibitions

- No endpoint may accept or execute arbitrary shell commands or code strings.
- No endpoint may read or return raw NVS entries that contain secrets.
- No endpoint may modify the partition table.
- No endpoint may change bootloader, eFuse, or secure boot settings.

---

## Machine-Readable Health Response

The agent health check (`GET /api/dev/health`) returns a JSON object that the agent
script can parse directly. See the schema in `health-checks.md`. The `status` field
is the primary signal: `"ok"`, `"failed"`, or `"pending"`.

The agent script should:
1. Poll `GET /api/dev/health` after OTA reboot.
2. Retry with backoff until `status != "pending"` or timeout.
3. Report `pass` if `status == "ok"`, `fail` otherwise.
4. Include `last_failed_stage` and `last_failed_reason` in the failure report.

---

## Build Configurations

| Build Type | `DEV_DIAGNOSTICS` | `AGENT_DIAGNOSTICS` | `DIAG_HTTP_API` |
|------------|:-----------------:|:-------------------:|:---------------:|
| Production / release | `n` | `n` | `n` |
| Dev diagnostics | `y` | `n` | `y` |
| Agent diagnostics | `y` | `y` | `y` |

See `build-guide.md` for the exact build commands for each configuration.

---

## Related Documents

- `health-checks.md` — health check stages and rollback gate
- `api.md` — full API endpoint list including standard endpoints
- `display-diagnostics.md` — display status object and verbose display flags
- `serial-log-markers.md` — `DEV_DIAGNOSTICS_ENABLED` marker
- `build-guide.md` — build commands for each configuration
- `safety-rules.md` — diagnostics safety invariants
