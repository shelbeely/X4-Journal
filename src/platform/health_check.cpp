/* health_check.cpp — OTA health-check pipeline & rollback gate.
 *
 * Stages run in order.  A failed "required" stage immediately marks overall
 * status as "failed" and the rollback gate will trigger.
 *
 * Safe mode relaxes the display and input stages to "skipped".
 * CONFIG_X4_AGENT_DIAGNOSTICS disallows any "skipped" stage and adds a live
 * manifest_fetch stage.
 */
#include "health_check.h"
#include "nvs_utils.h"
#include "wifi.h"
#include "display.h"
#include "buttons.h"
#include "sdcard.h"
#include "log_buffer.h"
#include "ota_manager.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "health";

/* ---- Reset reason mapping ------------------------------------------------- */
static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
    }
}

/* ---- Helpers -------------------------------------------------------------- */
static void mark_failed(health_status_t *out, const char *stage, const char *reason)
{
    strncpy(out->status, HEALTH_FAILED, sizeof(out->status)-1);
    strncpy(out->last_failed_stage,  stage,  sizeof(out->last_failed_stage)-1);
    strncpy(out->last_failed_reason, reason, sizeof(out->last_failed_reason)-1);
}

/* ---- health_check_run ----------------------------------------------------- */

void health_check_run(health_status_t *out, bool safe_mode)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    strncpy(out->status, HEALTH_PENDING, sizeof(out->status)-1);

    /* Populate version and slot */
    const esp_app_desc_t *app = esp_app_get_description();
    strncpy(out->version, app ? app->version : "unknown", sizeof(out->version)-1);
    const esp_partition_t *part = esp_ota_get_running_partition();
    strncpy(out->slot, part ? part->label : "unknown", sizeof(out->slot)-1);
    out->safe_mode = safe_mode;

    /* Stage 1: boot — if we reached this code, boot succeeded */
    strncpy(out->boot, HEALTH_OK, sizeof(out->boot)-1);

    /* Stage 2: reset_reason + crash-loop detection */
    strncpy(out->reset_reason, reset_reason_str(), sizeof(out->reset_reason)-1);
    out->crash_loop_count = nvs_utils_boot_count_get();
    if (out->crash_loop_count >= CRASH_LOOP_THRESHOLD) {
        mark_failed(out, "reset_reason", "crash_loop");
        ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=reset_reason reason=crash_loop count=%lu",
                 (unsigned long)out->crash_loop_count);
        health_check_apply_rollback_gate(out);
        return;
    }

    /* Stage 3: storage */
    bool sd_ok = sdcard_is_mounted();
    strncpy(out->storage, sd_ok ? HEALTH_OK : HEALTH_FAILED, sizeof(out->storage)-1);
    if (!sd_ok) {
        mark_failed(out, "storage", "sdcard_not_mounted");
        ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=storage reason=sdcard_not_mounted");
        health_check_apply_rollback_gate(out);
        return;
    }

    /* Stage 4: wifi */
    bool wifi_ok = wifi_sta_is_connected() || wifi_ap_is_running();
    strncpy(out->wifi, wifi_ok ? HEALTH_OK : HEALTH_FAILED, sizeof(out->wifi)-1);
    if (!wifi_ok) {
        if (!safe_mode) {
            mark_failed(out, "wifi", "not_connected");
            ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=wifi reason=not_connected");
            health_check_apply_rollback_gate(out);
            return;
        }
        /* In safe mode WiFi failure is non-fatal */
        ESP_LOGW(TAG, "Health: wifi failed (safe mode — continuing)");
    }

    /* Stage 5: internet — manifest server reachability */
    esp_err_t inet_err = ota_can_reach_manifest();
    strncpy(out->internet, inet_err == ESP_OK ? HEALTH_OK : HEALTH_FAILED,
            sizeof(out->internet)-1);
    if (inet_err != ESP_OK) {
        if (!safe_mode) {
            mark_failed(out, "internet", "manifest_unreachable");
            ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=internet reason=manifest_unreachable");
            health_check_apply_rollback_gate(out);
            return;
        }
        ESP_LOGW(TAG, "Health: internet check failed (safe mode — continuing)");
    }

    /* Stage 6: OTA partition */
    out->pending_verify = ota_is_pending_verify();
    strncpy(out->ota, part ? HEALTH_OK : HEALTH_FAILED, sizeof(out->ota)-1);
    if (!part) {
        mark_failed(out, "ota", "no_running_partition");
        ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=ota reason=no_running_partition");
        health_check_apply_rollback_gate(out);
        return;
    }

    /* Stage 7: display — init + test pattern */
    if (safe_mode && !display_get_init_ok()) {
        strncpy(out->display, HEALTH_SKIPPED, sizeof(out->display)-1);
        ESP_LOGW(TAG, "Health: display skipped (safe mode)");
    } else {
        bool disp_ok = display_get_init_ok();
        if (disp_ok) {
            esp_err_t pat_err = display_render_test_pattern("all_white");
            disp_ok = (pat_err == ESP_OK);
        }
        strncpy(out->display, disp_ok ? HEALTH_OK : HEALTH_FAILED,
                sizeof(out->display)-1);
        if (!disp_ok) {
            mark_failed(out, "display", "display_test_pattern_failed");
            ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=display reason=display_test_pattern_failed");
#ifdef CONFIG_X4_AGENT_DIAGNOSTICS
            health_check_apply_rollback_gate(out);
            return;
#else
            if (!safe_mode) {
                health_check_apply_rollback_gate(out);
                return;
            }
#endif
        }
    }

    /* Stage 8: input */
    if (safe_mode && !buttons_is_init()) {
        strncpy(out->input, HEALTH_SKIPPED, sizeof(out->input)-1);
        ESP_LOGW(TAG, "Health: input skipped (safe mode)");
    } else {
        bool inp_ok = buttons_is_init();
        strncpy(out->input, inp_ok ? HEALTH_OK : HEALTH_FAILED, sizeof(out->input)-1);
        if (!inp_ok) {
            mark_failed(out, "input", "buttons_not_init");
            ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=input reason=buttons_not_init");
#ifdef CONFIG_X4_AGENT_DIAGNOSTICS
            health_check_apply_rollback_gate(out);
            return;
#else
            if (!safe_mode) {
                health_check_apply_rollback_gate(out);
                return;
            }
#endif
        }
    }

    /* Stage 9: heap */
    out->heap_free = esp_get_free_heap_size();
    bool heap_ok = out->heap_free >= CONFIG_OTA_MIN_HEAP_FOR_OTA;
    strncpy(out->heap, heap_ok ? HEALTH_OK : HEALTH_FAILED, sizeof(out->heap)-1);
    if (!heap_ok) {
        mark_failed(out, "heap", "low_heap");
        ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=heap reason=low_heap free=%lu",
                 (unsigned long)out->heap_free);
        health_check_apply_rollback_gate(out);
        return;
    }

    /* Stage 10: logs (optional) */
    strncpy(out->logs, log_buffer_is_init() ? HEALTH_OK : HEALTH_SKIPPED,
            sizeof(out->logs)-1);

