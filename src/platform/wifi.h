#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- SoftAP mode (existing) ----------------------------------------------- */
esp_err_t wifi_ap_start(const char *ssid_suffix);
void      wifi_ap_stop(void);
bool      wifi_ap_is_running(void);
void      wifi_get_ap_ip(char *buf, size_t len);

/* ---- Station (STA) mode --------------------------------------------------- */

/* Connect to an AP.  Blocks until connected or timeout_ms elapses.
   Password is never logged.  Returns ESP_OK when an IP is assigned. */
esp_err_t wifi_sta_connect(const char *ssid, const char *password,
                            uint32_t timeout_ms);

/* Connect using SSID/password stored in NVS by wifi_save_credentials(). */
esp_err_t wifi_sta_connect_saved(uint32_t timeout_ms);

/* Disconnect from the current AP. */
void wifi_sta_disconnect(void);

bool  wifi_sta_is_connected(void);
void  wifi_get_sta_ip(char *buf, size_t len);
int8_t wifi_get_rssi(void);

/* Persist credentials in NVS (namespace "wifi", keys "ssid"/"pass").
   Password is written to NVS but never logged. */
esp_err_t wifi_save_credentials(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
