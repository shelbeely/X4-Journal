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

#define JSON_CONTENT "application/json"

/* ---- helpers ---- */

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
    cJSON_AddStringToObject(obj, "id",           e->id);
    cJSON_AddStringToObject(obj, "created",       e->created);
    cJSON_AddNumberToObject(obj, "mood",          e->mood);
    cJSON_AddNumberToObject(obj, "energy",        e->energy);
    cJSON_AddNumberToObject(obj, "anxiety",       e->anxiety);
    cJSON_AddStringToObject(obj, "body_feeling",  e->body_feeling);
    cJSON_AddBoolToObject  (obj, "encrypted",     e->encrypted);
    cJSON_AddBoolToObject  (obj, "favorite",      e->favorite);
    cJSON_AddStringToObject(obj, "body",          e->body);
    cJSON *tags = cJSON_AddArrayToObject(obj, "tags");
    for (int i = 0; i < e->tag_count; i++)
        cJSON_AddItemToArray(tags, cJSON_CreateString(e->tags[i]));
    return obj;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root, int status)
{
    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, JSON_CONTENT);
    httpd_resp_set_status(req, status == 201 ? "201 Created" :
                               status == 404 ? "404 Not Found" : "200 OK");
    httpd_resp_sendstr(req, str);
    free(str);
    return ESP_OK;
}

/* ---- GET /api/entries ---- */
static esp_err_t handle_get_entries(httpd_req_t *req)
{
    entry_meta_t entries[64];
    int count = 0;

    /* optional ?date=YYYY-MM-DD */
    char query[64];
    char date_param[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "date", date_param, sizeof(date_param));
    }

    if (date_param[0]) {
        int y, m, d;
        if (sscanf(date_param, "%4d-%2d-%2d", &y, &m, &d) == 3) {
            index_get_by_date(y, m, d, entries, &count);
        }
    } else {
        index_get_recent(64, entries, &count);
    }

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, meta_to_json(&entries[i]));
    }
    return send_json(req, arr, 200);
}

/* ---- GET /api/entries/:id ---- */
static esp_err_t handle_get_entry(httpd_req_t *req)
{
    /* extract id from URI: /api/entries/YYYY-MM-DD-HHMM */
    const char *id_start = strrchr(req->uri, '/');
    if (!id_start || strlen(id_start) < 2) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    const char *id = id_start + 1;

    journal_entry_t e;
    if (entry_load(id, &e) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    return send_json(req, entry_to_json(&e), 200);
}

/* ---- POST /api/entries ---- */
static esp_err_t handle_post_entry(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large or empty");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }

    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    journal_entry_t e;
    entry_init(&e);
    e.source = ENTRY_SOURCE_WEB;

    cJSON *jbody = cJSON_GetObjectItem(root, "body");
    if (jbody && cJSON_IsString(jbody)) strlcpy(e.body, jbody->valuestring, sizeof(e.body));
    cJSON *jmood = cJSON_GetObjectItem(root, "mood");
    if (jmood) e.mood = jmood->valueint;
    cJSON *jenergy = cJSON_GetObjectItem(root, "energy");
    if (jenergy) e.energy = jenergy->valueint;
    cJSON *janx = cJSON_GetObjectItem(root, "anxiety");
    if (janx) e.anxiety = janx->valueint;
    cJSON *jfeel = cJSON_GetObjectItem(root, "body_feeling");
    if (jfeel && cJSON_IsString(jfeel)) strlcpy(e.body_feeling, jfeel->valuestring, sizeof(e.body_feeling));
    cJSON *jtags = cJSON_GetObjectItem(root, "tags");
    if (jtags && cJSON_IsArray(jtags)) {
        cJSON *t;
        cJSON_ArrayForEach(t, jtags) {
            if (cJSON_IsString(t)) entry_add_tag(&e, t->valuestring);
        }
    }
    cJSON_Delete(root);

    if (entry_save(&e) != ESP_OK) { httpd_resp_send_500(req); return ESP_FAIL; }
    index_rebuild();

    return send_json(req, entry_to_json(&e), 201);
}

/* ---- PUT /api/entries/:id ---- */
static esp_err_t handle_put_entry(httpd_req_t *req)
{
    const char *id_start = strrchr(req->uri, '/');
    if (!id_start) { httpd_resp_send_404(req); return ESP_FAIL; }
    const char *id = id_start + 1;

    journal_entry_t e;
    if (entry_load(id, &e) != ESP_OK) { httpd_resp_send_404(req); return ESP_FAIL; }

    int total = req->content_len;
    if (total <= 0 || total > 8192) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body"); return ESP_FAIL; }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }
    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON error"); return ESP_FAIL; }

    cJSON *jbody = cJSON_GetObjectItem(root, "body");
    if (jbody && cJSON_IsString(jbody)) strlcpy(e.body, jbody->valuestring, sizeof(e.body));
    cJSON *jfav = cJSON_GetObjectItem(root, "favorite");
    if (jfav) e.favorite = cJSON_IsTrue(jfav);
    cJSON_Delete(root);

    entry_save(&e);
    index_rebuild();
    return send_json(req, entry_to_json(&e), 200);
}

/* ---- DELETE /api/entries/:id ---- */
static esp_err_t handle_delete_entry(httpd_req_t *req)
{
    const char *id_start = strrchr(req->uri, '/');
    if (!id_start) { httpd_resp_send_404(req); return ESP_FAIL; }
    const char *id = id_start + 1;

    char path[256];
    if (journal_fs_entry_path(id, path, sizeof(path)) != ESP_OK ||
        sdcard_delete_file(path) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    index_rebuild();
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- URI dispatch ---- */
static esp_err_t entries_handler(httpd_req_t *req)
{
    if (strcmp(req->method_str, "GET") == 0) {
        /* distinguish list vs single */
        const char *last_slash = strrchr(req->uri, '/');
        if (last_slash && strcmp(last_slash, "/entries") != 0 &&
            strcmp(last_slash, "/entries/") != 0) {
            return handle_get_entry(req);
        }
        return handle_get_entries(req);
    }
    if (strcmp(req->method_str, "POST") == 0)   return handle_post_entry(req);
    if (strcmp(req->method_str, "PUT") == 0)    return handle_put_entry(req);
    if (strcmp(req->method_str, "DELETE") == 0) return handle_delete_entry(req);
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
    return ESP_FAIL;
}

void api_entries_register(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        { .uri = "/api/entries",    .method = HTTP_GET,    .handler = entries_handler },
        { .uri = "/api/entries",    .method = HTTP_POST,   .handler = entries_handler },
        { .uri = "/api/entries/*",  .method = HTTP_GET,    .handler = entries_handler },
        { .uri = "/api/entries/*",  .method = HTTP_PUT,    .handler = entries_handler },
        { .uri = "/api/entries/*",  .method = HTTP_DELETE, .handler = entries_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "api_entries registered");
}