#ifdef CONFIG_X4_AGENT_DIAGNOSTICS
    /* Agent diagnostics: extra manifest_fetch stage */
    esp_err_t mf = ota_can_reach_manifest();
    if (mf != ESP_OK) {
        mark_failed(out, "manifest_fetch", "unreachable");
        ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=manifest_fetch reason=unreachable");
        health_check_apply_rollback_gate(out);
        return;
    }
    /* Disallow any "skipped" stage in agent mode */
    if (strcmp(out->display, HEALTH_SKIPPED) == 0 ||
        strcmp(out->input,   HEALTH_SKIPPED) == 0) {
        mark_failed(out, "agent_gate", "skipped_stage_not_allowed");
        ESP_LOGE(TAG, "[X4] HEALTH_FAILED stage=agent_gate reason=skipped_stage_not_allowed");
        health_check_apply_rollback_gate(out);
        return;
    }
#endif

    /* All stages passed */
    strncpy(out->status, HEALTH_OK, sizeof(out->status)-1);
    ESP_LOGI(TAG, "[X4] HEALTH_OK version=%s slot=%s", out->version, out->slot);
}

/* ---- health_check_is_pending_verify -------------------------------------- */

bool health_check_is_pending_verify(void)
{
    return ota_is_pending_verify();
}

/* ---- health_check_apply_rollback_gate ------------------------------------ */

void health_check_apply_rollback_gate(const health_status_t *status)
{
    if (!status) return;
    if (strcmp(status->status, HEALTH_OK) == 0) {
        ota_mark_valid();
        nvs_utils_boot_count_reset();
        nvs_utils_set_crashed(false);
    } else {
        ota_rollback(status->last_failed_reason[0]
                         ? status->last_failed_reason
                         : status->last_failed_stage);
        /* ota_rollback() restarts the device — code below not normally reached */
    }
}

/* ---- JSON serialisation -------------------------------------------------- */

char *health_status_to_json(const health_status_t *s)
{
    if (!s) return nullptr;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status",            s->status);
    cJSON_AddStringToObject(root, "version",           s->version);
    cJSON_AddStringToObject(root, "slot",              s->slot);
    cJSON_AddBoolToObject  (root, "pending_verify",    s->pending_verify);
    cJSON_AddStringToObject(root, "boot",              s->boot);
    cJSON_AddStringToObject(root, "reset_reason",      s->reset_reason);
    cJSON_AddStringToObject(root, "storage",           s->storage);
    cJSON_AddStringToObject(root, "wifi",              s->wifi);
    cJSON_AddStringToObject(root, "internet",          s->internet);
    cJSON_AddStringToObject(root, "ota",               s->ota);
    cJSON_AddStringToObject(root, "display",           s->display);
    cJSON_AddStringToObject(root, "input",             s->input);
    cJSON_AddStringToObject(root, "heap",              s->heap);
    cJSON_AddNumberToObject(root, "heap_free",         s->heap_free);
    cJSON_AddStringToObject(root, "logs",              s->logs);
    cJSON_AddBoolToObject  (root, "safe_mode",         s->safe_mode);
    cJSON_AddNumberToObject(root, "crash_loop_count",  s->crash_loop_count);
    if (s->last_failed_stage[0]) {
        cJSON_AddStringToObject(root, "last_failed_stage",  s->last_failed_stage);
        cJSON_AddStringToObject(root, "last_failed_reason", s->last_failed_reason);
    } else {
        cJSON_AddNullToObject(root, "last_failed_stage");
        cJSON_AddNullToObject(root, "last_failed_reason");
    }
    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    return str;
}
