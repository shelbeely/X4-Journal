/* api_dev.cpp — Development / diagnostics API routes.
 *
 * Compiled only when CONFIG_X4_DIAG_HTTP_API is defined.
 * All endpoints require the Authorization header if CONFIG_X4_DIAG_API_TOKEN
 * is non-empty.  No arbitrary command execution is exposed.
 *
 * Routes:
 *   GET  /api/dev/status
 *   GET  /api/dev/health
 *   GET  /api/dev/logs
 *   GET  /api/dev/ota
 *   GET  /api/dev/display
 *   GET  /api/dev/display/screenshot.bmp
 *   POST /api/dev/display/test-pattern
 *   POST /api/dev/ota/check
 *   POST /api/dev/ota/apply
 *   POST /api/dev/reboot
 *   POST /api/dev/rollback
 */
#ifdef CONFIG_X4_DIAG_HTTP_API

#include "api_dev.h"
#include "display.h"
#include "health_check.h"
#include "ota_manager.h"
#include "log_buffer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <WebServer.h>
#include <string.h>
#include <stdlib.h>

#ifdef CONFIG_X4_DEV_DIAGNOSTICS
#include "diag_status.h"
#endif

static const char *TAG   = "api_dev";
static WebServer  *s_srv = nullptr;

/* ---- Auth token (empty = no auth required) -------------------------------- */
#ifndef CONFIG_X4_DIAG_API_TOKEN
#define CONFIG_X4_DIAG_API_TOKEN ""
#endif

static bool check_auth(void)
{
    const char *token = CONFIG_X4_DIAG_API_TOKEN;
    if (!token || !token[0]) return true; /* no auth configured */
    if (!s_srv->hasHeader("Authorization")) return false;
    String auth = s_srv->header("Authorization");
    /* Accept "Bearer <token>" or just "<token>" */
    String expected = String("Bearer ") + token;
    return (auth == expected || auth == token);
}

/* ---- Helpers -------------------------------------------------------------- */

static void send_json(int status, cJSON *root)
{
    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    if (!str) { s_srv->send(500, "application/json", "{\"error\":\"OOM\"}"); return; }
    s_srv->send(status, "application/json", str);
    free(str);
}

static void send_401(void)
{
    s_srv->sendHeader("WWW-Authenticate", "Bearer realm=\"X4 Dev API\"");
    s_srv->send(401, "application/json", "{\"error\":\"unauthorized\"}");
}

#define AUTH_GUARD() do { if (!check_auth()) { send_401(); return; } } while(0)

/* ---- GET /api/dev/status -------------------------------------------------- */

static void handle_dev_status(void)
{
    AUTH_GUARD();
#ifdef CONFIG_X4_DEV_DIAGNOSTICS
    static char json_buf[8192];
    esp_err_t err = diag_get_status(json_buf, sizeof(json_buf));
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    if (err == ESP_OK) {
        s_srv->send(200, "application/json", json_buf);
    } else {
        s_srv->send(500, "application/json",
                    "{\"error\":\"diagnostics_buffer_overflow\"}");
    }
#else
    s_srv->send(200, "application/json",
                "{\"note\":\"CONFIG_X4_DEV_DIAGNOSTICS not enabled\"}");
#endif
}

/* ---- GET /api/dev/health -------------------------------------------------- */

static void handle_dev_health(void)
{
    AUTH_GUARD();
    health_status_t h = {};
    health_check_run(&h, false);
    char *json = health_status_to_json(&h);
    if (!json) { s_srv->send(500, "application/json", "{\"error\":\"OOM\"}"); return; }
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    s_srv->send(200, "application/json", json);
    free(json);
}

/* ---- GET /api/dev/logs ---------------------------------------------------- */

static void handle_dev_logs(void)
{
    AUTH_GUARD();
    if (!log_buffer_is_init()) {
        s_srv->sendHeader("Access-Control-Allow-Origin", "*");
        s_srv->send(501, "application/json",
                    "{\"error\":\"log_buffer_not_available\"}");
        return;
    }
    const char *lines[LOG_BUFFER_LINES];
    size_t count = LOG_BUFFER_LINES;
    log_buffer_get_lines(lines, &count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "lines");
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(lines[i]));
    }
    cJSON_AddBoolToObject(root, "truncated", false);
    send_json(200, root);
}

/* ---- GET /api/dev/ota ----------------------------------------------------- */

