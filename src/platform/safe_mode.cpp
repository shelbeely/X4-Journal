/* safe_mode.cpp — Physical-button safe mode entry and minimal recovery boot.
 *
 * Safe mode is life-support infrastructure.  It must never be removed or made
 * unreachable.  Changes to this file require explicit justification.
 */
#include "safe_mode.h"
#include "nvs_utils.h"
#include "wifi.h"
#include "display.h"
#include "ota_manager.h"
#include "health_check.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "safe_mode";
static bool s_active = false;

/* ---- safe_mode_check_entry ----------------------------------------------- */

void safe_mode_check_entry(void)
{
    /* Configure safe-mode GPIO as input with pull-up (POWER button = GPIO3) */
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << CONFIG_SAFE_MODE_GPIO);
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    /* Quick debounce: if not pressed initially, skip */
    if (gpio_get_level((gpio_num_t)CONFIG_SAFE_MODE_GPIO) != 0) {
        return; /* button not held */
    }

    /* Button is LOW — wait to confirm hold */
    uint32_t held_ms = 0;
    while (held_ms < CONFIG_SAFE_MODE_HOLD_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
        held_ms += 50;
        if (gpio_get_level((gpio_num_t)CONFIG_SAFE_MODE_GPIO) != 0) {
            return; /* released before threshold */
        }
    }

    /* Threshold reached — enter safe mode */
    s_active = true;
    nvs_utils_set_safe_mode_entered(true);

    const esp_app_desc_t *app  = esp_app_get_description();
    const esp_partition_t *part = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "[X4] SAFE_MODE_ENTERED version=%s slot=%s",
             app  ? app->version  : "unknown",
             part ? part->label   : "unknown");
}

/* ---- safe_mode_is_active ------------------------------------------------- */

bool safe_mode_is_active(void)
{
    return s_active;
}

/* ---- Recovery status display --------------------------------------------- */

static void render_recovery_screen(const char *wifi_status, const char *wifi_ip,
                                   const char *ota_status)
{
    const esp_app_desc_t *app  = esp_app_get_description();
    const esp_partition_t *part = esp_ota_get_running_partition();

    display_clear();

    display_draw_rect(0, 0, X4_DISPLAY_WIDTH, X4_DISPLAY_HEIGHT, 0);  /* border */

    char line[64];
    int y = 10;
    display_draw_text(10, y, "SAFE MODE", 2);     y += 24;
    display_draw_line(10, y, X4_DISPLAY_WIDTH - 10, y); y += 8;

    snprintf(line, sizeof(line), "Version: %s",
             app ? app->version : "unknown");
    display_draw_text(10, y, line, 1); y += 14;

    snprintf(line, sizeof(line), "Slot:    %s",
             part ? part->label : "unknown");
    display_draw_text(10, y, line, 1); y += 14;

    snprintf(line, sizeof(line), "Wi-Fi:   %s  %s", wifi_status, wifi_ip);
    display_draw_text(10, y, line, 1); y += 14;

    snprintf(line, sizeof(line), "OTA:     %s", ota_status);
    display_draw_text(10, y, line, 1);

    display_full_refresh();
}

/* ---- safe_mode_run ------------------------------------------------------- */

void safe_mode_run(void)
{
    /* Step 1-2: already emitted SAFE_MODE_ENTERED in safe_mode_check_entry() */
    const esp_app_desc_t *app  = esp_app_get_description();
    const esp_partition_t *part = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "[X4] CURRENT_VERSION version=%s",
             app ? app->version : "unknown");
    ESP_LOGI(TAG, "[X4] CURRENT_SLOT slot=%s",
             part ? part->label : "unknown");

    /* Step 3: NVS */
    nvs_utils_init();

    /* Step 4: Wi-Fi — connect using saved credentials */
    char wifi_ip[24]     = "N/A";
    char wifi_status[16] = "FAILED";
    esp_err_t wifi_err = wifi_sta_connect_saved(15000);
    if (wifi_err == ESP_OK) {
        wifi_get_sta_ip(wifi_ip, sizeof(wifi_ip));
        strncpy(wifi_status, "OK", sizeof(wifi_status)-1);
    }

    /* Step 5: OTA subsystem readiness */
    char ota_status[16] = "UNAVAILABLE";
    const char *murl = CONFIG_OTA_MANIFEST_URL;
    if (murl && murl[0]) {
        if (ota_can_reach_manifest() == ESP_OK) {
            strncpy(ota_status, "READY", sizeof(ota_status)-1);
            ESP_LOGI(TAG, "[X4] OTA_READY");
        } else {
            ESP_LOGI(TAG, "[X4] OTA_UNAVAILABLE reason=manifest_unreachable");
        }
    } else {
        ESP_LOGI(TAG, "[X4] OTA_UNAVAILABLE reason=no_manifest_url");
    }

    /* Step 6: display */
    esp_err_t disp_err = display_init();
    bool      disp_ok  = (disp_err == ESP_OK);
    if (disp_ok) {
        render_recovery_screen(wifi_status, wifi_ip, ota_status);
    }

    /* Step 7: Run health check pipeline (display/input may be skipped) */
    health_status_t h = {};
    health_check_run(&h, /*safe_mode=*/true);

    /* In safe mode the OTA pending state is preserved — we do NOT mark valid
       or rollback here; that is deferred to operator action via /api/ota/*. */

    /* Step 8: idle loop */
    ESP_LOGI(TAG, "Safe mode ready. Use /api/ota/check and /api/ota/apply to recover.");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
