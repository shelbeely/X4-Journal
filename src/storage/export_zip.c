#include "export_zip.h"
#include "journal_fs.h"
#include "sdcard.h"
#include "rtc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "export_zip";

/*
 * Minimal store-only ZIP writer (no compression, method=0).
 * Implements just enough of the ZIP specification to produce a valid file
 * that can be opened by standard tools.
 */

/* CRC-32 lookup table (polynomial 0xEDB88320) */
static uint32_t crc32_table[256];
static bool     crc32_init_done = false;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_init_done = true;
}

static uint32_t crc32_buf(const uint8_t *buf, size_t len)
{
    if (!crc32_init_done) crc32_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c = crc32_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static void write_u16le(FILE *f, uint16_t v)
{
    uint8_t b[2] = { v & 0xFF, (v >> 8) & 0xFF };
    fwrite(b, 1, 2, f);
}

static void write_u32le(FILE *f, uint32_t v)
{
    uint8_t b[4] = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF };
    fwrite(b, 1, 4, f);
}

typedef struct {
    char     name[128];
    uint32_t crc32;
    uint32_t size;
    uint32_t offset;   /* local header offset */
} zip_entry_t;

#define MAX_ZIP_FILES 512

static zip_entry_t s_entries[MAX_ZIP_FILES];
static int         s_nentries;

static void write_local_header(FILE *f, const char *name, uint32_t size, uint32_t crc)
{
    uint16_t namelen = strlen(name);
    fwrite("\x50\x4B\x03\x04", 1, 4, f); /* local file header sig */
    write_u16le(f, 20);          /* version needed: 2.0 */
    write_u16le(f, 0);           /* general purpose bit flag */
    write_u16le(f, 0);           /* compression method: stored */
    write_u16le(f, 0);           /* last mod time */
    write_u16le(f, 0);           /* last mod date */
    write_u32le(f, crc);         /* CRC-32 */
    write_u32le(f, size);        /* compressed size */
    write_u32le(f, size);        /* uncompressed size */
    write_u16le(f, namelen);     /* file name length */
    write_u16le(f, 0);           /* extra field length */
    fwrite(name, 1, namelen, f); /* file name */
}

static void write_central_dir_entry(FILE *f, const zip_entry_t *e)
{
    uint16_t namelen = strlen(e->name);
    fwrite("\x50\x4B\x01\x02", 1, 4, f); /* central dir sig */
    write_u16le(f, 20);          /* version made by */
    write_u16le(f, 20);          /* version needed */
    write_u16le(f, 0);           /* flag */
    write_u16le(f, 0);           /* compression */
    write_u16le(f, 0);           /* last mod time */
    write_u16le(f, 0);           /* last mod date */
    write_u32le(f, e->crc32);
    write_u32le(f, e->size);
    write_u32le(f, e->size);
    write_u16le(f, namelen);
    write_u16le(f, 0);           /* extra length */
    write_u16le(f, 0);           /* comment length */
    write_u16le(f, 0);           /* disk start */
    write_u16le(f, 0);           /* internal attr */
    write_u32le(f, 0);           /* external attr */
    write_u32le(f, e->offset);
    fwrite(e->name, 1, namelen, f);
}

static int add_directory_to_zip(FILE *zf, const char *dir_path, const char *zip_prefix)
{
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    struct dirent *ent;
    while ((ent = readdir(d)) && s_nentries < MAX_ZIP_FILES) {
        if (ent->d_name[0] == '.') continue;
        char full_path[256], zip_name[200];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
        snprintf(zip_name,  sizeof(zip_name),  "%s%s",  zip_prefix, ent->d_name);

        struct stat st;
        stat(full_path, &st);
        if (S_ISDIR(st.st_mode)) {
            char sub_prefix[200];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s/", zip_name);
            add_directory_to_zip(zf, full_path, sub_prefix);
            continue;
        }

        /* read file */
        char *buf = NULL;
        size_t len = 0;
        if (sdcard_read_file(full_path, &buf, &len) != ESP_OK) continue;

        uint32_t crc  = crc32_buf((const uint8_t *)buf, len);
        uint32_t off  = (uint32_t)ftell(zf);

        write_local_header(zf, zip_name, len, crc);
        fwrite(buf, 1, len, zf);
        free(buf);

        strlcpy(s_entries[s_nentries].name, zip_name, sizeof(s_entries[0].name));
        s_entries[s_nentries].crc32  = crc;
        s_entries[s_nentries].size   = (uint32_t)len;
        s_entries[s_nentries].offset = off;
        s_nentries++;
    }
    closedir(d);
    return 0;
}

esp_err_t export_create_zip(const char *out_path)
{
    if (!out_path) return ESP_ERR_INVALID_ARG;

    sdcard_mkdir_p(EXPORT_ROOT);

    FILE *zf = fopen(out_path, "wb");
    if (!zf) {
        ESP_LOGE(TAG, "cannot create %s", out_path);
        return ESP_FAIL;
    }

    s_nentries = 0;
    add_directory_to_zip(zf, JOURNAL_ROOT, "journal/");

    /* write central directory */
    uint32_t cd_offset = (uint32_t)ftell(zf);
    for (int i = 0; i < s_nentries; i++) {
        write_central_dir_entry(zf, &s_entries[i]);
    }
    uint32_t cd_size = (uint32_t)ftell(zf) - cd_offset;

    /* end of central directory record */
    fwrite("\x50\x4B\x05\x06", 1, 4, zf);
    write_u16le(zf, 0);
    write_u16le(zf, 0);
    write_u16le(zf, (uint16_t)s_nentries);
    write_u16le(zf, (uint16_t)s_nentries);
    write_u32le(zf, cd_size);
    write_u32le(zf, cd_offset);
    write_u16le(zf, 0); /* comment length */

    fclose(zf);
    ESP_LOGI(TAG, "zip created: %s (%d files)", out_path, s_nentries);
    return ESP_OK;
}
