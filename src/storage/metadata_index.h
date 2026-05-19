#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "markdown_entry.h"
#include "esp_err.h"

#define INDEX_MAX_ENTRIES 128

typedef struct {
    char id[32];
    char created[32];
    int  mood;
    int  energy;
    int  anxiety;
    char tags[MAX_TAGS][MAX_TAG_LEN];
    int  tag_count;
    bool favorite;
    char preview[64];
} entry_meta_t;

esp_err_t index_rebuild(void);
esp_err_t index_get_recent(int limit, entry_meta_t *out, int *count);
esp_err_t index_get_by_date(int year, int month, int day, entry_meta_t *out, int *count);
esp_err_t index_get_favorites(int limit, entry_meta_t *out, int *count);
esp_err_t index_get_by_tag(const char *tag, int limit, entry_meta_t *out, int *count);
int       index_total_count(void);

#ifdef __cplusplus
}
#endif
