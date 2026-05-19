/* api_prompts.cpp — REST API for journal prompts using Arduino WebServer */
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

static WebServer *s_srv = nullptr;

static void add_cors(void)
{
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
}

/* ---- GET /api/prompts ---------------------------------------------------- */

static void handle_get_prompts(void)
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
        for (int j = 0; j < p->count; j++)
            cJSON_AddItemToArray(arr, cJSON_CreateString(p->prompts[j]));
        cJSON_AddItemToArray(packs, pack);
    }
    cJSON_AddStringToObject(root, "daily", prompt_get_daily());

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    add_cors();
    if (!str) { s_srv->send(500, "application/json", "{\"error\":\"OOM\"}"); return; }
    s_srv->send(200, "application/json", str);
    free(str);
}

/* ---- GET /api/prompts/daily ---------------------------------------------- */

static void handle_get_daily(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "prompt", prompt_get_daily());
    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    add_cors();
    if (!str) { s_srv->send(500, "application/json", "{\"error\":\"OOM\"}"); return; }
    s_srv->send(200, "application/json", str);
    free(str);
}

/* ---- POST /api/prompts/upload -------------------------------------------- */

static void handle_upload_pack(void)
{
    String body = s_srv->arg("plain");
    if (body.length() == 0 || body.length() > 32768) {
        s_srv->send(400, "application/json", "{\"error\":\"bad body\"}");
        return;
    }

    /* Save to /sdcard/prompts/custom-<millis>.json */
    char path[128];
    snprintf(path, sizeof(path), "%s/custom-%lu.json",
             PROMPTS_ROOT, (unsigned long)millis());

    if (sdcard_write_file(path, body.c_str(), body.length()) != ESP_OK) {
        s_srv->send(500, "application/json", "{\"error\":\"write failed\"}");
        return;
    }

    if (prompt_load_pack(path) != ESP_OK) {
        s_srv->send(500, "application/json", "{\"error\":\"parse failed\"}");
        return;
    }

    add_cors();
    s_srv->send(200, "application/json", "{\"ok\":true}");
}

/* ---- Public registration ------------------------------------------------- */

void api_prompts_register(WebServer &server)
{
    s_srv = &server;

    server.on("/api/prompts",        HTTP_GET,  handle_get_prompts);
    server.on("/api/prompts/daily",  HTTP_GET,  handle_get_daily);
    server.on("/api/prompts/upload", HTTP_POST, handle_upload_pack);

    ESP_LOGI(TAG, "api_prompts registered");
}
