#include <Arduino.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "platform/nvs_utils.h"
#include "platform/safe_mode.h"
#include "platform/sdcard.h"
#include "platform/display.h"
#include "platform/buttons.h"
#include "platform/rtc.h"
#include "platform/power.h"
#include "platform/wifi.h"
#include "platform/log_buffer.h"
#include "platform/health_check.h"

#include "storage/journal_fs.h"
#include "storage/metadata_index.h"

#include "app/prompt_engine.h"
#include "app/entry_editor.h"
#include "app/journal_app.h"

#include "crypto/vault.h"
#include "ota/ota_manager.h"
#include "web/web_server.h"

static const char *TAG = "main";

/* FIRMWARE_VERSION, GIT_COMMIT, BUILD_TIMESTAMP are injected by get_build_flags.py */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.0"
#endif

/* Forward-declare button callback (defined in journal_app.c via queue) */
static void _btn_noop(button_event_t ev, void *ctx) { (void)ev; (void)ctx; }

void setup(void)
{
    /* ------------------------------------------------------------------ *
     * Step 0: Log ring buffer — install vprintf hook first so we capture
     *         all boot log output including BOOT_START.
     * ------------------------------------------------------------------ */
    log_buffer_init();

    /* ------------------------------------------------------------------ *
     * Step 1: BOOT_START — emitted before any subsystem init.
     * ------------------------------------------------------------------ */
    const esp_partition_t *run_part = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "[X4] BOOT_START version=%s slot=%s",
             FIRMWARE_VERSION, run_part ? run_part->label : "unknown");

#ifdef CONFIG_X4_DEV_DIAGNOSTICS
    ESP_LOGI(TAG, "[X4] DEV_DIAGNOSTICS_ENABLED agent=%s verbose_display=0 http_api=%s",
#ifdef CONFIG_X4_AGENT_DIAGNOSTICS
             "1",
#else
             "0",
#endif
#ifdef CONFIG_X4_DIAG_HTTP_API
             "1"
#else
             "0"
#endif
    );
#endif

    /* ------------------------------------------------------------------ *
     * Step 2: NVS utils — must come before anything that reads/writes NVS
     * ------------------------------------------------------------------ */
    nvs_utils_init();
    nvs_utils_boot_count_increment();

    /* Mark this boot as potentially crashed until BOOT_OK resets it */
    nvs_utils_set_crashed(true);

    /* ------------------------------------------------------------------ *
     * Step 3: Safe mode detection — check before any other init.
     * ------------------------------------------------------------------ */
    safe_mode_check_entry();
    if (safe_mode_is_active()) {
        safe_mode_run();  /* blocks indefinitely */
        return;
    }

    /* ------------------------------------------------------------------ *
     * Normal boot continues below
     * ------------------------------------------------------------------ */

    /* 4. SD card */
    esp_err_t ret = sdcard_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(ret));
    }

    /* 5. Display */
    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
    }

    /* 6. Splash screen */
    display_clear();
    display_draw_text(30, 80,  "PocketShrine", 2);
    display_draw_text(60, 100, "v" FIRMWARE_VERSION, 1);
    display_full_refresh();

    /* 7. RTC */
    rtcdrv_init();

    /* 8. Power */
    power_init();

    /* 9. Vault */
    vault_init();

    /* 10. Journal filesystem */
    if (sdcard_is_mounted()) {
        journal_fs_init();
    }

    /* 11. Metadata index */
    if (sdcard_is_mounted()) {
        ESP_LOGI(TAG, "Rebuilding metadata index...");
        index_rebuild();
    }

    /* 12. Prompt engine */
    if (sdcard_is_mounted()) {
        prompt_engine_init();
    }

    /* 13. Entry editor */
    entry_editor_init();

    /* 14. Buttons */
    ret = buttons_init(_btn_noop, NULL);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "[X4] INPUT_FAILED reason=%s", esp_err_to_name(ret));
    }

    /* 15. Wi-Fi STA — connect to saved AP for OTA and NTP.
     *     SoftAP is started by wifi_ap_start() called from journal_app
     *     when required; we try STA here for internet connectivity. */
    wifi_sta_connect_saved(15000);

    /* 16. OTA pending-verify gate.
     *     If this is the first boot after an OTA update, run the full
     *     health check pipeline and gate firmware acceptance on results. */
    if (ota_is_pending_verify()) {
        ESP_LOGI(TAG, "[X4] OTA_PENDING_VERIFY slot=%s",
                 run_part ? run_part->label : "unknown");
        health_status_t h = {};
        health_check_run(&h, /*safe_mode=*/false);
        health_check_apply_rollback_gate(&h);
        /* If rollback was triggered, health_check_apply_rollback_gate()
           calls ota_rollback() which calls esp_restart() — we never
           reach the code below in that case. */
    }

    /* ------------------------------------------------------------------ *
     * Step 17: BOOT_OK — all required subsystems successfully initialised.
     * ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "[X4] BOOT_OK version=%s slot=%s",
             FIRMWARE_VERSION, run_part ? run_part->label : "unknown");
    nvs_utils_boot_count_reset();
    nvs_utils_set_crashed(false);

    /* 18. Web server */
    web_server_start();

    /* 19. Journal app task */
    ret = journal_app_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "journal_app_init failed: %s", esp_err_to_name(ret));
    }
    xTaskCreate(journal_app_task, "journal_app", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Boot complete. Running.");
}

void loop(void)
{
    /* All application logic runs in the FreeRTOS journal_app_task.
       Return periodically so the Arduino core can run its housekeeping. */
    vTaskDelay(pdMS_TO_TICKS(1000));
}

