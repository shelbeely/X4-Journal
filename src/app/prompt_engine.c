#include "prompt_engine.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "rtc.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "prompt_engine";

static prompt_pack_t s_packs[MAX_PACKS];
static int           s_pack_count = 0;

static esp_err_t parse_pack_json(const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) { ESP_LOGE(TAG, "JSON parse error"); return ESP_FAIL; }

    cJSON *packs_arr = cJSON_GetObjectItem(root, "packs");
    if (!packs_arr) { cJSON_Delete(root); return ESP_FAIL; }

    cJSON *pack;
    cJSON_ArrayForEach(pack, packs_arr) {
        if (s_pack_count >= MAX_PACKS) break;
        prompt_pack_t *p = &s_packs[s_pack_count];
        memset(p, 0, sizeof(*p));

        cJSON *id   = cJSON_GetObjectItem(pack, "id");
        cJSON *name = cJSON_GetObjectItem(pack, "name");
        cJSON *arr  = cJSON_GetObjectItem(pack, "prompts");

        if (id   && cJSON_IsString(id))   strlcpy(p->id,   id->valuestring,   sizeof(p->id));
        if (name && cJSON_IsString(name)) strlcpy(p->name, name->valuestring, sizeof(p->name));

        if (arr && cJSON_IsArray(arr)) {
            cJSON *item;
            cJSON_ArrayForEach(item, arr) {
                if (p->count >= MAX_PROMPTS_PER_PACK) break;
                if (cJSON_IsString(item)) {
                    strlcpy(p->prompts[p->count++], item->valuestring,
                            sizeof(p->prompts[0]));
                }
            }
        }
        s_pack_count++;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t prompt_engine_init(void)
{
    s_pack_count = 0;
    char path[128];
    snprintf(path, sizeof(path), "%s/default.json", PROMPTS_ROOT);

    char *buf = NULL;
    size_t len = 0;
    esp_err_t ret = sdcard_read_file(path, &buf, &len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "default.json not found; no prompts loaded");
        return ESP_OK;
    }
    ret = parse_pack_json(buf, len);
    free(buf);
    ESP_LOGI(TAG, "prompt_engine_init: %d packs", s_pack_count);
    return ret;
}

esp_err_t prompt_load_pack(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    char *buf = NULL;
    size_t len = 0;
    ESP_ERROR_CHECK(sdcard_read_file(path, &buf, &len));
    esp_err_t ret = parse_pack_json(buf, len);
    free(buf);
    return ret;
}

const char *prompt_get_daily(void)
{
    if (s_pack_count == 0) return "How are you arriving today?";

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    /* day_of_year approximation */
    static const int DAYS_IN_MONTH[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    int doy = dt.day;
    for (int m = 1; m < dt.month; m++) doy += DAYS_IN_MONTH[m];

    /* distribute across all packs */
    int total = 0;
    for (int i = 0; i < s_pack_count; i++) total += s_packs[i].count;
    if (total == 0) return "How are you arriving today?";

    int idx = doy % total;
    for (int i = 0; i < s_pack_count; i++) {
        if (idx < s_packs[i].count) return s_packs[i].prompts[idx];
        idx -= s_packs[i].count;
    }
    return "How are you arriving today?";
}

const char *prompt_get_random(const char *pack_id)
{
    if (!pack_id) return prompt_get_daily();
    for (int i = 0; i < s_pack_count; i++) {
        if (strcmp(s_packs[i].id, pack_id) == 0) {
            if (s_packs[i].count == 0) return "";
            uint32_t r;
            esp_fill_random(&r, sizeof(r));
            return s_packs[i].prompts[r % s_packs[i].count];
        }
    }
    return "";
}

int prompt_get_pack_count(void) { return s_pack_count; }

const prompt_pack_t *prompt_get_pack(int idx)
{
    if (idx < 0 || idx >= s_pack_count) return NULL;
    return &s_packs[idx];
}
