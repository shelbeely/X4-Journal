/* export_zip.cpp — Minimal store-only ZIP writer.
 *
 * Uses SDCardManager (SdFat FsFile) for both reading source files and writing
 * the output ZIP, so it works without the ESP-IDF VFS layer.
 *
 * Directory traversal uses sdcard_list_dir() (which also lists subdirectories
 * returned by FsFile::openNextFile) so we detect dirs via SdMan.open().isDirectory().
 */
#include "export_zip.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "rtc.h"
#include "esp_log.h"
#include <SDCardManager.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static const char *TAG = "export_zip";

/* ---------- CRC-32 (polynomial 0xEDB88320) -------------------------------- */

static uint32_t s_crc32_table[256];
static bool     s_crc32_ready = false;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc32_table[i] = c;
    }
    s_crc32_ready = true;
}

static uint32_t crc32_buf(const uint8_t *buf, size_t len)
{
    if (!s_crc32_ready) crc32_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = s_crc32_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---------- Low-level ZIP helpers ----------------------------------------- */

static void write_u16le(FsFile &f, uint16_t v)
{
    uint8_t b[2] = { static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8) };
    f.write(b, 2);
}

static void write_u32le(FsFile &f, uint32_t v)
{
    uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8)  & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
    f.write(b, 4);
}

typedef struct {
    char     name[128];
    uint32_t crc32;
    uint32_t size;
    uint32_t offset;
} zip_entry_t;

#define MAX_ZIP_FILES 256

static zip_entry_t *s_entries  = nullptr;
static int          s_nentries = 0;

static void write_local_header(FsFile &f, const char *name, uint32_t size, uint32_t crc)
{
    uint16_t namelen = (uint16_t)strlen(name);
    f.write(reinterpret_cast<const uint8_t *>("\x50\x4B\x03\x04"), 4);
    write_u16le(f, 20); write_u16le(f, 0); write_u16le(f, 0);
    write_u16le(f, 0);  write_u16le(f, 0);
    write_u32le(f, crc);
    write_u32le(f, size); write_u32le(f, size);
    write_u16le(f, namelen); write_u16le(f, 0);
    f.write(reinterpret_cast<const uint8_t *>(name), namelen);
}

static void write_central_dir_entry(FsFile &f, const zip_entry_t *e)
{
    uint16_t namelen = (uint16_t)strlen(e->name);
    f.write(reinterpret_cast<const uint8_t *>("\x50\x4B\x01\x02"), 4);
    write_u16le(f, 20); write_u16le(f, 20); write_u16le(f, 0); write_u16le(f, 0);
    write_u16le(f, 0);  write_u16le(f, 0);
    write_u32le(f, e->crc32);
    write_u32le(f, e->size); write_u32le(f, e->size);
    write_u16le(f, namelen); write_u16le(f, 0); write_u16le(f, 0);
    write_u16le(f, 0); write_u16le(f, 0); write_u32le(f, 0); write_u32le(f, e->offset);
    f.write(reinterpret_cast<const uint8_t *>(e->name), namelen);
}

/* ---------- Recursive directory → ZIP ----------------------------------------
 *
 * dir_path_vfs : VFS-style path  e.g. "/sdcard/journal/2026/05"
 * zip_prefix   : path inside ZIP e.g. "journal/2026/05/"
 */
static void add_dir_to_zip(FsFile &zf, const char *dir_path_vfs, const char *zip_prefix)
{
    char **names = nullptr;
    int n = 0;
    if (sdcard_list_dir(dir_path_vfs, &names, &n) != ESP_OK || n == 0) {
        sdcard_free_list(names, n);
        return;
    }

    for (int i = 0; i < n && s_nentries < MAX_ZIP_FILES; i++) {
        /* Build full VFS path */
        char full_vfs[300];
        snprintf(full_vfs, sizeof(full_vfs), "%s/%s", dir_path_vfs, names[i]);

        /* Build ZIP entry name */
        char zip_name[256];
        snprintf(zip_name, sizeof(zip_name), "%s%s", zip_prefix, names[i]);

        /* Check if it's a directory by opening the SD-relative path */
        const char *sdp = full_vfs;
        if (strncmp(sdp, "/sdcard", 7) == 0) sdp += 7;

        FsFile check = SdMan.open(sdp, O_RDONLY);
        bool is_dir = (check && check.isDirectory());
        check.close();

        if (is_dir) {
            /* Recurse */
            char sub_prefix[256];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s/", zip_name);
            add_dir_to_zip(zf, full_vfs, sub_prefix);
        } else {
            /* Read file and append to ZIP */
            char *buf = nullptr;
            size_t len = 0;
            if (sdcard_read_file(full_vfs, &buf, &len) != ESP_OK) continue;

            uint32_t crc = crc32_buf(reinterpret_cast<const uint8_t *>(buf), len);
            uint32_t off = (uint32_t)zf.curPosition();

            write_local_header(zf, zip_name, (uint32_t)len, crc);
            zf.write(reinterpret_cast<const uint8_t *>(buf), len);
            free(buf);

            strlcpy(s_entries[s_nentries].name, zip_name, sizeof(s_entries[0].name));
            s_entries[s_nentries].crc32  = crc;
            s_entries[s_nentries].size   = (uint32_t)len;
            s_entries[s_nentries].offset = off;
            s_nentries++;
        }
    }
    sdcard_free_list(names, n);
}

/* ---------- Public API ----------------------------------------------------- */

esp_err_t export_create_zip(const char *out_path)
{
    if (!out_path) return ESP_ERR_INVALID_ARG;

    sdcard_mkdir_p(EXPORT_ROOT);

    /* Allocate entries table on the heap for the duration of the export */
    s_entries = static_cast<zip_entry_t *>(calloc(MAX_ZIP_FILES, sizeof(zip_entry_t)));
    if (!s_entries) return ESP_ERR_NO_MEM;
    s_nentries = 0;

    /* Strip /sdcard prefix for SDCardManager */
    const char *sdp = out_path;
    if (strncmp(sdp, "/sdcard", 7) == 0) sdp += 7;

    FsFile zf;
    if (!SdMan.openFileForWrite("export_zip", sdp, zf)) {
        ESP_LOGE(TAG, "cannot create: %s", sdp);
        free(s_entries); s_entries = nullptr;
        return ESP_FAIL;
    }

    add_dir_to_zip(zf, JOURNAL_ROOT, "journal/");

    /* Central directory */
    uint32_t cd_offset = (uint32_t)zf.curPosition();
    for (int i = 0; i < s_nentries; i++) write_central_dir_entry(zf, &s_entries[i]);
    uint32_t cd_size = (uint32_t)zf.curPosition() - cd_offset;

    /* End of central directory record */
    zf.write(reinterpret_cast<const uint8_t *>("\x50\x4B\x05\x06"), 4);
    write_u16le(zf, 0); write_u16le(zf, 0);
    write_u16le(zf, (uint16_t)s_nentries);
    write_u16le(zf, (uint16_t)s_nentries);
    write_u32le(zf, cd_size);
    write_u32le(zf, cd_offset);
    write_u16le(zf, 0);

    zf.close();
    ESP_LOGI(TAG, "zip created: %s (%d files)", sdp, s_nentries);
    free(s_entries); s_entries = nullptr;
    return ESP_OK;
}
