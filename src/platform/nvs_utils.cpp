/* nvs_utils.cpp — NVS helper and crash-loop / safe-mode tracking.
 *
 * All persistent state for the OTA health-check pipeline is stored in the
 * NVS namespace "x4sys".  This is separate from the "rtc" namespace used
 * by the RTC driver and any other Preferences namespaces in the project.
 */
#include "nvs_utils.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG       = "nvs_utils";
static const char *NAMESPACE = "x4sys";

/* ---- NVS key names -------------------------------------------------------- */
static const char *KEY_BOOT_COUNT  = "boot_cnt";
static const char *KEY_CRASHED     = "crashed";
static const char *KEY_SAFE_ENTERED = "safe_ent";

/* ---- Init ----------------------------------------------------------------- */

esp_err_t nvs_utils_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue (%s) — erasing and reinitialising",
                 esp_err_to_name(ret));
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ---- Boot counter --------------------------------------------------------- */

void nvs_utils_boot_count_increment(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    uint32_t cnt = 0;
    nvs_get_u32(h, KEY_BOOT_COUNT, &cnt); /* ignore error — defaults to 0 */
    cnt++;
    nvs_set_u32(h, KEY_BOOT_COUNT, cnt);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "boot_count → %lu", (unsigned long)cnt);
}

void nvs_utils_boot_count_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, KEY_BOOT_COUNT, 0);
    nvs_commit(h);
    nvs_close(h);
}

uint32_t nvs_utils_boot_count_get(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t cnt = 0;
    nvs_get_u32(h, KEY_BOOT_COUNT, &cnt);
    nvs_close(h);
    return cnt;
}

/* ---- Last-crash flag ------------------------------------------------------ */

void nvs_utils_set_crashed(bool crashed)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, KEY_CRASHED, crashed ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool nvs_utils_was_crashed(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t val = 0;
    nvs_get_u8(h, KEY_CRASHED, &val);
    nvs_close(h);
    return val != 0;
}

/* ---- Safe-mode flag ------------------------------------------------------- */

void nvs_utils_set_safe_mode_entered(bool entered)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, KEY_SAFE_ENTERED, entered ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool nvs_utils_safe_mode_was_entered(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t val = 0;
    nvs_get_u8(h, KEY_SAFE_ENTERED, &val);
    nvs_close(h);
    return val != 0;
}
