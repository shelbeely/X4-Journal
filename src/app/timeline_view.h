#pragma once
#include "metadata_index.h"
#include "esp_err.h"

typedef struct {
    entry_meta_t entries[32];
    int          count;
    int          selected_idx;
    int          scroll_offset;
} timeline_state_t;

esp_err_t           timeline_init(timeline_state_t *ts);
esp_err_t           timeline_load_recent(timeline_state_t *ts, int limit);
esp_err_t           timeline_load_month(timeline_state_t *ts, int year, int month);
void                timeline_scroll_down(timeline_state_t *ts);
void                timeline_scroll_up(timeline_state_t *ts);
const entry_meta_t *timeline_get_selected(const timeline_state_t *ts);