static void handle_dev_ota(void)
{
    AUTH_GUARD();
    const esp_partition_t *part = esp_ota_get_running_partition();
    char manifest_url[128] = {};
    ota_get_manifest_url_redacted(manifest_url, sizeof(manifest_url));

    const ota_check_result_t *last = ota_last_check_result();
    const char *last_status = "none";
    if (last) {
        switch (last->status) {
        case OTA_CHECK_UPDATE_AVAILABLE: last_status = "update_available"; break;
        case OTA_CHECK_UP_TO_DATE:       last_status = "up_to_date";       break;
        case OTA_CHECK_REJECTED:         last_status = "rejected";          break;
        case OTA_CHECK_ERROR:            last_status = "error";             break;
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "slot",             part ? part->label : "unknown");
    cJSON_AddBoolToObject  (root, "pending_verify",   ota_is_pending_verify());
    cJSON_AddStringToObject(root, "channel",          ota_get_channel());
    cJSON_AddStringToObject(root, "manifest_url",     manifest_url);
    cJSON_AddStringToObject(root, "last_check_result", last_status);
    cJSON_AddStringToObject(root, "credentials",      "[REDACTED]");
    send_json(200, root);
}

/* ---- GET /api/dev/display ------------------------------------------------- */

static void handle_dev_display(void)
{
    AUTH_GUARD();
    display_status_t ds = {};
    display_get_status(&ds);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject  (root, "init_ok",              ds.init_ok);
    cJSON_AddStringToObject(root, "driver",               ds.driver);
    cJSON_AddNumberToObject(root, "width",                ds.width);
    cJSON_AddNumberToObject(root, "height",               ds.height);
    cJSON_AddStringToObject(root, "last_refresh_type",    ds.last_refresh_type);
    cJSON_AddNumberToObject(root, "last_refresh_duration_ms", ds.last_refresh_duration_ms);
    cJSON_AddNumberToObject(root, "framebuffer_hash",     (double)ds.framebuffer_hash);
    cJSON_AddStringToObject(root, "last_error",           ds.last_error);
    send_json(200, root);
}

/* ---- GET /api/dev/display/screenshot.bmp ---------------------------------- */

static void handle_dev_screenshot(void)
{
    AUTH_GUARD();
    uint8_t *bmp = nullptr;
    size_t   bmp_len = 0;
    esp_err_t err = display_screenshot_bmp(&bmp, &bmp_len);
    if (err != ESP_OK || !bmp) {
        s_srv->sendHeader("Access-Control-Allow-Origin", "*");
        s_srv->send(501, "application/json",
                    "{\"error\":\"screenshot_not_available\"}");
        return;
    }
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    s_srv->sendHeader("Content-Disposition",
                      "attachment; filename=\"screenshot.bmp\"");
    s_srv->send_P(200, "image/bmp",
                  reinterpret_cast<const char *>(bmp), (int)bmp_len);
    free(bmp);
}

/* ---- POST /api/dev/display/test-pattern ----------------------------------- */

static void handle_dev_test_pattern(void)
{
    AUTH_GUARD();
    String body = s_srv->arg("plain");
    cJSON *req  = cJSON_Parse(body.c_str());
    cJSON *jpat = req ? cJSON_GetObjectItem(req, "pattern") : nullptr;
    if (!jpat || !cJSON_IsString(jpat)) {
        if (req) cJSON_Delete(req);
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "error", "pattern_required");
        send_json(400, err_obj);
        return;
    }
    char pattern[32] = {};
    strncpy(pattern, jpat->valuestring, sizeof(pattern)-1);
    cJSON_Delete(req);

    esp_err_t err = display_render_test_pattern(pattern);
    display_status_t after = {};
    display_get_status(&after);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",              err == ESP_OK ? "ok" : "error");
    cJSON_AddStringToObject(root, "pattern",             pattern);
    cJSON_AddNumberToObject(root, "refresh_duration_ms", after.last_refresh_duration_ms);
    if (err != ESP_OK) cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
    send_json(err == ESP_OK ? 200 : 500, root);
}

/* ---- POST /api/dev/ota/check ---------------------------------------------- */

