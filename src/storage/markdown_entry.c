#include "markdown_entry.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "rtc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "markdown_entry";

static const char *SOURCE_NAMES[] = { "", "device", "web", "import" };

esp_err_t entry_init(journal_entry_t *e)
{
    if (!e) return ESP_ERR_INVALID_ARG;
    memset(e, 0, sizeof(*e));

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    snprintf(e->id, sizeof(e->id), "%04d-%02d-%02d-%02d%02d",
             dt.year, dt.month, dt.day, dt.hour, dt.minute);
    rtc_format_iso8601(&dt, e->created, sizeof(e->created));
    e->source = ENTRY_SOURCE_DEVICE;
    return ESP_OK;
}

esp_err_t entry_add_tag(journal_entry_t *e, const char *tag)
{
    if (!e || !tag) return ESP_ERR_INVALID_ARG;
    if (e->tag_count >= MAX_TAGS) return ESP_ERR_NO_MEM;
    strlcpy(e->tags[e->tag_count++], tag, MAX_TAG_LEN);
    return ESP_OK;
}

const char *entry_get_preview(const journal_entry_t *e)
{
    if (!e || e->body[0] == '\0') return "";
    static char preview[64];
    strlcpy(preview, e->body, sizeof(preview));
    return preview;
}

/* ---- serialization ---- */

esp_err_t entry_serialize(const journal_entry_t *e, char **out, size_t *len)
{
    if (!e || !out || !len) return ESP_ERR_INVALID_ARG;

    /* build front matter */
    char fm[1024];
    int pos = 0;
    pos += snprintf(fm + pos, sizeof(fm) - pos, "---\n");
    pos += snprintf(fm + pos, sizeof(fm) - pos, "id: %s\n", e->id);
    pos += snprintf(fm + pos, sizeof(fm) - pos, "created: %s\n", e->created);
    pos += snprintf(fm + pos, sizeof(fm) - pos, "mood: %d\n", e->mood);
    pos += snprintf(fm + pos, sizeof(fm) - pos, "energy: %d\n", e->energy);
    pos += snprintf(fm + pos, sizeof(fm) - pos, "anxiety: %d\n", e->anxiety);
    pos += snprintf(fm + pos, sizeof(fm) - pos, "body_feeling: %s\n", e->body_feeling);
    if (e->tag_count > 0) {
        pos += snprintf(fm + pos, sizeof(fm) - pos, "tags:\n");
        for (int i = 0; i < e->tag_count; i++) {
            pos += snprintf(fm + pos, sizeof(fm) - pos, "  - %s\n", e->tags[i]);
        }
    } else {
        pos += snprintf(fm + pos, sizeof(fm) - pos, "tags: []\n");
    }
    int src = (e->source >= 1 && e->source <= 3) ? e->source : 1;
    pos += snprintf(fm + pos, sizeof(fm) - pos, "source: %s\n", SOURCE_NAMES[src]);
    pos += snprintf(fm + pos, sizeof(fm) - pos, "encrypted: %s\n", e->encrypted ? "true" : "false");
    pos += snprintf(fm + pos, sizeof(fm) - pos, "favorite: %s\n", e->favorite  ? "true" : "false");
    pos += snprintf(fm + pos, sizeof(fm) - pos, "---\n\n");

    size_t total = pos + strlen(e->body) + 1;
    char  *buf   = malloc(total);
    if (!buf) return ESP_ERR_NO_MEM;
    memcpy(buf, fm, pos);
    strcpy(buf + pos, e->body);

    *out = buf;
    *len = total - 1;
    return ESP_OK;
}

/* ---- deserialization — simple line-by-line YAML front matter parser ---- */

static void trim_nl(char *s)
{
    size_t l = strlen(s);
    while (l > 0 && (s[l-1] == '\n' || s[l-1] == '\r' || s[l-1] == ' ')) {
        s[--l] = '\0';
    }
}

