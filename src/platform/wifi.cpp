/* wifi.cpp — WiFi SoftAP + Station platform driver using Arduino WiFi.h */
#include "wifi.h"
#include <WiFi.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";

#define AP_CHANNEL    6
#define AP_MAX_CONN   4
#define STA_NVS_NS    "wifi"
#define STA_KEY_SSID  "ssid"
#define STA_KEY_PASS  "pass"

static bool s_ap_running  = false;
static bool s_sta_connected = false;

/* ---- SoftAP mode ---------------------------------------------------------- */

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

    /* If STA is already running, switch to AP+STA mode */
    if (s_sta_connected) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_AP);
    }
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
    if (!s_sta_connected) {
        WiFi.mode(WIFI_OFF);
    }
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

/* ---- Station (STA) mode --------------------------------------------------- */

esp_err_t wifi_sta_connect(const char *ssid, const char *password,
                            uint32_t timeout_ms)
{
    if (!ssid || !ssid[0]) {
        ESP_LOGE(TAG, "[X4] WIFI_FAILED reason=no_ssid");
        return ESP_ERR_INVALID_ARG;
    }

    /* Set mode; keep AP if it is already running */
    if (s_ap_running) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }

    /* Connect — never log the password */
    WiFi.begin(ssid, password);
    ESP_LOGI(TAG, "STA connecting to: %s", ssid);

    uint32_t start = (uint32_t)millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((uint32_t)(millis() - start) >= timeout_ms) {
            WiFi.disconnect(true);
            ESP_LOGI(TAG, "[X4] WIFI_FAILED reason=timeout");
            return ESP_ERR_TIMEOUT;
        }
        delay(250);
    }

    s_sta_connected = true;
    char ip[24] = {};
    wifi_get_sta_ip(ip, sizeof(ip));
    int8_t rssi = wifi_get_rssi();
    ESP_LOGI(TAG, "[X4] WIFI_OK ip=%s rssi=%d", ip, (int)rssi);
    return ESP_OK;
}

esp_err_t wifi_sta_connect_saved(uint32_t timeout_ms)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(STA_NVS_NS, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "[X4] WIFI_FAILED reason=no_saved_credentials");
        return ESP_ERR_NOT_FOUND;
    }

    char ssid[64] = {};
    char pass[64] = {};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);

    ret = nvs_get_str(h, STA_KEY_SSID, ssid, &ssid_len);
    nvs_get_str(h, STA_KEY_PASS, pass, &pass_len); /* ignore error for pass */
    nvs_close(h);

    if (ret != ESP_OK || !ssid[0]) {
        ESP_LOGI(TAG, "[X4] WIFI_FAILED reason=no_saved_credentials");
        return ESP_ERR_NOT_FOUND;
    }

    return wifi_sta_connect(ssid, pass, timeout_ms);
}

void wifi_sta_disconnect(void)
{
    if (!s_sta_connected) return;
    WiFi.disconnect(true);
    s_sta_connected = false;
    if (!s_ap_running) {
        WiFi.mode(WIFI_OFF);
    }
    ESP_LOGI(TAG, "STA disconnected");
}

bool wifi_sta_is_connected(void)
{
    return WiFi.status() == WL_CONNECTED;
}

void wifi_get_sta_ip(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    WiFi.localIP().toString().toCharArray(buf, len);
}

int8_t wifi_get_rssi(void)
{
    return (int8_t)WiFi.RSSI();
}

esp_err_t wifi_save_credentials(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(STA_NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    nvs_set_str(h, STA_KEY_SSID, ssid);
    if (password) {
        nvs_set_str(h, STA_KEY_PASS, password);
    }
    ret = nvs_commit(h);
    nvs_close(h);

    /* Log SSID but NEVER the password */
    ESP_LOGI(TAG, "Saved WiFi credentials for SSID: %s", ssid);
    return ret;
}

