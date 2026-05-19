#include "journal_fs.h"
#include "sdcard.h"
#include "rtc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "journal_fs";

esp_err_t journal_fs_init(void)
{
    esp_err_t ret;
    const char *dirs[] = {
        JOURNAL_ROOT, PROMPTS_ROOT, CONFIG_ROOT, EXPORT_ROOT, IMPORT_PATH, WEB_ROOT,
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        ret = sdcard_mkdir_p(dirs[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "mkdir_p(%s) failed", dirs[i]);
            return ret;
        }
    }
    ESP_LOGI(TAG, "journal_fs_init ok");
    return ESP_OK;
}

/* id format: "YYYY-MM-DD-HHMM"  →  path: JOURNAL_ROOT/YYYY/MM/YYYY-MM-DD_HH-MM.md */
esp_err_t journal_fs_entry_path(const char *id, char *buf, size_t len)
{
    if (!id || !buf) return ESP_ERR_INVALID_ARG;
    int year, month, day, hour, minute;
    if (sscanf(id, "%4d-%2d-%2d-%2d%2d", &year, &month, &day, &hour, &minute) != 5) {
        ESP_LOGE(TAG, "bad id format: %s", id);
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(buf, len, "%s/%04d/%02d/%04d-%02d-%02d_%02d-%02d.md",
             JOURNAL_ROOT, year, month, year, month, day, hour, minute);
    return ESP_OK;
}

esp_err_t journal_fs_list_entries_for_date(int year, int month, int day,
                                            char ***ids, int *count)
{
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/%04d/%02d", JOURNAL_ROOT, year, month);

    char **files = NULL;
    int n = 0;
    esp_err_t ret = sdcard_list_dir(dir, &files, &n);
    if (ret != ESP_OK) { *ids = NULL; *count = 0; return ESP_OK; }

    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%04d-%02d-%02d", year, month, day);

    char **result = calloc(n, sizeof(char *));
    int   ri = 0;
    for (int i = 0; i < n; i++) {
        if (strncmp(files[i], prefix, 10) == 0) {
            /* convert filename to id: "YYYY-MM-DD_HH-MM.md" → "YYYY-MM-DD-HHMM" */
            char id[32];
            int y, mo, d, h, mi;
            if (sscanf(files[i], "%4d-%2d-%2d_%2d-%2d.md", &y, &mo, &d, &h, &mi) == 5) {
                snprintf(id, sizeof(id), "%04d-%02d-%02d-%02d%02d", y, mo, d, h, mi);
                result[ri++] = strdup(id);
            }
        }
    }
    sdcard_free_list(files, n);
    *ids   = result;
    *count = ri;
    return ESP_OK;
}

esp_err_t journal_fs_list_entries_for_month(int year, int month,
                                             char ***ids, int *count)
{
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/%04d/%02d", JOURNAL_ROOT, year, month);

    char **files = NULL;
    int n = 0;
    esp_err_t ret = sdcard_list_dir(dir, &files, &n);
    if (ret != ESP_OK) { *ids = NULL; *count = 0; return ESP_OK; }

    char **result = calloc(n + 1, sizeof(char *));
    int   ri = 0;
    for (int i = 0; i < n; i++) {
        int y, mo, d, h, mi;
        if (sscanf(files[i], "%4d-%2d-%2d_%2d-%2d.md", &y, &mo, &d, &h, &mi) == 5) {
            char id[32];
            snprintf(id, sizeof(id), "%04d-%02d-%02d-%02d%02d", y, mo, d, h, mi);
            result[ri++] = strdup(id);
        }
    }
    sdcard_free_list(files, n);
    *ids   = result;
    *count = ri;
    return ESP_OK;
}

esp_err_t journal_fs_count_entries_this_week(int *count)
{
    if (!count) return ESP_ERR_INVALID_ARG;
    *count = 0;

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    /* weekday 0=Sunday; compute offset to Monday */
    int offset_to_mon = (dt.weekday == 0) ? 6 : dt.weekday - 1;

    /* iterate Mon through today */
    for (int d = 0; d <= offset_to_mon; d++) {
        int check_day = dt.day - (offset_to_mon - d);
        if (check_day < 1) continue;
        char **ids = NULL;
        int n = 0;
        journal_fs_list_entries_for_date(dt.year, dt.month, check_day, &ids, &n);
        *count += n;
        if (ids) {
            for (int i = 0; i < n; i++) free(ids[i]);
            free(ids);
        }
    }
    return ESP_OK;
}

esp_err_t journal_fs_get_last_entry_time(char *buf, size_t len)
{
    if (!buf) return ESP_ERR_INVALID_ARG;
    strlcpy(buf, "Never", len);

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    /* scan current + previous month for latest entry */
    for (int m = 0; m <= 1; m++) {
        int year = dt.year, month = dt.month - m;
        if (month < 1) { month = 12; year--; }
        char **ids = NULL;
        int n = 0;
        journal_fs_list_entries_for_month(year, month, &ids, &n);
        if (n > 0) {
            /* ids are sorted by filename, so last is most recent */
            const char *last = ids[n - 1];
            int y, mo, d, h, mi;
            if (sscanf(last, "%4d-%2d-%2d-%2d%2d", &y, &mo, &d, &h, &mi) == 5) {
                int h12 = h % 12; if (h12 == 0) h12 = 12;
                if (y == dt.year && mo == dt.month && d == dt.day) {
                    snprintf(buf, len, "Today %d:%02d %s", h12, mi, h < 12 ? "AM" : "PM");
                } else if (y == dt.year && mo == dt.month && d == dt.day - 1) {
                    snprintf(buf, len, "Yesterday %d:%02d %s", h12, mi, h < 12 ? "AM" : "PM");
                } else {
                    snprintf(buf, len, "%02d/%02d %d:%02d %s", mo, d, h12, mi, h < 12 ? "AM" : "PM");
                }
            }
            for (int i = 0; i < n; i++) free(ids[i]);
            free(ids);
            return ESP_OK;
        }
        if (ids) free(ids);
    }
    return ESP_OK;
}

int journal_fs_total_days_written(void)
{
    int total = 0;
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    /* scan last 12 months */
    for (int m = 0; m < 12; m++) {
        int year = dt.year, month = dt.month - m;
        while (month < 1) { month += 12; year--; }
        char dir[128];
        snprintf(dir, sizeof(dir), "%s/%04d/%02d", JOURNAL_ROOT, year, month);
        char **files = NULL;
        int n = 0;
        if (sdcard_list_dir(dir, &files, &n) == ESP_OK) {
            /* count unique days */
            char seen[32][11];
            int nseen = 0;
            for (int i = 0; i < n && nseen < 32; i++) {
                char day_prefix[11];
                strncpy(day_prefix, files[i], 10);
                day_prefix[10] = '\0';
                bool found = false;
                for (int j = 0; j < nseen; j++) {
                    if (strcmp(seen[j], day_prefix) == 0) { found = true; break; }
                }
                if (!found) { strlcpy(seen[nseen++], day_prefix, 11); total++; }
            }
            sdcard_free_list(files, n);
        }
    }
    return total;
}
