/* wifi.cpp — WiFi SoftAP platform driver using Arduino WiFi.h */
#include "wifi.h"
#include <WiFi.h>
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";

#define AP_CHANNEL    6
#define AP_MAX_CONN   4

static bool s_ap_running = false;

esp_err_t wifi_ap_start(const char *ssid_suffix)
{
    if (s_ap_running) return ESP_OK;

    char ssid[32];
    if (ssid_suffix && ssid_suffix[0]) {
        snprintf(ssid, sizeof(ssid), "PocketShrine-%s", ssid_suffix);
    } else {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(ssid, sizeof(ssid), "PocketShrine-%02X%02X", mac[4], mac[5]);
    }

    WiFi.mode(WIFI_AP);
    /* Open AP, no password, channel 6, not hidden, max 4 stations */
    if (!WiFi.softAP(ssid, nullptr, AP_CHANNEL, 0, AP_MAX_CONN)) {
        ESP_LOGE(TAG, "softAP failed");
        return ESP_FAIL;
    }

    s_ap_running = true;
    ESP_LOGI(TAG, "AP started: %s  IP: %s", ssid,
             WiFi.softAPIP().toString().c_str());
    return ESP_OK;
}

void wifi_ap_stop(void)
{
    if (!s_ap_running) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    s_ap_running = false;
    ESP_LOGI(TAG, "AP stopped");
}

bool wifi_ap_is_running(void)
{
    return s_ap_running;
}

void wifi_get_ap_ip(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    WiFi.softAPIP().toString().toCharArray(buf, len);
}
