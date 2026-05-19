/* api_export.cpp — Export/import API using Arduino WebServer.
 *
 * Export: builds a ZIP on SD, reads it via SDCardManager, streams to client.
 * Import: receives a raw markdown entry body and saves it to the import path.
 */
#include "api_export.h"
#include "export_zip.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "markdown_entry.h"
#include "rtc.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "api_export";

static WebServer *s_srv = nullptr;

/* ---- GET /api/export ----------------------------------------------------- */

static void handle_export(void)
{
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    char zip_path[128];
    snprintf(zip_path, sizeof(zip_path), "%s/journal-export-%04d-%02d-%02d.zip",
             EXPORT_ROOT, dt.year, dt.month, dt.day);

    if (export_create_zip(zip_path) != ESP_OK) {
        s_srv->send(500, "application/json", "{\"error\":\"zip failed\"}");
        return;
    }

    /* Read the ZIP into a buffer and send */
    char *buf = nullptr;
    size_t len = 0;
    if (sdcard_read_file(zip_path, &buf, &len) != ESP_OK) {
        s_srv->send(500, "application/json", "{\"error\":\"read failed\"}");
        return;
    }

    char cd[160];
    snprintf(cd, sizeof(cd), "attachment; filename=\"journal-export-%04d-%02d-%02d.zip\"",
             dt.year, dt.month, dt.day);

    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    s_srv->sendHeader("Content-Disposition", cd);
    /* Binary ZIP: stream via setContentLength + sendContent */
    s_srv->setContentLength(len);
    s_srv->send(200, "application/zip", "");
    s_srv->sendContent(reinterpret_cast<const char *>(buf), len);
    free(buf);
}

/* ---- POST /api/import ---------------------------------------------------- */

static void handle_import(void)
{
    String body = s_srv->arg("plain");
    if (body.length() == 0 || body.length() > 65536) {
        s_srv->send(400, "application/json", "{\"error\":\"bad body\"}");
        return;
    }

    journal_entry_t e;
    if (entry_deserialize(body.c_str(), body.length(), &e) == ESP_OK) {
        e.source = ENTRY_SOURCE_IMPORT;
        char import_path[256];
        snprintf(import_path, sizeof(import_path), "%s/%s.md",
                 IMPORT_PATH, e.id[0] ? e.id : "imported");
        sdcard_mkdir_p(IMPORT_PATH);
        sdcard_write_file(import_path, body.c_str(), body.length());
    }

    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    s_srv->send(200, "application/json", "{\"ok\":true}");
}

/* ---- Public registration ------------------------------------------------- */

void api_export_register(WebServer &server)
{
    s_srv = &server;

    server.on("/api/export", HTTP_GET,  handle_export);
    server.on("/api/import", HTTP_POST, handle_import);

    ESP_LOGI(TAG, "api_export registered");
}
