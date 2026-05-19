#include "web_server.h"
#include "api_entries.h"
#include "api_prompts.h"
#include "api_export.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;

/* Serve the static editor SPA from SD card or embedded fallback */
static esp_err_t root_handler(httpd_req_t *req)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/index.html", WEB_ROOT);

    char *buf = NULL;
    size_t len = 0;
    if (sdcard_read_file(path, &buf, &len) == ESP_OK) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, buf, len);
        free(buf);
    } else {
        /* minimal fallback if SD file is absent */
        const char *fallback =
            "<!DOCTYPE html><html><head><title>PocketShrine</title></head>"
            "<body><h1>PocketShrine</h1>"
            "<p>Place index.html in /sdcard/web/ for the full editor.</p>"
            "<p><a href='/api/entries'>API: entries</a></p></body></html>";
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, fallback);
    }
    return ESP_OK;
}

/* CORS pre-flight for browser clients */
static esp_err_t options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin",  "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 24;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* root handler */
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_register_uri_handler(s_server, &root);

    /* CORS */
    httpd_uri_t opts = { .uri = "/*", .method = HTTP_OPTIONS, .handler = options_handler };
    httpd_register_uri_handler(s_server, &opts);

    /* API modules */
    api_entries_register(s_server);
    api_prompts_register(s_server);
    api_export_register(s_server);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool web_server_is_running(void)
{
    return s_server != NULL;
}
