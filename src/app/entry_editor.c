#include "entry_editor.h"
#include "markdown_entry.h"
#include "metadata_index.h"
#include "rtc.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "entry_editor";

/* ---- Canned fragments ---- */
const char *const FEELING_FRAGMENTS[] = {
    "soft", "scattered", "okay", "heavy", "hopeful",
    "overstimulated", "proud", "lonely", "safe", "weird but surviving",
};
const int FEELING_FRAGMENT_COUNT = 10;

const char *const NEED_FRAGMENTS[] = {
    "quiet", "rest", "movement", "connection", "warmth",
    "space", "grounding", "water", "a snack", "someone to listen",
};
const int NEED_FRAGMENT_COUNT = 10;

const char *const WIN_FRAGMENTS[] = {
    "I got through the morning",
    "I ate something",
    "I showed up",
    "I made a choice",
    "I kept going",
    "I reached out",
    "I noticed something beautiful",
    "I was honest",
};
const int WIN_FRAGMENT_COUNT = 8;

/* ---- Internal state ---- */
static journal_entry_t s_entry;
static editor_mode_t   s_mode;
static bool            s_active = false;

/* fragments selected during check-in */
static char s_feeling_frag[64];
static char s_need_frag[64];
static char s_win_frag[128];

esp_err_t entry_editor_init(void) { return ESP_OK; }

esp_err_t entry_editor_start(editor_mode_t mode)
{
    s_mode = mode;
    s_active = true;
    s_feeling_frag[0] = '\0';
    s_need_frag[0]    = '\0';
    s_win_frag[0]     = '\0';
    return entry_init(&s_entry);
}

esp_err_t entry_editor_set_mood(int mood)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    s_entry.mood = mood;
    return ESP_OK;
}

esp_err_t entry_editor_set_energy(int energy)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    s_entry.energy = energy;
    return ESP_OK;
}

esp_err_t entry_editor_set_anxiety(int anxiety)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    s_entry.anxiety = anxiety;
    return ESP_OK;
}

esp_err_t entry_editor_set_body_feeling(const char *feeling)
{
    if (!s_active || !feeling) return ESP_ERR_INVALID_ARG;
    strlcpy(s_entry.body_feeling, feeling, sizeof(s_entry.body_feeling));
    return ESP_OK;
}

esp_err_t entry_editor_add_fragment(const char *fragment)
{
    if (!s_active || !fragment) return ESP_ERR_INVALID_ARG;
    /* cycle through feeling → need → win slots */
    if (s_feeling_frag[0] == '\0') {
        strlcpy(s_feeling_frag, fragment, sizeof(s_feeling_frag));
    } else if (s_need_frag[0] == '\0') {
        strlcpy(s_need_frag, fragment, sizeof(s_need_frag));
    } else {
        strlcpy(s_win_frag, fragment, sizeof(s_win_frag));
    }
    return ESP_OK;
}

esp_err_t entry_editor_set_body(const char *text)
{
    if (!s_active || !text) return ESP_ERR_INVALID_ARG;
    strlcpy(s_entry.body, text, sizeof(s_entry.body));
    return ESP_OK;
}

esp_err_t entry_editor_add_tag(const char *tag)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    return entry_add_tag(&s_entry, tag);
}

esp_err_t entry_editor_save(char *saved_id_out, size_t len)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;

    /* compose body from fragments for check-in mode */
    if (s_mode == EDITOR_MODE_CHECKIN && s_entry.body[0] == '\0') {
        char composed[512];
        int pos = 0;
        if (s_feeling_frag[0]) {
            pos += snprintf(composed + pos, sizeof(composed) - pos,
                            "Today I feel: %s.", s_feeling_frag);
        }
        if (s_need_frag[0]) {
            pos += snprintf(composed + pos, sizeof(composed) - pos,
                            " One thing I need: %s.", s_need_frag);
        }
        if (s_win_frag[0]) {
            pos += snprintf(composed + pos, sizeof(composed) - pos,
                            " Tiny win: %s.", s_win_frag);
        }
        strlcpy(s_entry.body, composed, sizeof(s_entry.body));
    }

    s_entry.source = ENTRY_SOURCE_DEVICE;
    esp_err_t ret  = entry_save(&s_entry);
    if (ret == ESP_OK) {
        if (saved_id_out) strlcpy(saved_id_out, s_entry.id, len);
        /* update in-memory index */
        index_rebuild();
        s_active = false;
        ESP_LOGI(TAG, "entry saved: %s", s_entry.id);
    }
    return ret;
}

esp_err_t entry_editor_discard(void)
{
    s_active = false;
    memset(&s_entry, 0, sizeof(s_entry));
    return ESP_OK;
}

const journal_entry_t *entry_editor_get_current(void)
{
    return s_active ? &s_entry : NULL;
}
