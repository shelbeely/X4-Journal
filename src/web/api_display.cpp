/* api_display.cpp — Display subsystem API routes.
 *
 * Routes registered:
 *   GET  /api/display/status
 *   GET  /api/display/screenshot.bmp
 *   GET  /api/display/logs
 *   POST /api/display/test-pattern
 *   POST /api/display/refresh/full
 *   POST /api/display/refresh/partial
 *   POST /api/display/clear
 */
#include "api_display.h"
#include "display.h"
#include "log_buffer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <WebServer.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG   = "api_display";
static WebServer  *s_srv = nullptr;

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

/* Serialise display_status_t to cJSON object */
static cJSON *display_status_to_json(const display_status_t *ds)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "driver",                  ds->driver);
    cJSON_AddNumberToObject(obj, "width",                   ds->width);
    cJSON_AddNumberToObject(obj, "height",                  ds->height);
    cJSON_AddNumberToObject(obj, "rotation",                ds->rotation);
    cJSON_AddNumberToObject(obj, "framebuffer_size",        (double)ds->framebuffer_size);
    cJSON_AddNumberToObject(obj, "framebuffer_hash",        (double)ds->framebuffer_hash);
    cJSON_AddStringToObject(obj, "last_refresh_type",       ds->last_refresh_type);
    cJSON_AddNumberToObject(obj, "last_refresh_duration_ms",ds->last_refresh_duration_ms);
    cJSON_AddNumberToObject(obj, "busy_pin_wait_ms",        ds->busy_pin_wait_ms);
    cJSON_AddStringToObject(obj, "last_error",              ds->last_error);
    cJSON_AddBoolToObject  (obj, "init_ok",                 ds->init_ok);
    cJSON_AddStringToObject(obj, "test_pattern_last",       ds->test_pattern_last);
    cJSON_AddStringToObject(obj, "test_pattern_result",     ds->test_pattern_result);
    cJSON_AddNumberToObject(obj, "heap_free",               (double)esp_get_free_heap_size());
    return obj;
}

/* ---- GET /api/display/status --------------------------------------------- */

static void handle_status(void)
{
    display_status_t ds = {};
    display_get_status(&ds);
    send_json(200, display_status_to_json(&ds));
}

/* ---- GET /api/display/screenshot.bmp ------------------------------------- */

static void handle_screenshot(void)
{
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

/* ---- GET /api/display/logs ----------------------------------------------- */

static void handle_logs(void)
{
    if (!log_buffer_is_init()) {
        s_srv->sendHeader("Access-Control-Allow-Origin", "*");
        s_srv->send(501, "application/json",
                    "{\"error\":\"log_buffer_not_available\"}");
        return;
    }

    const char *all_lines[LOG_BUFFER_LINES];
    size_t all_count = LOG_BUFFER_LINES;
    log_buffer_get_lines(all_lines, &all_count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "lines");
    for (size_t i = 0; i < all_count; i++) {
        if (strstr(all_lines[i], "DISPLAY_")) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(all_lines[i]));
        }
    }
    send_json(200, root);
}

/* ---- POST /api/display/test-pattern -------------------------------------- */

static void handle_test_pattern(void)
{
    String body = s_srv->arg("plain");
    cJSON *req  = cJSON_Parse(body.c_str());
    cJSON *jpat = req ? cJSON_GetObjectItem(req, "pattern") : nullptr;

    if (!jpat || !cJSON_IsString(jpat)) {
        if (req) cJSON_Delete(req);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "pattern_required");
        send_json(400, err);
        return;
    }

    char pattern[32] = {};
    strncpy(pattern, jpat->valuestring, sizeof(pattern)-1);
    cJSON_Delete(req);

    display_status_t before = {};
    display_get_status(&before);
    esp_err_t err = display_render_test_pattern(pattern);
    display_status_t after = {};
    display_get_status(&after);

    cJSON *root = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(root, "result",              "ok");
        cJSON_AddStringToObject(root, "pattern",             pattern);
        cJSON_AddNumberToObject(root, "refresh_duration_ms", after.last_refresh_duration_ms);
    } else {
        cJSON_AddStringToObject(root, "result",  "error");
        cJSON_AddStringToObject(root, "pattern", pattern);
        cJSON_AddStringToObject(root, "error",   esp_err_to_name(err));
    }
    send_json(err == ESP_OK ? 200 : 500, root);
}

/* ---- POST /api/display/refresh/full -------------------------------------- */

static void handle_refresh_full(void)
{
    display_status_t before = {};
    display_get_status(&before);
    display_full_refresh();
    display_status_t after = {};
    display_get_status(&after);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",     "ok");
    cJSON_AddNumberToObject(root, "duration_ms", after.last_refresh_duration_ms);
    send_json(200, root);
}

/* ---- POST /api/display/refresh/partial ----------------------------------- */

static void handle_refresh_partial(void)
{
    String body = s_srv->arg("plain");
    cJSON *req  = cJSON_Parse(body.c_str());

    int x = 0, y = 0, w = 64, h = 32;
    if (req) {
        cJSON *jx = cJSON_GetObjectItem(req, "x");
        cJSON *jy = cJSON_GetObjectItem(req, "y");
        cJSON *jw = cJSON_GetObjectItem(req, "w");
        cJSON *jh = cJSON_GetObjectItem(req, "h");
        if (cJSON_IsNumber(jx)) x = (int)jx->valuedouble;
        if (cJSON_IsNumber(jy)) y = (int)jy->valuedouble;
        if (cJSON_IsNumber(jw)) w = (int)jw->valuedouble;
        if (cJSON_IsNumber(jh)) h = (int)jh->valuedouble;
        cJSON_Delete(req);
    }

    display_partial_refresh(x, y, w, h);
    display_status_t after = {};
    display_get_status(&after);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",     "ok");
    cJSON_AddNumberToObject(root, "duration_ms", after.last_refresh_duration_ms);
    send_json(200, root);
}

/* ---- POST /api/display/clear --------------------------------------------- */

static void handle_clear(void)
{
    display_render_test_pattern("all_white");
    display_status_t after = {};
    display_get_status(&after);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",     "ok");
    cJSON_AddNumberToObject(root, "duration_ms", after.last_refresh_duration_ms);
    send_json(200, root);
}

/* ---- Registration --------------------------------------------------------- */

void api_display_register(WebServer *server)
{
    s_srv = server;
    server->on("/api/display/status",           HTTP_GET,  handle_status);
    server->on("/api/display/screenshot.bmp",   HTTP_GET,  handle_screenshot);
    server->on("/api/display/logs",             HTTP_GET,  handle_logs);
    server->on("/api/display/test-pattern",     HTTP_POST, handle_test_pattern);
    server->on("/api/display/refresh/full",     HTTP_POST, handle_refresh_full);
    server->on("/api/display/refresh/partial",  HTTP_POST, handle_refresh_partial);
    server->on("/api/display/clear",            HTTP_POST, handle_clear);
    ESP_LOGI(TAG, "Display API routes registered");
}
