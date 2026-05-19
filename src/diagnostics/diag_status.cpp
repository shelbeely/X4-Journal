/* diag_status.cpp — Full structured diagnostics object for dev/agent builds.
 *
 * Compiled only when CONFIG_X4_DEV_DIAGNOSTICS is defined.
 * All secrets (Wi-Fi password, tokens, keys) are redacted before inclusion.
 */
#ifdef CONFIG_X4_DEV_DIAGNOSTICS

#include "diag_status.h"
#include "health_check.h"
#include "ota_manager.h"
#include "nvs_utils.h"
#include "wifi.h"
#include "display.h"
#include "buttons.h"
#include "safe_mode.h"
#include "log_buffer.h"
#include "sdcard.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "diag";

/* Inject these via get_build_flags.py / build_flags */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP "unknown"
#endif
#ifndef CONFIG_OTA_CHANNEL
#define CONFIG_OTA_CHANNEL "dev"
#endif

/* ---- Chip model string ---------------------------------------------------- */
static const char *chip_model_str(esp_chip_model_t m)
{
    switch (m) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    default:           return "unknown";
    }
}

/* ---- diag_get_status ------------------------------------------------------ */

esp_err_t diag_get_status(char *json_out, size_t len)
{
    if (!json_out || len == 0) return ESP_ERR_INVALID_ARG;

    /* ---- Chip info ---- */
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);

    /* ---- OTA state ---- */
    const esp_partition_t *part = esp_ota_get_running_partition();
    bool pending   = ota_is_pending_verify();
    bool rollback_ok = false;
    if (part) {
        esp_ota_img_states_t state;
        rollback_ok = (esp_ota_get_state_partition(part, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_PENDING_VERIFY);
    }

    /* ---- Wi-Fi ---- */
    char sta_ip[24]   = "N/A";
    int8_t rssi       = 0;
    bool sta_conn     = wifi_sta_is_connected();
    if (sta_conn) {
        wifi_get_sta_ip(sta_ip, sizeof(sta_ip));
        rssi = wifi_get_rssi();
    }

    /* ---- Display ---- */
    display_status_t ds = {};
    display_get_status(&ds);

    /* ---- Health (fresh run) ---- */
    health_status_t h = {};
    health_check_run(&h, safe_mode_is_active());

    /* ---- Manifest URL (redacted) ---- */
    char manifest_url[128] = {};
    ota_get_manifest_url_redacted(manifest_url, sizeof(manifest_url));

    /* ---- Build JSON ---- */
    cJSON *root = cJSON_CreateObject();

    /* firmware */
    cJSON_AddStringToObject(root, "firmware_version", FIRMWARE_VERSION);
    cJSON_AddStringToObject(root, "git_commit",       GIT_COMMIT);
    cJSON_AddStringToObject(root, "build_timestamp",  BUILD_TIMESTAMP);
    cJSON_AddStringToObject(root, "chip",             chip_model_str(chip.model));
    cJSON_AddNumberToObject(root, "chip_revision",    chip.revision);

    /* OTA */
    cJSON *ota_obj = cJSON_AddObjectToObject(root, "ota");
    cJSON_AddStringToObject(ota_obj, "slot",             part ? part->label : "unknown");
    cJSON_AddBoolToObject  (ota_obj, "pending_verify",   pending);
    cJSON_AddBoolToObject  (ota_obj, "rollback_available", rollback_ok);
    cJSON_AddStringToObject(ota_obj, "channel",          CONFIG_OTA_CHANNEL);
    cJSON_AddStringToObject(ota_obj, "manifest_url",     manifest_url);
    cJSON_AddStringToObject(ota_obj, "credentials",      "[REDACTED]");

    /* boot */
    cJSON *boot_obj = cJSON_AddObjectToObject(root, "boot");
    cJSON_AddStringToObject(boot_obj, "reset_reason", h.reset_reason);
    cJSON_AddNumberToObject(boot_obj, "boot_count",   nvs_utils_boot_count_get());
    cJSON_AddBoolToObject  (boot_obj, "crash_loop",   h.crash_loop_count >= CRASH_LOOP_THRESHOLD);
    cJSON_AddNumberToObject(boot_obj, "crash_loop_count", h.crash_loop_count);
    cJSON_AddBoolToObject  (boot_obj, "safe_mode",    safe_mode_is_active());

    /* heap */
    cJSON *heap_obj = cJSON_AddObjectToObject(root, "heap");
    cJSON_AddNumberToObject(heap_obj, "free",     esp_get_free_heap_size());
    cJSON_AddNumberToObject(heap_obj, "min_free", esp_get_minimum_free_heap_size());

    /* wifi */
    cJSON *wifi_obj = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject  (wifi_obj, "sta_connected", sta_conn);
    cJSON_AddBoolToObject  (wifi_obj, "ap_running",    wifi_ap_is_running());
    cJSON_AddStringToObject(wifi_obj, "ip",            sta_ip);
    cJSON_AddNumberToObject(wifi_obj, "rssi",          rssi);
    cJSON_AddStringToObject(wifi_obj, "password",      "[REDACTED]");

    /* display */
    cJSON *disp_obj = cJSON_AddObjectToObject(root, "display");
    cJSON_AddBoolToObject  (disp_obj, "init_ok",              ds.init_ok);
    cJSON_AddStringToObject(disp_obj, "driver",               ds.driver);
    cJSON_AddNumberToObject(disp_obj, "width",                ds.width);
    cJSON_AddNumberToObject(disp_obj, "height",               ds.height);
    cJSON_AddStringToObject(disp_obj, "last_refresh_type",    ds.last_refresh_type);
    cJSON_AddNumberToObject(disp_obj, "last_refresh_duration_ms", ds.last_refresh_duration_ms);
    cJSON_AddStringToObject(disp_obj, "test_pattern_last",    ds.test_pattern_last);
    cJSON_AddStringToObject(disp_obj, "test_pattern_result",  ds.test_pattern_result);
    cJSON_AddStringToObject(disp_obj, "last_error",           ds.last_error);

    /* input */
    cJSON *inp_obj = cJSON_AddObjectToObject(root, "input");
    cJSON_AddBoolToObject  (inp_obj, "init_ok",      buttons_is_init());
    cJSON_AddNumberToObject(inp_obj, "last_event_id", (int)buttons_last_event_id());

    /* storage */
    cJSON *stor_obj = cJSON_AddObjectToObject(root, "storage");
    cJSON_AddBoolToObject(stor_obj, "sdcard_mounted", sdcard_is_mounted());
    cJSON_AddBoolToObject(stor_obj, "log_buffer_init", log_buffer_is_init());

    /* health */
    cJSON *health_obj = cJSON_AddObjectToObject(root, "health");
    cJSON_AddStringToObject(health_obj, "status",  h.status);
    cJSON_AddStringToObject(health_obj, "wifi",    h.wifi);
    cJSON_AddStringToObject(health_obj, "display", h.display);
    cJSON_AddStringToObject(health_obj, "input",   h.input);
    cJSON_AddStringToObject(health_obj, "storage", h.storage);
    cJSON_AddStringToObject(health_obj, "heap",    h.heap);

    /* diagnostics flags */
    cJSON *flags_obj = cJSON_AddObjectToObject(root, "diag_flags");
    cJSON_AddBoolToObject(flags_obj, "dev_diagnostics",     true);
#ifdef CONFIG_X4_AGENT_DIAGNOSTICS
    cJSON_AddBoolToObject(flags_obj, "agent_diagnostics",   true);
#else
    cJSON_AddBoolToObject(flags_obj, "agent_diagnostics",   false);
#endif
#ifdef CONFIG_X4_DIAG_HTTP_API
    cJSON_AddBoolToObject(flags_obj, "http_api",            true);
#else
    cJSON_AddBoolToObject(flags_obj, "http_api",            false);
#endif

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) return ESP_ERR_NO_MEM;

    if (strlen(str) >= len) {
        free(str);
        return ESP_ERR_NO_MEM;
    }
    strncpy(json_out, str, len - 1);
    json_out[len - 1] = '\0';
    free(str);
    return ESP_OK;
}

#endif /* CONFIG_X4_DEV_DIAGNOSTICS */
