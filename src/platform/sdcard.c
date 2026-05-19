#include "sdcard.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "sdcard";

#define MOUNT_POINT "/sdcard"
#define PIN_MISO  5
#define PIN_MOSI 10
#define PIN_CLK   8
#define PIN_CS   12

static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_CS;
    slot_cfg.host_id = host.slot;

    /* SPI bus may already be initialized by display; try; ignore if already init */
    spi_bus_config_t buscfg = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = PIN_MISO,
        .sclk_io_num   = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t ret = spi_bus_initialize(host.slot, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
    return ESP_OK;
}

void sdcard_deinit(void)
{
    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_mounted = false;
        s_card    = NULL;
    }
}

bool sdcard_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sdcard_mkdir_p(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    char tmp[256];
    strlcpy(tmp, path, sizeof(tmp));
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return ESP_OK;
}

esp_err_t sdcard_write_file(const char *path, const char *data, size_t len)
{
    if (!path || !data) return ESP_ERR_INVALID_ARG;

    /* ensure parent directory exists */
    char dir[256];
    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) { *slash = '\0'; sdcard_mkdir_p(dir); }

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed: %d", path, errno);
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdcard_read_file(const char *path, char **out_buf, size_t *out_len)
{
    if (!path || !out_buf || !out_len) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(path, "r");
    if (!f) return ESP_ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(buf, 1, sz, f);
    fclose(f);
    buf[sz]  = '\0';
    *out_buf = buf;
    *out_len = (size_t)sz;
    return ESP_OK;
}

esp_err_t sdcard_file_exists(const char *path, bool *exists)
{
    if (!path || !exists) return ESP_ERR_INVALID_ARG;
    struct stat st;
    *exists = (stat(path, &st) == 0);
    return ESP_OK;
}

esp_err_t sdcard_list_dir(const char *path, char ***entries, int *count)
{
    if (!path || !entries || !count) return ESP_ERR_INVALID_ARG;

    DIR *d = opendir(path);
    if (!d) { *count = 0; *entries = NULL; return ESP_ERR_NOT_FOUND; }

    /* count entries */
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] != '.') n++;
    }
    rewinddir(d);

    char **list = calloc(n, sizeof(char *));
    if (!list) { closedir(d); return ESP_ERR_NO_MEM; }

    int i = 0;
    while ((ent = readdir(d)) && i < n) {
        if (ent->d_name[0] == '.') continue;
        list[i++] = strdup(ent->d_name);
    }
    closedir(d);
    *entries = list;
    *count   = i;
    return ESP_OK;
}

esp_err_t sdcard_delete_file(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    return (remove(path) == 0) ? ESP_OK : ESP_FAIL;
}

void sdcard_free_list(char **entries, int count)
{
    if (!entries) return;
    for (int i = 0; i < count; i++) free(entries[i]);
    free(entries);
}
