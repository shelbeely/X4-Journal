/* api_entries.cpp — REST API for journal entries using Arduino WebServer */
#include "api_entries.h"
#include "markdown_entry.h"
#include "metadata_index.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "api_entries";

static WebServer *s_srv = nullptr;

/* ---- JSON helpers -------------------------------------------------------- */

static cJSON *meta_to_json(const entry_meta_t *m)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id",      m->id);
    cJSON_AddStringToObject(obj, "created", m->created);
    cJSON_AddNumberToObject(obj, "mood",    m->mood);
    cJSON_AddNumberToObject(obj, "energy",  m->energy);
    cJSON_AddNumberToObject(obj, "anxiety", m->anxiety);
    cJSON_AddBoolToObject  (obj, "favorite",m->favorite);
    cJSON_AddStringToObject(obj, "preview", m->preview);
    cJSON *tags = cJSON_AddArrayToObject(obj, "tags");
    for (int i = 0; i < m->tag_count; i++)
        cJSON_AddItemToArray(tags, cJSON_CreateString(m->tags[i]));
    return obj;
}

static cJSON *entry_to_json(const journal_entry_t *e)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id",          e->id);
    cJSON_AddStringToObject(obj, "created",      e->created);
    cJSON_AddNumberToObject(obj, "mood",         e->mood);
    cJSON_AddNumberToObject(obj, "energy",       e->energy);
    cJSON_AddNumberToObject(obj, "anxiety",      e->anxiety);
    cJSON_AddStringToObject(obj, "body_feeling", e->body_feeling);
    cJSON_AddBoolToObject  (obj, "encrypted",    e->encrypted);
    cJSON_AddBoolToObject  (obj, "favorite",     e->favorite);
    cJSON_AddStringToObject(obj, "body",         e->body);
    cJSON *tags = cJSON_AddArrayToObject(obj, "tags");
    for (int i = 0; i < e->tag_count; i++)
        cJSON_AddItemToArray(tags, cJSON_CreateString(e->tags[i]));
    return obj;
}

static void send_json(WebServer &srv, cJSON *root, int status)
{
    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    srv.sendHeader("Access-Control-Allow-Origin", "*");
    if (!str) { srv.send(500, "application/json", "{\"error\":\"OOM\"}"); return; }
    srv.send(status, "application/json", str);
    free(str);
}

/* ---- GET /api/entries ---------------------------------------------------- */

static void handle_get_entries(void)
{
    entry_meta_t entries[64];
    int count = 0;

    String date_param = s_srv->arg("date");
    if (date_param.length() > 0) {
        int y, m, d;
        if (sscanf(date_param.c_str(), "%4d-%2d-%2d", &y, &m, &d) == 3) {
            index_get_by_date(y, m, d, entries, &count);
        }
    } else {
        index_get_recent(64, entries, &count);
    }

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) cJSON_AddItemToArray(arr, meta_to_json(&entries[i]));
    send_json(*s_srv, arr, 200);
}

/* ---- GET /api/entries/:id ------------------------------------------------ */

