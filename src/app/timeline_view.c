#include "timeline_view.h"
#include "metadata_index.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "timeline_view";

esp_err_t timeline_init(timeline_state_t *ts)
{
    if (!ts) return ESP_ERR_INVALID_ARG;
    memset(ts, 0, sizeof(*ts));
    return ESP_OK;
}

esp_err_t timeline_load_recent(timeline_state_t *ts, int limit)
{
    if (!ts) return ESP_ERR_INVALID_ARG;
    int max = limit < 64 ? limit : 64;
    esp_err_t ret = index_get_recent(max, ts->entries, &ts->count);
    ts->selected_idx  = 0;
    ts->scroll_offset = 0;
    ESP_LOGI(TAG, "loaded %d recent entries", ts->count);
    return ret;
}

esp_err_t timeline_load_month(timeline_state_t *ts, int year, int month)
{
    if (!ts) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = index_get_by_date(year, month, 0, ts->entries, &ts->count);
    ts->selected_idx  = 0;
    ts->scroll_offset = 0;
    return ret;
}

void timeline_scroll_down(timeline_state_t *ts)
{
    if (!ts || ts->count == 0) return;
    if (ts->selected_idx < ts->count - 1) ts->selected_idx++;
    if (ts->selected_idx - ts->scroll_offset >= 6) ts->scroll_offset++;
}

void timeline_scroll_up(timeline_state_t *ts)
{
    if (!ts || ts->count == 0) return;
    if (ts->selected_idx > 0) ts->selected_idx--;
    if (ts->selected_idx < ts->scroll_offset) ts->scroll_offset--;
}

const entry_meta_t *timeline_get_selected(const timeline_state_t *ts)
{
    if (!ts || ts->count == 0) return NULL;
    if (ts->selected_idx < 0 || ts->selected_idx >= ts->count) return NULL;
    return &ts->entries[ts->selected_idx];
}