esp_err_t entry_deserialize(const char *data, size_t data_len, journal_entry_t *e)
{
    if (!data || !e) return ESP_ERR_INVALID_ARG;
    memset(e, 0, sizeof(*e));

    char *copy = malloc(data_len + 1);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, data, data_len);
    copy[data_len] = '\0';

    char *p   = copy;
    int   fm  = 0; /* 0=before, 1=inside, 2=after */
    bool  in_tags = false;

    while (*p) {
        char *nl = strchr(p, '\n');
        if (!nl) nl = p + strlen(p);
        size_t line_len = nl - p;
        char line[512];
        size_t copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        memcpy(line, p, copy_len);
        line[copy_len] = '\0';
        trim_nl(line);

        if (fm == 0) {
            if (strcmp(line, "---") == 0) fm = 1;
        } else if (fm == 1) {
            if (strcmp(line, "---") == 0) {
                fm = 2;
                /* skip blank line after --- */
                if (*nl) { p = nl + 1; if (*p == '\n') p++; break; }
            } else if (strncmp(line, "  - ", 4) == 0 && in_tags) {
                if (e->tag_count < MAX_TAGS) {
                    strlcpy(e->tags[e->tag_count++], line + 4, MAX_TAG_LEN);
                }
            } else {
                in_tags = false;
                char *colon = strchr(line, ':');
                if (colon) {
                    *colon = '\0';
                    const char *key = line;
                    const char *val = colon + 1;
                    while (*val == ' ') val++;
                    if      (strcmp(key, "id")           == 0) strlcpy(e->id,           val, sizeof(e->id));
                    else if (strcmp(key, "created")      == 0) strlcpy(e->created,       val, sizeof(e->created));
                    else if (strcmp(key, "mood")         == 0) e->mood     = atoi(val);
                    else if (strcmp(key, "energy")       == 0) e->energy   = atoi(val);
                    else if (strcmp(key, "anxiety")      == 0) e->anxiety  = atoi(val);
                    else if (strcmp(key, "body_feeling") == 0) strlcpy(e->body_feeling,  val, sizeof(e->body_feeling));
                    else if (strcmp(key, "encrypted")    == 0) e->encrypted = (strcmp(val, "true") == 0);
                    else if (strcmp(key, "favorite")     == 0) e->favorite  = (strcmp(val, "true") == 0);
                    else if (strcmp(key, "source")       == 0) {
                        if      (strcmp(val, "web")    == 0) e->source = ENTRY_SOURCE_WEB;
                        else if (strcmp(val, "import") == 0) e->source = ENTRY_SOURCE_IMPORT;
                        else                                  e->source = ENTRY_SOURCE_DEVICE;
                    }
                    else if (strcmp(key, "tags") == 0) {
                        in_tags = (strcmp(val, "") == 0 || val[0] == '\0');
                    }
                }
            }
        }
        p = (*nl) ? nl + 1 : nl;
    }

    /* rest is body */
    if (fm == 2) {
        strlcpy(e->body, p, sizeof(e->body));
        /* strip trailing newline */
        size_t bl = strlen(e->body);
        while (bl > 0 && (e->body[bl-1] == '\n' || e->body[bl-1] == '\r')) {
            e->body[--bl] = '\0';
        }
    }

    free(copy);
    return ESP_OK;
}

esp_err_t entry_save(const journal_entry_t *e)
{
    char path[256];
    ESP_ERROR_CHECK(journal_fs_entry_path(e->id, path, sizeof(path)));

    /* ensure directory */
    char dir[256];
    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; sdcard_mkdir_p(dir); }

    char *buf = NULL;
    size_t len = 0;
    esp_err_t ret = entry_serialize(e, &buf, &len);
    if (ret != ESP_OK) return ret;
    ret = sdcard_write_file(path, buf, len);
    free(buf);
    ESP_LOGI(TAG, "saved entry %s", e->id);
    return ret;
}

esp_err_t entry_load(const char *id, journal_entry_t *e)
{
    char path[256];
    ESP_ERROR_CHECK(journal_fs_entry_path(id, path, sizeof(path)));

    char *buf = NULL;
    size_t len = 0;
    esp_err_t ret = sdcard_read_file(path, &buf, &len);
    if (ret != ESP_OK) return ret;
    ret = entry_deserialize(buf, len, e);
    free(buf);
    return ret;
}
