# Display Diagnostics Spec — Remote Display Debugging

## Overview

This document specifies the structured logging, status object, test patterns, and
web API endpoints required for remote display debugging on the Xteink X4's e-paper display.

Display diagnostics are part of the OTA health gate: the firmware cannot be marked
valid unless display initialization and at least one test pattern render successfully.

---

## Structured Log Markers

All markers follow the `[X4] MARKER [key=value ...]` format defined in `serial-log-markers.md`.

| Marker | When emitted | Key-value pairs |
|--------|-------------|-----------------|
| `DISPLAY_INIT_START` | Display driver `init()` called | |
| `DISPLAY_INIT_OK` | Driver initialized successfully | `driver=` `rotation=` `width=` `height=` |
| `DISPLAY_INIT_FAILED` | Driver `init()` returned error | `error=` |
| `DISPLAY_FRAMEBUFFER_ALLOC_OK` | Framebuffer allocated | `size=` (bytes) |
| `DISPLAY_FRAMEBUFFER_ALLOC_FAILED` | Framebuffer allocation failed | `size_requested=` `heap_free=` |
| `DISPLAY_FULL_REFRESH_START` | Full-panel refresh command issued | |
| `DISPLAY_FULL_REFRESH_OK` | Full-panel refresh completed | `duration_ms=` |
| `DISPLAY_PARTIAL_REFRESH_START` | Partial refresh command issued | `x=` `y=` `w=` `h=` |
| `DISPLAY_PARTIAL_REFRESH_OK` | Partial refresh completed | `duration_ms=` |
| `DISPLAY_REFRESH_OK` | Generic refresh complete (use full/partial when known) | `duration_ms=` |
| `DISPLAY_BUSY_TIMEOUT` | Busy-pin wait exceeded timeout | `timeout_ms=` |
| `DISPLAY_SPI_ERROR` | SPI transaction error | `error=` |
| `DISPLAY_SLEEP_OK` | Display entered sleep/low-power mode | |
| `DISPLAY_WAKE_OK` | Display woken from sleep | |

---

## Display Status Object

The display status object is returned by `GET /api/display/status` and included in
the diagnostics object from `GET /api/dev/display`.

```json
{
  "driver": "gdew0213b74",
  "width": 250,
  "height": 122,
  "rotation": 0,
  "framebuffer_size": 3844,
  "framebuffer_hash": "a3f1c2d4",
  "last_refresh_type": "full | partial | none",
  "last_refresh_duration_ms": 1820,
  "busy_pin_wait_ms": 1750,
  "last_error": null,
  "heap_free": 123456,
  "init_ok": true,
  "test_pattern_last": "checkerboard",
  "test_pattern_result": "ok | failed | none"
}
```

| Field | Description |
|-------|-------------|
| `driver` | Driver name/identifier string |
| `width`, `height` | Display dimensions in pixels |
| `rotation` | Current rotation: 0, 90, 180, or 270 |
| `framebuffer_size` | Allocated framebuffer size in bytes |
| `framebuffer_hash` | CRC32 or first 8 hex chars of SHA-256 of framebuffer contents |
| `last_refresh_type` | Most recent refresh type |
| `last_refresh_duration_ms` | Wall time of most recent refresh in milliseconds |
| `busy_pin_wait_ms` | How long the last refresh waited on the busy pin |
| `last_error` | Error string from most recent failed operation, or `null` |
| `heap_free` | Free heap at time of status query |
| `init_ok` | Whether the display driver is currently in an initialized state |
| `test_pattern_last` | Name of the last test pattern rendered |
| `test_pattern_result` | Whether the last test pattern completed without error |

---

## Test Pattern Catalog

Test patterns are deterministic framebuffer fills. Each pattern is identified by a name
string used in API calls and log output.

