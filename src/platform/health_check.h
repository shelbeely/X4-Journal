#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stage result values (stored as char strings for JSON serialization) */
#define HEALTH_OK      "ok"
#define HEALTH_FAILED  "failed"
#define HEALTH_SKIPPED "skipped"
#define HEALTH_PENDING "pending"

/* Crash-loop threshold: N consecutive boots without BOOT_OK = crash loop */
#define CRASH_LOOP_THRESHOLD 3

/* ---- Health status object (mirrors health-checks.md JSON schema) ---------- */
typedef struct {
    char     status[12];         /* "ok" | "failed" | "pending" */
    char     version[64];
    char     slot[16];
    bool     pending_verify;
    char     boot[12];
    char     reset_reason[24];
    char     storage[12];
    char     wifi[12];
    char     internet[12];
    char     ota[12];
    char     display[12];
    char     input[12];
    char     heap[12];
    uint32_t heap_free;
    char     logs[12];
    bool     safe_mode;
    uint32_t crash_loop_count;
    char     last_failed_stage[32];
    char     last_failed_reason[64];
} health_status_t;

/* ---- Public API ----------------------------------------------------------- */

/* Run the full health check pipeline.  Populates *out.
   If safe_mode is true, display/input may be marked "skipped" instead of
   "failed" and do not gate OTA validity.
   In CONFIG_X4_AGENT_DIAGNOSTICS mode every required stage must be "ok". */
void health_check_run(health_status_t *out, bool safe_mode);

/* Quick helper: returns true if the running partition is PENDING_VERIFY. */
bool health_check_is_pending_verify(void);

/* Apply the rollback gate based on health results.
   Calls ota_mark_valid() on success, ota_rollback() on failure.
   This function may call esp_restart() — it does not return on rollback. */
void health_check_apply_rollback_gate(const health_status_t *status);

/* Serialize health_status_t to a JSON string (heap-allocated, caller free()s). */
char *health_status_to_json(const health_status_t *s);

#ifdef __cplusplus
}
#endif
