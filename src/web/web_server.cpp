/* web_server.cpp — HTTP server using Arduino WebServer.
 *
 * Runs in a dedicated FreeRTOS task that calls handleClient() in a tight loop
 * so the journal_app_task is not blocked by network I/O.
 */
#include "web_server.h"
#include "api_entries.h"
#include "api_prompts.h"
#include "api_export.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <WebServer.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_server";

static WebServer    *s_server   = nullptr;
static TaskHandle_t  s_task     = nullptr;

/* ---- CORS helper --------------------------------------------------------- */

static void add_cors_headers(void)
{
    s_server->sendHeader("Access-Control-Allow-Origin",  "*");
    s_server->sendHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
    s_server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

/* ---- Root handler -------------------------------------------------------- */

static void root_handler(void)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/index.html", WEB_ROOT);

    char *buf = nullptr;
    size_t len = 0;
    add_cors_headers();
    if (sdcard_read_file(path, &buf, &len) == ESP_OK) {
        s_server->send(200, "text/html", (const char *)buf);
        free(buf);
    } else {
        const char *fallback =
            "<!DOCTYPE html><html><head><title>PocketShrine</title></head>"
            "<body><h1>PocketShrine</h1>"
            "<p>Place index.html in /sdcard/web/ for the full editor.</p>"
            "<p><a href='/api/entries'>API: entries</a></p></body></html>";
        s_server->send(200, "text/html", fallback);
    }
}

/* ---- OPTIONS / CORS pre-flight ------------------------------------------- */

static void options_handler(void)
{
    add_cors_headers();
    s_server->send(204);
}

/* ---- Catch-all: routes /api/entries/:id and any unrecognised path --------- */

static void not_found_handler(void)
{
    String uri = s_server->uri();
    HTTPMethod method = s_server->method();

    add_cors_headers();

    if (method == HTTP_OPTIONS) {
        s_server->send(204);
        return;
    }

    /* Delegate /api/entries/* to the entries API */
    if (uri.startsWith("/api/entries/")) {
        api_entries_handle_single(*s_server);
        return;
    }

    s_server->send(404, "application/json", "{\"error\":\"not found\"}");
}

/* ---- FreeRTOS web-server task -------------------------------------------- */

static void web_task(void *arg)
{
    while (s_server) {
        s_server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelete(nullptr);
}

/* ---- Public API ---------------------------------------------------------- */

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    s_server = new WebServer(80);

    /* Root */
    s_server->on("/",        HTTP_GET,     root_handler);
    s_server->on("/",        HTTP_OPTIONS, options_handler);

    /* API modules */
    api_entries_register(*s_server);
    api_prompts_register(*s_server);
    api_export_register(*s_server);

    /* Catch-all for dynamic routes and OPTIONS pre-flight */
    s_server->onNotFound(not_found_handler);

    s_server->begin();

    xTaskCreate(web_task, "web_srv", 8192, nullptr, 3, &s_task);
    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (!s_server) return;
    if (s_task) {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    s_server->stop();
    delete s_server;
    s_server = nullptr;
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool web_server_is_running(void)
{
    return s_server != nullptr;
}
