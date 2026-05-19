#include "rtc.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "rtc";

#define NVS_NAMESPACE "rtc"
#define NVS_KEY_UNIX  "unix"

static const char *WEEKDAY_NAMES[] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};
static const char *MONTH_NAMES[] = {
    "","January","February","March","April","May","June",
    "July","August","September","October","November","December"
};

esp_err_t rtc_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* restore last-known time from NVS so clock is reasonable before SNTP */
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t saved = 0;
        if (nvs_get_u32(h, NVS_KEY_UNIX, &saved) == ESP_OK && saved > 1000000000u) {
            struct timeval tv = { .tv_sec = (time_t)saved, .tv_usec = 0 };
            settimeofday(&tv, NULL);
        }
        nvs_close(h);
    }

    ESP_LOGI(TAG, "rtc_init ok");
    return ESP_OK;
}

esp_err_t rtc_get_datetime(rtc_datetime_t *dt)
{
    if (!dt) return ESP_ERR_INVALID_ARG;
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    dt->year    = t.tm_year + 1900;
    dt->month   = t.tm_mon  + 1;
    dt->day     = t.tm_mday;
    dt->hour    = t.tm_hour;
    dt->minute  = t.tm_min;
    dt->second  = t.tm_sec;
    dt->weekday = t.tm_wday;
    return ESP_OK;
}

esp_err_t rtc_set_datetime(const rtc_datetime_t *dt)
{
    if (!dt) return ESP_ERR_INVALID_ARG;
    struct tm t = {
        .tm_year = dt->year - 1900,
        .tm_mon  = dt->month - 1,
        .tm_mday = dt->day,
        .tm_hour = dt->hour,
        .tm_min  = dt->minute,
        .tm_sec  = dt->second,
    };
    time_t unix_t = mktime(&t);
    struct timeval tv = { .tv_sec = unix_t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    /* persist to NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY_UNIX, (uint32_t)unix_t);
        nvs_commit(h);
        nvs_close(h);
    }
    return ESP_OK;
}

uint32_t rtc_get_unix(void)
{
    time_t now;
    time(&now);
    return (uint32_t)now;
}

void rtc_format_date(const rtc_datetime_t *dt, char *buf, size_t len)
{
    if (!dt || !buf) return;
    snprintf(buf, len, "%s, %s %d",
             WEEKDAY_NAMES[dt->weekday % 7],
             MONTH_NAMES[dt->month > 0 && dt->month <= 12 ? dt->month : 0],
             dt->day);
}

void rtc_format_time(const rtc_datetime_t *dt, char *buf, size_t len)
{
    if (!dt || !buf) return;
    int h12 = dt->hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, len, "%d:%02d %s", h12, dt->minute, dt->hour < 12 ? "AM" : "PM");
}

void rtc_format_iso8601(const rtc_datetime_t *dt, char *buf, size_t len)
{
    if (!dt || !buf) return;
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d",
             dt->year, dt->month, dt->day,
             dt->hour, dt->minute, dt->second);
}
