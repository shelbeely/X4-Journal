#include "api_export.h"
#include "export_zip.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "markdown_entry.h"
#include "rtc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "api_export";

static esp_err_t handle_export(httpd_req_t *req)
{
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    char zip_path[128];
    snprintf(zip_path, sizeof(zip_path), "%s/journal-export-%04d-%02d-%02d.zip",
             EXPORT_ROOT, dt.year, dt.month, dt.day);

    if (export_create_zip(zip_path) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* stream the ZIP file */
    FILE *f = fopen(zip_path, "rb");
    if (!f) { httpd_resp_send_500(req); return ESP_FAIL; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"journal-export-%04d-%02d-%02d.zip\"",
             dt.year, dt.month, dt.day);
    httpd_resp_set_type(req, "application/zip");
    httpd_resp_set_hdr(req, "Content-Disposition", cd);

    char buf[1024];
    size_t remaining = (size_t)size;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t read  = fread(buf, 1, chunk, f);
        if (read == 0) break;
        httpd_resp_send_chunk(req, buf, read);
        remaining -= read;
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0); /* end chunked */
    return ESP_OK;
}

static esp_err_t handle_import(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 65536) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }
    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
    body[received] = '\0';

    /* try to deserialize as a journal entry and save to import dir */
    journal_entry_t e;
    if (entry_deserialize(body, received, &e) == ESP_OK) {
        e.source = ENTRY_SOURCE_IMPORT;
        char import_path[256];
        snprintf(import_path, sizeof(import_path), "%s/%s.md", IMPORT_PATH, e.id[0] ? e.id : "imported");
        sdcard_mkdir_p(IMPORT_PATH);
        sdcard_write_file(import_path, body, received);
    }
    free(body);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

void api_export_register(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        { .uri = "/api/export", .method = HTTP_GET,  .handler = handle_export },
        { .uri = "/api/import", .method = HTTP_POST, .handler = handle_import },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "api_export registered");
}
