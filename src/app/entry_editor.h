#pragma once
#include "markdown_entry.h"
#include "esp_err.h"

typedef enum {
    EDITOR_MODE_CHECKIN,
    EDITOR_MODE_FREEWRITE,
    EDITOR_MODE_PROMPTED,
} editor_mode_t;

/* Canned fragment arrays */
extern const char *const FEELING_FRAGMENTS[];
extern const int          FEELING_FRAGMENT_COUNT;
extern const char *const NEED_FRAGMENTS[];
extern const int          NEED_FRAGMENT_COUNT;
extern const char *const WIN_FRAGMENTS[];
extern const int          WIN_FRAGMENT_COUNT;

esp_err_t             entry_editor_init(void);
esp_err_t             entry_editor_start(editor_mode_t mode);
esp_err_t             entry_editor_set_mood(int mood);
esp_err_t             entry_editor_set_energy(int energy);
esp_err_t             entry_editor_set_anxiety(int anxiety);
esp_err_t             entry_editor_set_body_feeling(const char *feeling);
esp_err_t             entry_editor_add_fragment(const char *fragment);
esp_err_t             entry_editor_set_body(const char *text);
esp_err_t             entry_editor_add_tag(const char *tag);
esp_err_t             entry_editor_save(char *saved_id_out, size_t len);
esp_err_t             entry_editor_discard(void);
const journal_entry_t *entry_editor_get_current(void);
