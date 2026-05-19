#include "metadata_index.h"
#include "journal_fs.h"
#include "markdown_entry.h"
#include "rtc.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "metadata_index";

static entry_meta_t s_index[INDEX_MAX_ENTRIES];
static int          s_count = 0;

static void meta_from_entry(const journal_entry_t *e, entry_meta_t *m)
{
    strlcpy(m->id,      e->id,      sizeof(m->id));
    strlcpy(m->created, e->created, sizeof(m->created));
    m->mood      = e->mood;
    m->energy    = e->energy;
    m->anxiety   = e->anxiety;
    m->tag_count = e->tag_count;
    m->favorite  = e->favorite;
    for (int i = 0; i < e->tag_count && i < MAX_TAGS; i++) {
        strlcpy(m->tags[i], e->tags[i], MAX_TAG_LEN);
    }
    strlcpy(m->preview, entry_get_preview(e), sizeof(m->preview));
}

esp_err_t index_rebuild(void)
{
    s_count = 0;
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    /* scan last 12 months */
    for (int m = 0; m < 12 && s_count < INDEX_MAX_ENTRIES; m++) {
        int year = dt.year, month = dt.month - m;
        while (month < 1) { month += 12; year--; }
        char **ids = NULL;
        int n = 0;
        journal_fs_list_entries_for_month(year, month, &ids, &n);
        for (int i = 0; i < n && s_count < INDEX_MAX_ENTRIES; i++) {
            journal_entry_t e;
            if (entry_load(ids[i], &e) == ESP_OK) {
                meta_from_entry(&e, &s_index[s_count++]);
            }
        }
        if (ids) {
            for (int i = 0; i < n; i++) free(ids[i]);
            free(ids);
        }
    }

    /* sort descending by created (lexicographic works for ISO 8601) */
    for (int i = 0; i < s_count - 1; i++) {
        for (int j = i + 1; j < s_count; j++) {
            if (strcmp(s_index[i].created, s_index[j].created) < 0) {
                entry_meta_t tmp = s_index[i];
                s_index[i]       = s_index[j];
                s_index[j]       = tmp;
            }
        }
    }

    ESP_LOGI(TAG, "index rebuilt: %d entries", s_count);
    return ESP_OK;
}

esp_err_t index_get_recent(int limit, entry_meta_t *out, int *count)
{
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    int n = s_count < limit ? s_count : limit;
    memcpy(out, s_index, n * sizeof(entry_meta_t));
    *count = n;
    return ESP_OK;
}

esp_err_t index_get_by_date(int year, int month, int day,
                             entry_meta_t *out, int *count)
{
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    char prefix[11];
    snprintf(prefix, sizeof(prefix), "%04d-%02d-%02d", year, month, day);
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_index[i].id, prefix, 10) == 0) {
            out[n++] = s_index[i];
        }
    }
    *count = n;
    return ESP_OK;
}

esp_err_t index_get_favorites(int limit, entry_meta_t *out, int *count)
{
    if (!out || !count) return ESP_ERR_INVALID_ARG;
    int n = 0;
    for (int i = 0; i < s_count && n < limit; i++) {
        if (s_index[i].favorite) out[n++] = s_index[i];
    }
    *count = n;
    return ESP_OK;
}

esp_err_t index_get_by_tag(const char *tag, int limit, entry_meta_t *out, int *count)
{
    if (!tag || !out || !count) return ESP_ERR_INVALID_ARG;
    int n = 0;
    for (int i = 0; i < s_count && n < limit; i++) {
        for (int t = 0; t < s_index[i].tag_count; t++) {
            if (strcmp(s_index[i].tags[t], tag) == 0) { out[n++] = s_index[i]; break; }
        }
    }
    *count = n;
    return ESP_OK;
}

int index_total_count(void)
{
    return s_count;
}
