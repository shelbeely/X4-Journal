#include "api_prompts.h"
#include "prompt_engine.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "api_prompts";

static esp_err_t handle_get_prompts(httpd_req_t *req)
{
    cJSON *root  = cJSON_CreateObject();
    cJSON *packs = cJSON_AddArrayToObject(root, "packs");

    int count = prompt_get_pack_count();
    for (int i = 0; i < count; i++) {
        const prompt_pack_t *p = prompt_get_pack(i);
        if (!p) continue;
        cJSON *pack = cJSON_CreateObject();
        cJSON_AddStringToObject(pack, "id",   p->id);
        cJSON_AddStringToObject(pack, "name", p->name);
        cJSON_AddNumberToObject(pack, "count", p->count);
        cJSON *arr = cJSON_AddArrayToObject(pack, "prompts");
        for (int j = 0; j < p->count; j++) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(p->prompts[j]));
        }
        cJSON_AddItemToArray(packs, pack);
    }
    cJSON_AddStringToObject(root, "daily", prompt_get_daily());

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) { httpd_resp_send_500(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    free(str);
    return ESP_OK;
}

static esp_err_t handle_get_daily(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "prompt", prompt_get_daily());
    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, str);
    free(str);
    return ESP_OK;
}

static esp_err_t handle_upload_pack(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 32768) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }
    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
    body[received] = '\0';

    /* save to /sdcard/prompts/custom-<timestamp>.json */
    char path[128];
    snprintf(path, sizeof(path), "%s/custom-%lu.json", PROMPTS_ROOT, (unsigned long)0);
    sdcard_write_file(path, body, received);
    free(body);

    esp_err_t ret = prompt_load_pack(path);
    if (ret != ESP_OK) { httpd_resp_send_500(req); return ESP_FAIL; }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t prompts_handler(httpd_req_t *req)
{
    if (strstr(req->uri, "/daily")) return handle_get_daily(req);
    if (strstr(req->uri, "/upload")) return handle_upload_pack(req);
    return handle_get_prompts(req);
}

void api_prompts_register(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        { .uri = "/api/prompts",         .method = HTTP_GET,  .handler = prompts_handler },
        { .uri = "/api/prompts/daily",   .method = HTTP_GET,  .handler = prompts_handler },
        { .uri = "/api/prompts/upload",  .method = HTTP_POST, .handler = prompts_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "api_prompts registered");
}
