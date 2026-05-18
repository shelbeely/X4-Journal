# Health Checks & Rollback Gate Spec

## Overview

After every OTA update, the new firmware boots into the `PENDING_VERIFY` state.
Before it is permanently accepted, it must pass a sequence of diagnostic stages.
Only when **all required stages pass** is the firmware marked valid.
If any required stage fails, the firmware is rolled back.

This pipeline is part of the life-support infrastructure. It must not be modified
or bypassed during unrelated firmware changes.

---

## Health Check Stages

Stages run in order. Each stage reports `ok` or `failed`. A `failed` result on any
**required** stage immediately aborts the sequence and triggers rollback.

| # | Stage | Required | Pass Criteria |
|---|-------|----------|--------------|
| 1 | `boot` | yes | `app_main`/`setup()` reached without panic |
| 2 | `reset_reason` | yes | No crash-loop detected (≤ N consecutive resets without `BOOT_OK`) |
| 3 | `storage` | yes | NVS partition mounted; SPIFFS/LittleFS initialized if configured |
| 4 | `wifi` | yes | Wi-Fi connected to AP **or** recovery AP started in safe mode |
| 5 | `internet` | yes | TCP connection to manifest server host succeeds (HTTPS HEAD or GET) |
| 6 | `ota` | yes | `esp_ota_get_running_partition()` returns a valid partition; OTA client can reach manifest URL |
| 7 | `display` | yes | Display driver `init()` returns success **and** at least one test pattern renders without timeout |
| 8 | `input` | yes | Button/input subsystem initializes without error |
| 9 | `heap` | yes | `esp_get_free_heap_size()` ≥ `CONFIG_OTA_MIN_HEAP_FOR_OTA` after all init |
| 10 | `logs` | no | Recent boot log buffer is non-empty and contains `BOOT_START` marker |

> **Stage 7 detail:** A display test pattern (at minimum, an all-white or all-black fill)
> must be written to the framebuffer and a full refresh must complete without a
> `DISPLAY_BUSY_TIMEOUT` or `DISPLAY_SPI_ERROR` before this stage reports `ok`.

### Crash-Loop Detection (Stage 2)

A crash-loop is defined as ≥ 3 consecutive resets where `BOOT_OK` was never emitted
in the previous session. The firmware must store a boot-attempt counter in NVS and
reset it only after `BOOT_OK` is emitted. If the counter reaches the threshold,
stage 2 fails and rollback is triggered.

---

## Rollback Gate

```
All stages ok?
    │
    ├── YES → esp_ota_mark_app_valid_cancel_rollback()
    │          emit [X4] OTA_MARK_VALID
    │          reset crash-loop counter in NVS
    │
    └── NO  → log failing stage and reason
               emit [X4] OTA_ROLLBACK_REQUESTED reason=<stage>
               esp_ota_mark_app_invalid_rollback_and_reboot()
```

The rollback call must happen before any user-visible application logic runs that
could leave the device in an inconsistent state.

---

## Health Status JSON Schema

The health status object is used by the web API (`GET /api/health`), the diagnostics
endpoint (`GET /api/dev/health`), and as the payload of serial log lines.

```json
{
  "status": "ok | failed | pending",
  "version": "x4-agent-dev-42",
  "slot": "ota_0 | ota_1 | factory",
  "pending_verify": true,
  "boot": "ok | failed",
  "reset_reason": "poweron | watchdog | panic | brownout | unknown",
  "storage": "ok | failed",
  "wifi": "ok | failed",
  "internet": "ok | failed",
  "ota": "ok | failed",
  "display": "ok | failed | skipped",
  "input": "ok | failed | skipped",
  "heap": "ok | failed",
  "heap_free": 123456,
  "logs": "ok | failed | skipped",
  "safe_mode": false,
  "crash_loop_count": 0,
  "last_failed_stage": null,
  "last_failed_reason": null
}
```

| Field | Description |
|-------|-------------|
| `status` | Overall health: `ok` = all passed, `failed` = at least one required stage failed, `pending` = checks not yet complete |
| `version` | Currently running firmware version string |
| `slot` | Currently running OTA slot name |
| `pending_verify` | `true` if `esp_ota_get_state_partition()` returns `ESP_OTA_IMG_PENDING_VERIFY` |
| `boot` | Whether `app_main` was reached without panic |
| `reset_reason` | Result of `esp_reset_reason()` mapped to a human-readable string |
| `storage` | NVS/filesystem init result |
| `wifi` | Wi-Fi connection result |
| `internet` | Manifest server reachability result |
| `ota` | OTA client init result |
| `display` | Display driver init + test pattern result |
| `input` | Input subsystem init result |
| `heap` | Whether free heap is above threshold |
| `heap_free` | Current free heap in bytes |
| `logs` | Whether boot log buffer is available |
| `safe_mode` | Whether the device entered safe mode this boot |
| `crash_loop_count` | Number of consecutive failed boots (from NVS) |
| `last_failed_stage` | Name of the most recently failed stage, or `null` |
| `last_failed_reason` | Human-readable reason for the failure, or `null` |

---

## Serial Log Output

At the end of the health check pipeline, one of the following is emitted:

```
[X4] HEALTH_OK version=x4-agent-dev-42 slot=ota_1
[X4] HEALTH_FAILED stage=display reason=DISPLAY_BUSY_TIMEOUT
```

Each individual stage also emits its own marker on completion (see `serial-log-markers.md`).

---

## Timing

Health checks run immediately after basic subsystem initialization in `app_main`.
The ESP-IDF OTA watchdog timer typically allows 30–60 seconds before auto-rollback.
All health checks must complete within this window. Stages with external network
dependencies (internet, ota) must have short timeouts (recommended ≤ 10 seconds each).

---

## Agent Diagnostics Mode

When `CONFIG_X4_AGENT_DIAGNOSTICS=y`, the health check gate is stricter:
all of `wifi`, `ota`, `display`, `input`, `storage`, and `manifest_fetch` must pass
before the firmware is marked valid. No stage may be marked `skipped` when running
in agent diagnostics mode.

---

## Related Documents

- `ota.md` — OTA state machine that calls the rollback gate
- `safe-mode.md` — safe mode behavior when health checks are inconclusive
- `display-diagnostics.md` — display health check detail
- `serial-log-markers.md` — log marker reference
- `safety-rules.md` — invariants
