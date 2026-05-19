#include <Arduino.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "platform/sdcard.h"
#include "platform/display.h"
#include "platform/buttons.h"
#include "platform/rtc.h"
#include "platform/power.h"
#include "platform/wifi.h"

#include "storage/journal_fs.h"
#include "storage/metadata_index.h"

#include "app/prompt_engine.h"
#include "app/entry_editor.h"
#include "app/journal_app.h"

#include "crypto/vault.h"

static const char *TAG = "main";

#define FIRMWARE_VERSION "0.1.0"

/* Forward-declare button callback (defined in journal_app.c via queue) */
static void _btn_noop(button_event_t ev, void *ctx) { (void)ev; (void)ctx; }

void setup(void)
{
    ESP_LOGI(TAG, "PocketShrine v%s booting...", FIRMWARE_VERSION);

    /* 1. NVS (required by wifi, rtc, vault) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. SD card — must come before any filesystem access */
    ret = sdcard_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(ret));
        /* Continue without SD — display will still work */
    }

    /* 3. Display */
    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
    }

    /* 4. Splash screen */
    display_clear();
    display_draw_text(30, 80,  "PocketShrine", 2);
    display_draw_text(60, 100, "v" FIRMWARE_VERSION, 1);
    display_full_refresh();

    /* 5. RTC */
    rtc_init();

    /* 6. Power */
    power_init();

    /* 7. Vault (loads config from NVS; no passphrase needed at boot) */
    vault_init();

    /* 8. Journal filesystem — create directories if needed */
    if (sdcard_is_mounted()) {
        journal_fs_init();
    }

    /* 9. Metadata index */
    if (sdcard_is_mounted()) {
        ESP_LOGI(TAG, "Rebuilding metadata index...");
        index_rebuild();
    }

    /* 10. Prompt engine */
    if (sdcard_is_mounted()) {
        prompt_engine_init();
    }

    /* 11. Entry editor */
    entry_editor_init();

    /* 12. Buttons — pass NULL callback here; journal_app will register its own
       via the shared button queue set up in journal_app_init */
    buttons_init(_btn_noop, NULL);

    /* 13. Journal app task */
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