static void handle_dev_ota_check(void)
{
    AUTH_GUARD();
    ota_check_result_t res = {};
    ota_check(&res);

    cJSON *root = cJSON_CreateObject();
    const char *status_str = "error";
    switch (res.status) {
    case OTA_CHECK_UPDATE_AVAILABLE: status_str = "update_available"; break;
    case OTA_CHECK_UP_TO_DATE:       status_str = "up_to_date";       break;
    case OTA_CHECK_REJECTED:         status_str = "rejected";          break;
    default:                         break;
    }
    cJSON_AddStringToObject(root, "result",          status_str);
    cJSON_AddStringToObject(root, "version",         res.new_version);
    cJSON_AddStringToObject(root, "current_version", res.current_version);
    if (res.reject_reason[0]) {
        cJSON_AddStringToObject(root, "reason", res.reject_reason);
    }
    send_json(200, root);
}

/* ---- OTA apply task ------------------------------------------------------- */

static void dev_ota_apply_task(void *arg)
{
    (void)arg;
    ota_apply();
    vTaskDelete(nullptr);
}

/* ---- POST /api/dev/ota/apply ---------------------------------------------- */

static void handle_dev_ota_apply(void)
{
    AUTH_GUARD();
    String body = s_srv->arg("plain");
    cJSON *req  = cJSON_Parse(body.c_str());
    cJSON *jconf = req ? cJSON_GetObjectItem(req, "confirm") : nullptr;
    if (!jconf || !cJSON_IsTrue(jconf)) {
        if (req) cJSON_Delete(req);
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "result", "rejected");
        cJSON_AddStringToObject(err_obj, "reason", "confirm_required");
        send_json(400, err_obj);
        return;
    }
    if (req) cJSON_Delete(req);

    const ota_check_result_t *last = ota_last_check_result();
    if (!last || last->status != OTA_CHECK_UPDATE_AVAILABLE) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "result", "rejected");
        cJSON_AddStringToObject(err_obj, "reason", "no_validated_manifest");
        send_json(409, err_obj);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",  "applying");
    cJSON_AddStringToObject(root, "version", last->new_version);
    cJSON_AddStringToObject(root, "message",
                            "Downloading firmware. Device will reboot when complete.");
    send_json(200, root);
    xTaskCreate(dev_ota_apply_task, "dev_ota_apply", 8192, nullptr, 5, nullptr);
}

/* ---- POST /api/dev/reboot ------------------------------------------------- */

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    vTaskDelete(nullptr);
}

static void handle_dev_reboot(void)
{
    AUTH_GUARD();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", "rebooting");
    send_json(200, root);
    xTaskCreate(reboot_task, "dev_reboot", 2048, nullptr, 5, nullptr);
}

/* ---- POST /api/dev/rollback ----------------------------------------------- */

static void handle_dev_rollback(void)
{
    AUTH_GUARD();
    if (!ota_is_pending_verify()) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(err_obj, "result", "rejected");
        cJSON_AddStringToObject(err_obj, "reason",
                                "rollback_not_supported_or_not_pending");
        send_json(409, err_obj);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",  "rolling_back");
    cJSON_AddStringToObject(root, "message", "Device will reboot into previous slot.");
    send_json(200, root);
    vTaskDelay(pdMS_TO_TICKS(300));
    ota_rollback("api_request");
}

/* ---- Registration --------------------------------------------------------- */

void api_dev_register(WebServer *server)
{
    s_srv = server;
    /* Collect the Authorization header */
    const char *headerNames[] = { "Authorization" };
    server->collectHeaders(headerNames, 1);

    server->on("/api/dev/status",                  HTTP_GET,  handle_dev_status);
    server->on("/api/dev/health",                  HTTP_GET,  handle_dev_health);
    server->on("/api/dev/logs",                    HTTP_GET,  handle_dev_logs);
    server->on("/api/dev/ota",                     HTTP_GET,  handle_dev_ota);
    server->on("/api/dev/display",                 HTTP_GET,  handle_dev_display);
    server->on("/api/dev/display/screenshot.bmp",  HTTP_GET,  handle_dev_screenshot);
    server->on("/api/dev/display/test-pattern",    HTTP_POST, handle_dev_test_pattern);
    server->on("/api/dev/ota/check",               HTTP_POST, handle_dev_ota_check);
    server->on("/api/dev/ota/apply",               HTTP_POST, handle_dev_ota_apply);
    server->on("/api/dev/reboot",                  HTTP_POST, handle_dev_reboot);
    server->on("/api/dev/rollback",                HTTP_POST, handle_dev_rollback);
    ESP_LOGI(TAG, "Dev API routes registered (CONFIG_X4_DIAG_HTTP_API)");
}

#endif /* CONFIG_X4_DIAG_HTTP_API */
