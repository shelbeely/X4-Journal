#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month;    /* 1–12 */
    int day;      /* 1–31 */
    int hour;     /* 0–23 */
    int minute;
    int second;
    int weekday;  /* 0=Sunday */
} rtc_datetime_t;

esp_err_t rtcdrv_init(void);
esp_err_t rtc_get_datetime(rtc_datetime_t *dt);
esp_err_t rtc_set_datetime(const rtc_datetime_t *dt);
uint32_t  rtc_get_unix(void);
void      rtc_format_date(const rtc_datetime_t *dt, char *buf, size_t len);     /* "Sunday, May 17"    */
void      rtc_format_time(const rtc_datetime_t *dt, char *buf, size_t len);     /* "11:42 PM"          */
void      rtc_format_iso8601(const rtc_datetime_t *dt, char *buf, size_t len);  /* "2026-05-17T21:04:00" */

#ifdef __cplusplus
}
#endif
