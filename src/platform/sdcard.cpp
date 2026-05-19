/* sdcard.cpp — SD card platform driver backed by community-sdk SDCardManager.
 *
 * Paths in the rest of the code use "/sdcard/..." VFS-style prefixes.
 * SDCardManager uses SD-relative paths without the "/sdcard" prefix.
 * sd_path() strips the prefix before every call into the SDK.
 */
#include "sdcard.h"
#include "hardware_pins.h"
#include <SDCardManager.h>
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "sdcard";

/* Strip the /sdcard prefix so paths work with SdFat's root-relative API.
 * Always returns a non-empty string; falls back to "/" for bare /sdcard. */
static const char *sd_path(const char *path)
{
    if (path && strncmp(path, "/sdcard", 7) == 0) {
        const char *p = path + 7;
        return (p[0] == '\0') ? "/" : p;
    }
    return path;
}

esp_err_t sdcard_init(void)
{
    if (!SdMan.begin()) {
        ESP_LOGE(TAG, "SD card init failed (not detected or SPI error)");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SD card ready");
    return ESP_OK;
}

void sdcard_deinit(void)
{
    /* SDCardManager has no explicit teardown */
}

bool sdcard_is_mounted(void)
{
    return SdMan.ready();
}

esp_err_t sdcard_mkdir_p(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    /* SdFat sd.mkdir(path, true) creates all intermediate components */
    return SdMan.mkdir(sd_path(path), true) ? ESP_OK : ESP_FAIL;
}

esp_err_t sdcard_write_file(const char *path, const char *data, size_t len)
{
    if (!path || !data) return ESP_ERR_INVALID_ARG;

    const char *sdp = sd_path(path);

    /* Ensure parent directory */
    char dir[256];
    strlcpy(dir, sdp, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        SdMan.mkdir(dir, true);
    }

    FsFile f;
    if (!SdMan.openFileForWrite("sdcard", sdp, f)) {
        ESP_LOGE(TAG, "open for write failed: %s", sdp);
        return ESP_FAIL;
    }
    size_t written = f.write(reinterpret_cast<const uint8_t *>(data), len);
    f.close();
    if (written != len) {
        ESP_LOGE(TAG, "short write %s (%zu of %zu)", sdp, written, len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sdcard_read_file(const char *path, char **out_buf, size_t *out_len)
{
    if (!path || !out_buf || !out_len) return ESP_ERR_INVALID_ARG;

    const char *sdp = sd_path(path);
    if (!SdMan.exists(sdp)) return ESP_ERR_NOT_FOUND;

    /* Open to get file size */
    FsFile f;
    if (!SdMan.openFileForRead("sdcard", sdp, f)) return ESP_ERR_NOT_FOUND;
    size_t sz = (size_t)f.fileSize();
    f.close();

    char *buf = static_cast<char *>(malloc(sz + 1));
    if (!buf) return ESP_ERR_NO_MEM;

    size_t bytes = SdMan.readFileToBuffer(sdp, buf, sz + 1);
    buf[bytes] = '\0';
    *out_buf = buf;
    *out_len = bytes;
    return ESP_OK;
}

esp_err_t sdcard_file_exists(const char *path, bool *exists)
{
    if (!path || !exists) return ESP_ERR_INVALID_ARG;
    *exists = SdMan.exists(sd_path(path));
    return ESP_OK;
}

esp_err_t sdcard_list_dir(const char *path, char ***entries, int *count)
{
    if (!path || !entries || !count) return ESP_ERR_INVALID_ARG;

    const char *sdp = sd_path(path);
    FsFile dir = SdMan.open(sdp, O_RDONLY);
    if (!dir || !dir.isDirectory()) {
        dir.close();
        *entries = nullptr;
        *count   = 0;
        return ESP_ERR_NOT_FOUND;
    }

    /* First pass: count non-hidden entries */
    int n = 0;
    char name[128];
    for (FsFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
        f.getName(name, sizeof(name));
        if (name[0] != '.') n++;
        f.close();
    }
    dir.close();

    if (n == 0) {
        *entries = nullptr;
        *count   = 0;
        return ESP_OK;
    }

    /* Second pass: collect names */
    dir = SdMan.open(sdp, O_RDONLY);
    char **list = static_cast<char **>(calloc(n, sizeof(char *)));
    if (!list) { dir.close(); return ESP_ERR_NO_MEM; }

    int i = 0;
    for (FsFile f = dir.openNextFile(); f && i < n; f = dir.openNextFile()) {
        f.getName(name, sizeof(name));
        if (name[0] != '.') list[i++] = strdup(name);
        f.close();
    }
    dir.close();

    *entries = list;
    *count   = i;
    return ESP_OK;
}

esp_err_t sdcard_delete_file(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    return SdMan.remove(sd_path(path)) ? ESP_OK : ESP_FAIL;
}

void sdcard_free_list(char **entries, int count)
{
    if (!entries) return;
    for (int i = 0; i < count; i++) free(entries[i]);
    free(entries);
}
