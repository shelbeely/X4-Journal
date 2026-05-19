/* api_system.cpp — Standard system/OTA API routes.
 *
 * Routes registered:
 *   GET  /api/version
 *   GET  /api/health
 *   GET  /api/logs
 *   POST /api/ota/check
 *   POST /api/ota/apply
 *   POST /api/ota/rollback
 */
#include "api_system.h"
#include "health_check.h"
#include "ota_manager.h"
#include "log_buffer.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "cJSON.h"
#include <WebServer.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG     = "api_system";
static WebServer  *s_srv   = nullptr;

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

/* ---- GET /api/version ----------------------------------------------------- */

static void handle_version(void)
{
    const esp_app_desc_t *app  = esp_app_get_description();
    const esp_partition_t *part = esp_ota_get_running_partition();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version",
                            app  ? app->version : "unknown");
    cJSON_AddStringToObject(root, "slot",
                            part ? part->label  : "unknown");
    cJSON_AddBoolToObject  (root, "pending_verify", ota_is_pending_verify());
    send_json(200, root);
}

/* ---- GET /api/health ------------------------------------------------------- */

static void handle_health(void)
{
    health_status_t h = {};
    health_check_run(&h, false);
    char *json = health_status_to_json(&h);
    if (!json) { s_srv->send(500, "application/json", "{\"error\":\"OOM\"}"); return; }
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    s_srv->send(200, "application/json", json);
    free(json);
}

/* ---- GET /api/logs -------------------------------------------------------- */

static void handle_logs(void)
{
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

/* ---- POST /api/ota/check -------------------------------------------------- */

static void handle_ota_check(void)
{
    ota_check_result_t res = {};
    esp_err_t err = ota_check(&res);

    cJSON *root = cJSON_CreateObject();
    switch (res.status) {
    case OTA_CHECK_UPDATE_AVAILABLE:
        cJSON_AddStringToObject(root, "result",          "update_available");
        cJSON_AddStringToObject(root, "version",         res.new_version);
        cJSON_AddStringToObject(root, "current_version", res.current_version);
        break;
    case OTA_CHECK_UP_TO_DATE:
        cJSON_AddStringToObject(root, "result",  "up_to_date");
        cJSON_AddStringToObject(root, "version", res.new_version);
        break;
    case OTA_CHECK_REJECTED:
        cJSON_AddStringToObject(root, "result", "rejected");
        cJSON_AddStringToObject(root, "reason", res.reject_reason);
        break;
    default:
        cJSON_AddStringToObject(root, "result", "error");
        cJSON_AddStringToObject(root, "reason", esp_err_to_name(err));
        break;
    }
    send_json(200, root);
}

/* ---- OTA apply task ------------------------------------------------------- */

static void ota_apply_task(void *arg)
{
    (void)arg;
    ota_apply(); /* calls esp_restart() on success; logs error on failure */
    vTaskDelete(nullptr);
}

/* ---- POST /api/ota/apply -------------------------------------------------- */

static void handle_ota_apply(void)
{
    /* Require {"confirm":true} in body */
    String body = s_srv->arg("plain");
    cJSON *req  = cJSON_Parse(body.c_str());
    cJSON *jconf = req ? cJSON_GetObjectItem(req, "confirm") : nullptr;
    if (!jconf || !cJSON_IsTrue(jconf)) {
        if (req) cJSON_Delete(req);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "result", "rejected");
        cJSON_AddStringToObject(err, "reason", "confirm_required");
        send_json(400, err);
        return;
    }
    if (req) cJSON_Delete(req);

    const ota_check_result_t *last = ota_last_check_result();
    if (!last || last->status != OTA_CHECK_UPDATE_AVAILABLE) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "result", "rejected");
        cJSON_AddStringToObject(err, "reason", "no_validated_manifest");
        send_json(409, err);
        return;
    }

    /* Respond immediately, then start OTA task */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",  "applying");
    cJSON_AddStringToObject(root, "version", last->new_version);
    cJSON_AddStringToObject(root, "message",
                            "Downloading firmware. Device will reboot when complete.");
    send_json(200, root);

    xTaskCreate(ota_apply_task, "ota_apply", 8192, nullptr, 5, nullptr);
}

/* ---- POST /api/ota/rollback ----------------------------------------------- */

static void handle_ota_rollback(void)
{
    String body = s_srv->arg("plain");
    cJSON *req  = cJSON_Parse(body.c_str());
    cJSON *jconf = req ? cJSON_GetObjectItem(req, "confirm") : nullptr;
    if (!jconf || !cJSON_IsTrue(jconf)) {
        if (req) cJSON_Delete(req);
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "result", "rejected");
        cJSON_AddStringToObject(err, "reason", "confirm_required");
        send_json(400, err);
        return;
    }
    if (req) cJSON_Delete(req);

    if (!ota_is_pending_verify()) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "result", "rejected");
        cJSON_AddStringToObject(err, "reason",
                                "rollback_not_supported_or_not_pending");
        send_json(409, err);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result",  "rolling_back");
    cJSON_AddStringToObject(root, "message", "Device will reboot into previous slot.");
    send_json(200, root);

    /* Small delay so the response is sent before restart */
    vTaskDelay(pdMS_TO_TICKS(300));
    ota_rollback("api_request");
}

/* ---- Registration --------------------------------------------------------- */

void api_system_register(WebServer *server)
{
    s_srv = server;
    server->on("/api/version",      HTTP_GET,  handle_version);
    server->on("/api/health",       HTTP_GET,  handle_health);
    server->on("/api/logs",         HTTP_GET,  handle_logs);
    server->on("/api/ota/check",    HTTP_POST, handle_ota_check);
    server->on("/api/ota/apply",    HTTP_POST, handle_ota_apply);
    server->on("/api/ota/rollback", HTTP_POST, handle_ota_rollback);
    ESP_LOGI(TAG, "System API routes registered");
}
