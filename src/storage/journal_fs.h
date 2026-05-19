#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "esp_err.h"
#include <stddef.h>

#define JOURNAL_ROOT  "/sdcard/journal"
#define PROMPTS_ROOT  "/sdcard/prompts"
#define CONFIG_ROOT   "/sdcard/config"
#define EXPORT_ROOT   "/sdcard/export"
#define IMPORT_PATH   "/sdcard/journal/import"
#define WEB_ROOT      "/sdcard/web"

esp_err_t journal_fs_init(void);
esp_err_t journal_fs_entry_path(const char *id, char *buf, size_t len);
esp_err_t journal_fs_list_entries_for_date(int year, int month, int day, char ***ids, int *count);
esp_err_t journal_fs_list_entries_for_month(int year, int month, char ***ids, int *count);
esp_err_t journal_fs_count_entries_this_week(int *count);
esp_err_t journal_fs_get_last_entry_time(char *buf, size_t len);
int       journal_fs_total_days_written(void);

#ifdef __cplusplus
}
#endif
