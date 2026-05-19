#include "wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi";

#define AP_MAX_CONN    4
#define AP_CHANNEL     6

static bool s_ap_running = false;
static bool s_netif_init = false;

esp_err_t wifi_ap_start(const char *ssid_suffix)
{
    if (s_ap_running) return ESP_OK;

    if (!s_netif_init) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_netif_init = true;
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* build SSID */
    char ssid[32];
    if (ssid_suffix && ssid_suffix[0]) {
        snprintf(ssid, sizeof(ssid), "PocketShrine-%s", ssid_suffix);
    } else {
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_AP, mac);
        snprintf(ssid, sizeof(ssid), "PocketShrine-%02X%02X", mac[4], mac[5]);
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len       = 0,
            .channel        = AP_CHANNEL,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = AP_MAX_CONN,
        },
    };
    strlcpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_running = true;
    ESP_LOGI(TAG, "AP started: %s", ssid);
    return ESP_OK;
}

void wifi_ap_stop(void)
{
    if (!s_ap_running) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    s_ap_running = false;
    ESP_LOGI(TAG, "AP stopped");
}

bool wifi_ap_is_running(void)
{
    return s_ap_running;
}

void wifi_get_ap_ip(char *buf, size_t len)
{
    strlcpy(buf, "192.168.4.1", len);
}
