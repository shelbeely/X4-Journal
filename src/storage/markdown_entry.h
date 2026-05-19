#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_TAGS    8
#define MAX_TAG_LEN 32

typedef enum {
    ENTRY_SOURCE_DEVICE = 1,
    ENTRY_SOURCE_WEB    = 2,
    ENTRY_SOURCE_IMPORT = 3,
} entry_source_t;

typedef struct {
    char   id[32];
    char   created[32];
    int    mood;
    int    energy;
    int    anxiety;
    char   body_feeling[16];
    char   tags[MAX_TAGS][MAX_TAG_LEN];
    int    tag_count;
    entry_source_t source;
    bool   encrypted;
    bool   favorite;
    char   body[4096];
} journal_entry_t;

esp_err_t    entry_init(journal_entry_t *e);
esp_err_t    entry_serialize(const journal_entry_t *e, char **out, size_t *len);
esp_err_t    entry_deserialize(const char *data, size_t len, journal_entry_t *e);
esp_err_t    entry_save(const journal_entry_t *e);
esp_err_t    entry_load(const char *id, journal_entry_t *e);
esp_err_t    entry_add_tag(journal_entry_t *e, const char *tag);
const char  *entry_get_preview(const journal_entry_t *e);
