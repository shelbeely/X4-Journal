# Web API Spec

## Overview

This document specifies all HTTP API endpoints for the Xteink X4 firmware.

Endpoints are organized into three groups:
1. **Standard endpoints** — always present when a web server is included in the build.
2. **Display endpoints** — always present when the display subsystem and web server are both included.
3. **Dev/diagnostics endpoints** — present only when `CONFIG_X4_DIAG_HTTP_API=y`.

**Fallback:** If the firmware does not include a web server, all OTA and health state
is exposed exclusively through serial log markers (see `serial-log-markers.md`).
The internal OTA and health check logic still runs; only the HTTP interface is absent.

---

## General Conventions

- All request and response bodies are UTF-8 JSON unless otherwise noted.
- `Content-Type: application/json` is set on all JSON responses.
- HTTP status codes follow standard semantics:
  - `200 OK` — success
  - `400 Bad Request` — invalid or missing request parameters
  - `401 Unauthorized` — authentication required (dev endpoints)
  - `404 Not Found` — unknown route
  - `409 Conflict` — operation rejected (e.g., OTA already in progress)
  - `500 Internal Server Error` — firmware-side error
- Secrets must never appear in any response body. See `dev-diagnostics.md` for redaction rules.
- No endpoint may accept or execute arbitrary shell commands.

---

## 1. Standard Endpoints

These endpoints are always registered when a web server exists, regardless of
diagnostic build flags.

### `GET /api/version`

Returns the running firmware version and OTA slot.

**Response:**
```json
{
  "version": "x4-agent-dev-42",
  "slot": "ota_1",
  "pending_verify": false
}
```

---

### `GET /api/health`

Returns the health status object. See `health-checks.md` for the full schema.

**Response:**
```json
{
  "status": "ok",
  "version": "x4-agent-dev-42",
  "slot": "ota_1",
  "pending_verify": false,
  "boot": "ok",
  "reset_reason": "poweron",
  "storage": "ok",
  "wifi": "ok",
  "internet": "ok",
  "ota": "ok",
  "display": "ok",
  "input": "ok",
  "heap": "ok",
  "heap_free": 123456,
  "logs": "ok",
  "safe_mode": false,
  "crash_loop_count": 0,
  "last_failed_stage": null,
  "last_failed_reason": null
}
```

---

### `GET /api/logs`

Returns recent log lines from the in-memory log buffer, if the buffer is implemented.

**Response:**
```json
{
  "lines": [
    "[X4] BOOT_START version=x4-agent-dev-42 slot=ota_1",
    "[X4] WIFI_OK ip=192.168.1.55 rssi=-62",
    "[X4] HEALTH_OK version=x4-agent-dev-42 slot=ota_1"
  ],
  "truncated": false
}
```

If the log buffer is not implemented, returns `501 Not Implemented` with:
```json
{"error": "log_buffer_not_available"}
```

---

### `POST /api/ota/check`

Triggers an OTA manifest check. The device fetches and validates the manifest but
does not download or apply the firmware.

**Response (update available):**
```json
{
  "result": "update_available",
  "version": "x4-agent-dev-43",
  "current_version": "x4-agent-dev-42"
}
```

**Response (up to date):**
```json
{
  "result": "up_to_date",
  "version": "x4-agent-dev-42"
}
```

**Response (rejected):**
```json
{
  "result": "rejected",
  "reason": "wrong_channel"
}
```

---

### `POST /api/ota/apply`

Downloads and applies the most recently validated manifest. A manifest check must
succeed before this endpoint will proceed.

**Request body:**
```json
{"confirm": true}
```

**Response (accepted):**
```json
{
  "result": "applying",
  "version": "x4-agent-dev-43",
  "message": "Downloading firmware. Device will reboot when complete."
}
```

**Response (no validated manifest):**
```json
{"result": "rejected", "reason": "no_validated_manifest"}
```

---

### `POST /api/ota/rollback`

Triggers OTA rollback to the previous valid slot. Only available if ESP-IDF rollback
APIs are present and the current slot is in `PENDING_VERIFY` state.

**Request body:**
```json
{"confirm": true}
```

**Response:**
```json
{
  "result": "rolling_back",
  "message": "Device will reboot into previous slot."
}
```

**Response (rollback not available):**
```json
{"result": "rejected", "reason": "rollback_not_supported_or_not_pending"}
```

---

## 2. Display Endpoints

These endpoints are registered when the display subsystem and web server are both present.

### `GET /api/display/status`

Returns the display status object. See `display-diagnostics.md` for the full schema.

---

### `GET /api/display/screenshot.bmp`

Returns the current framebuffer contents as a BMP image.

- `Content-Type: image/bmp`
- Reuse existing screenshot support if present; do not duplicate it.
- Returns `501 Not Implemented` if framebuffer screenshot is not supported.

---

### `GET /api/display/logs`

Returns recent display-related log lines from the log buffer.

**Response:**
```json
{
  "lines": [
    "[X4] DISPLAY_INIT_OK driver=gdew0213b74 rotation=0",
    "[X4] DISPLAY_FULL_REFRESH_OK duration_ms=1820"
  ]
}
```

---

### `POST /api/display/test-pattern`

Renders a named test pattern. See `display-diagnostics.md` for the pattern catalog.

**Request body:**
```json
{"pattern": "checkerboard"}
```

**Response:**
```json
{
  "result": "ok",
  "pattern": "checkerboard",
  "refresh_duration_ms": 1820
}
```

---

### `POST /api/display/refresh/full`

Triggers a full panel refresh using the current framebuffer contents.

**Response:**
```json
{"result": "ok", "duration_ms": 1820}
```

---

### `POST /api/display/refresh/partial`

Triggers a partial refresh of the specified region.

**Request body:**
```json
{"x": 0, "y": 0, "w": 64, "h": 32}
```

**Response:**
```json
{"result": "ok", "duration_ms": 240}
```

---

### `POST /api/display/clear`

Renders `all_white` and issues a full refresh.

**Response:**
```json
{"result": "ok", "duration_ms": 1820}
```

---

## 3. Dev / Diagnostics Endpoints

**Only compiled and registered when `CONFIG_X4_DIAG_HTTP_API=y`.**
These endpoints require authentication if an auth mechanism exists (see `dev-diagnostics.md`).

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/dev/status` | Full diagnostics object |
| `GET` | `/api/dev/health` | Health check results |
| `GET` | `/api/dev/logs` | Recent log lines |
| `GET` | `/api/dev/ota` | OTA subsystem state |
| `GET` | `/api/dev/display` | Display status |
| `GET` | `/api/dev/display/screenshot.bmp` | Framebuffer BMP |
| `POST` | `/api/dev/display/test-pattern` | Render test pattern |
| `POST` | `/api/dev/ota/check` | Trigger manifest check |
| `POST` | `/api/dev/ota/apply` | Apply validated manifest |
| `POST` | `/api/dev/reboot` | Reboot device |
| `POST` | `/api/dev/rollback` | Trigger OTA rollback |

Full request/response schemas for dev endpoints are specified in `dev-diagnostics.md`.

---

## Related Documents

- `ota.md` — OTA state machine
- `health-checks.md` — health check response schema
- `display-diagnostics.md` — display status schema and test patterns
- `dev-diagnostics.md` — dev endpoint authentication and diagnostics object
- `serial-log-markers.md` — fallback when no web server is present
- `safety-rules.md` — API security invariants