static void handle_get_entry(const char *id)
{
    journal_entry_t e;
    if (entry_load(id, &e) != ESP_OK) {
        s_srv->send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    send_json(*s_srv, entry_to_json(&e), 200);
}

/* ---- POST /api/entries --------------------------------------------------- */

static void handle_post_entry(void)
{
    String body = s_srv->arg("plain");
    if (body.length() == 0 || body.length() > 8192) {
        s_srv->send(400, "application/json", "{\"error\":\"bad body\"}");
        return;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) { s_srv->send(400, "application/json", "{\"error\":\"invalid JSON\"}"); return; }

    journal_entry_t e;
    entry_init(&e);
    e.source = ENTRY_SOURCE_WEB;

    cJSON *jbody   = cJSON_GetObjectItem(root, "body");
    cJSON *jmood   = cJSON_GetObjectItem(root, "mood");
    cJSON *jenergy = cJSON_GetObjectItem(root, "energy");
    cJSON *janx    = cJSON_GetObjectItem(root, "anxiety");
    cJSON *jfeel   = cJSON_GetObjectItem(root, "body_feeling");
    cJSON *jtags   = cJSON_GetObjectItem(root, "tags");

    if (jbody   && cJSON_IsString(jbody))   strlcpy(e.body,         jbody->valuestring,   sizeof(e.body));
    if (jmood)                               e.mood   = jmood->valueint;
    if (jenergy)                             e.energy = jenergy->valueint;
    if (janx)                                e.anxiety= janx->valueint;
    if (jfeel   && cJSON_IsString(jfeel))   strlcpy(e.body_feeling, jfeel->valuestring,   sizeof(e.body_feeling));
    if (jtags && cJSON_IsArray(jtags)) {
        cJSON *t;
        cJSON_ArrayForEach(t, jtags) {
            if (cJSON_IsString(t)) entry_add_tag(&e, t->valuestring);
        }
    }
    cJSON_Delete(root);

    if (entry_save(&e) != ESP_OK) { s_srv->send(500, "application/json", "{\"error\":\"save failed\"}"); return; }
    index_rebuild();
    send_json(*s_srv, entry_to_json(&e), 201);
}

/* ---- PUT /api/entries/:id ------------------------------------------------ */

static void handle_put_entry(const char *id)
{
    journal_entry_t e;
    if (entry_load(id, &e) != ESP_OK) {
        s_srv->send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }

    String body = s_srv->arg("plain");
    if (body.length() == 0 || body.length() > 8192) {
        s_srv->send(400, "application/json", "{\"error\":\"bad body\"}");
        return;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) { s_srv->send(400, "application/json", "{\"error\":\"JSON error\"}"); return; }

    cJSON *jbody = cJSON_GetObjectItem(root, "body");
    cJSON *jfav  = cJSON_GetObjectItem(root, "favorite");
    if (jbody && cJSON_IsString(jbody)) strlcpy(e.body, jbody->valuestring, sizeof(e.body));
    if (jfav) e.favorite = cJSON_IsTrue(jfav);
    cJSON_Delete(root);

    entry_save(&e);
    index_rebuild();
    send_json(*s_srv, entry_to_json(&e), 200);
}

/* ---- DELETE /api/entries/:id --------------------------------------------- */

static void handle_delete_entry(const char *id)
{
    char path[256];
    if (journal_fs_entry_path(id, path, sizeof(path)) != ESP_OK ||
        sdcard_delete_file(path) != ESP_OK) {
        s_srv->send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    index_rebuild();
    s_srv->sendHeader("Access-Control-Allow-Origin", "*");
    s_srv->send(200, "application/json", "{\"ok\":true}");
}

/* ---- Public registration ------------------------------------------------- */

void api_entries_register(WebServer &server)
{
    s_srv = &server;

    server.on("/api/entries", HTTP_GET,  handle_get_entries);
    server.on("/api/entries", HTTP_POST, handle_post_entry);

    ESP_LOGI(TAG, "api_entries registered");
}

/* Called by web_server.cpp for /api/entries/:id routes */
void api_entries_handle_single(WebServer &server)
{
    /* Extract id: URI is /api/entries/<id> */
    String uri = server.uri();
    String id  = uri.substring(13);  /* len("/api/entries/") = 13 */
    if (id.length() == 0) { server.send(400, "application/json", "{\"error\":\"missing id\"}"); return; }

    switch (server.method()) {
    case HTTP_GET:    handle_get_entry(id.c_str());    break;
    case HTTP_PUT:    handle_put_entry(id.c_str());    break;
    case HTTP_DELETE: handle_delete_entry(id.c_str()); break;
    default:
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(405, "application/json", "{\"error\":\"method not allowed\"}");
        break;
    }
}
