#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_ap_start(const char *ssid_suffix);
void      wifi_ap_stop(void);
bool      wifi_ap_is_running(void);
void      wifi_get_ap_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
