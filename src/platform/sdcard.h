#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t sdcard_init(void);
void      sdcard_deinit(void);
bool      sdcard_is_mounted(void);
esp_err_t sdcard_mkdir_p(const char *path);
esp_err_t sdcard_write_file(const char *path, const char *data, size_t len);
esp_err_t sdcard_read_file(const char *path, char **out_buf, size_t *out_len);
esp_err_t sdcard_file_exists(const char *path, bool *exists);
esp_err_t sdcard_list_dir(const char *path, char ***entries, int *count);
esp_err_t sdcard_delete_file(const char *path);
void      sdcard_free_list(char **entries, int count);