| Pattern Name | Description |
|-------------|-------------|
| `all_white` | Fill entire framebuffer with white (0xFF for most e-paper) |
| `all_black` | Fill entire framebuffer with black (0x00) |
| `checkerboard` | 8×8 pixel alternating black/white grid |
| `border` | White fill with a 1-pixel black border and corner labels (TL, TR, BL, BR) |
| `diagonal` | Single-pixel diagonal line from top-left to bottom-right |
| `font_sample` | Render the string "X4 DIAG 0123456789" using the firmware's default font |
| `partial_rect` | Fill a centred 64×32 pixel rectangle black; render with partial refresh |
| `rotation_test` | Render "TOP" at the top edge in the current rotation; useful for verifying orientation |

### Rendering a Test Pattern

1. Fill the framebuffer according to the pattern definition.
2. Issue a full refresh (or partial refresh for `partial_rect`).
3. Wait for the display to become not-busy.
4. Emit `DISPLAY_FULL_REFRESH_OK` or `DISPLAY_PARTIAL_REFRESH_OK`.
5. Update `test_pattern_last` and `test_pattern_result` in the display status object.

A test pattern is considered **failed** if:
- `DISPLAY_FRAMEBUFFER_ALLOC_FAILED` was emitted.
- A `DISPLAY_BUSY_TIMEOUT` or `DISPLAY_SPI_ERROR` occurs during the refresh.
- The driver returns a non-zero error code.

---

## OTA Health Gate

The OTA firmware acceptance gate **fails** (triggering rollback) if any of the following
conditions are true after display initialization:

| Condition | Effect |
|-----------|--------|
| `DISPLAY_INIT_FAILED` emitted | Display health stage = `failed` |
| `DISPLAY_FRAMEBUFFER_ALLOC_FAILED` emitted | Display health stage = `failed` |
| `DISPLAY_BUSY_TIMEOUT` during health check test pattern | Display health stage = `failed` |
| `DISPLAY_SPI_ERROR` during health check test pattern | Display health stage = `failed` |
| Driver reports any non-zero error during the health check test pattern | Display health stage = `failed` |

The health check test pattern used during OTA validation is `all_white` (simplest and
most reliable; requires full refresh).

---

## Safe Mode Access

All test patterns must be accessible from safe mode. The safe mode boot path must
call the test pattern renderer directly (no dependency on the full application UI stack).
Specifically:

- `all_white` is rendered automatically during safe mode display init to verify the
  display is functional.
- Other patterns are available via `POST /api/display/test-pattern` if the web server
  starts in safe mode.

---

## Camera Feedback Integration

If a camera capture script exists in the project (e.g., `tools/capture_display.sh`
or similar), the agent health check pipeline may compare a captured image of the
physical display against the expected test pattern.

Integration points:

1. After rendering a test pattern, the agent script calls the camera capture script.
2. The captured image is compared against a reference image stored in `tools/test-patterns/`.
3. The comparison result (`match` / `mismatch` / `unavailable`) is included in the
   agent health check report under field `camera_verify`.
4. A `mismatch` result is treated as a warning in normal mode and a failure in
   `CONFIG_X4_AGENT_DIAGNOSTICS` mode.

If no camera script exists, `camera_verify` is set to `"unavailable"` and does not
affect the rollback decision.

---

## Web API Endpoints

These endpoints are added when the firmware includes a web server.

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/display/status` | Returns display status JSON object |
| `GET` | `/api/display/screenshot.bmp` | Returns current framebuffer as a BMP image; reuse existing screenshot support if present |
| `GET` | `/api/display/logs` | Returns recent display-related log lines |
| `POST` | `/api/display/test-pattern` | Renders a named test pattern; body: `{"pattern": "checkerboard"}` |
| `POST` | `/api/display/refresh/full` | Triggers a full panel refresh |
| `POST` | `/api/display/refresh/partial` | Triggers partial refresh; body: `{"x":0,"y":0,"w":64,"h":32}` |
| `POST` | `/api/display/clear` | Renders `all_white` and does a full refresh |

If screenshot support already exists in the firmware, `/api/display/screenshot.bmp`
must reuse it rather than duplicating it.

---

## Related Documents

- `health-checks.md` — display health gate in the OTA rollback pipeline
- `safe-mode.md` — display access from safe mode
- `api.md` — full API endpoint list
- `serial-log-markers.md` — display marker reference
- `dev-diagnostics.md` — verbose display diagnostics flag
