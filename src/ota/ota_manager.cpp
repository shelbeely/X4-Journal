/* ota_manager.cpp — Pull-based OTA subsystem.
 *
 * State machine: IDLE → CHECK → DOWNLOAD → VERIFY → REBOOT
 * On first boot after OTA, esp_ota_get_state_partition() returns
 * ESP_OTA_IMG_PENDING_VERIFY.  After all health checks pass, ota_mark_valid()
 * calls esp_ota_mark_app_valid_cancel_rollback(); on failure ota_rollback()
 * calls esp_ota_mark_app_invalid_rollback_and_reboot().
 *
 * All network I/O uses esp_http_client so we stay inside ESP-IDF's memory
 * budget rather than pulling in the Arduino HTTPClient.
 */
#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "ota";

/* ---- Manifest struct ------------------------------------------------------ */
typedef struct {
    char device[32];
    char channel[32];
    char version[64];
    char url[256];
    char sha256[65];   /* 64 hex chars + NUL */
    int  min_battery_percent;
    bool valid;        /* set to true after passing all validation rules */
} ota_manifest_t;

static ota_manifest_t    s_manifest   = {};
static ota_check_result_t s_last_result = {};

/* ---- Version comparison --------------------------------------------------- */
/* Compare two version strings of the form "prefix-N".
   Returns >0 if b is newer than a, 0 if equal, <0 if a is newer. */
static int version_cmp(const char *a, const char *b)
{
    if (!a || !b) return 1; /* assume b is newer */
    if (strcmp(a, b) == 0) return 0;

    /* Try to extract integer suffix after the last '-' */
    const char *da = strrchr(a, '-');
    const char *db = strrchr(b, '-');
    if (da && db) {
        int na = atoi(da + 1);
        int nb = atoi(db + 1);
        return nb - na; /* >0 means b newer */
    }
    /* Fallback: lexicographic */
    return strcmp(b, a);
}

/* ---- Manifest URL redaction ---------------------------------------------- */
void ota_get_manifest_url_redacted(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    const char *url = CONFIG_OTA_MANIFEST_URL;
    const char *q = strchr(url, '?');
    if (q) {
        size_t path_len = (size_t)(q - url);
        snprintf(buf, len, "%.*s[?...]", (int)path_len, url);
    } else {
        strncpy(buf, url, len - 1);
        buf[len - 1] = '\0';
    }
}

const char *ota_get_channel(void)
{
    return CONFIG_OTA_CHANNEL;
}

const ota_check_result_t *ota_last_check_result(void)
{
    return &s_last_result;
}

/* ---- esp_http_client helpers --------------------------------------------- */

static int s_http_body_len = 0;
static char *s_http_body   = nullptr;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            if (s_http_body && evt->data_len > 0) {
                /* Append to body buffer */
                memcpy(s_http_body + s_http_body_len,
                       evt->data,
                       (size_t)evt->data_len);
                s_http_body_len += evt->data_len;
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Fetch URL into a heap-allocated buffer.  Caller must free().
   Returns ESP_OK and sets *body_out / *len_out on success. */
static esp_err_t http_get_body(const char *url, char **body_out, int *len_out,
                                int timeout_ms)
{
    *body_out = nullptr;
    *len_out  = 0;

    /* Pre-allocate a reasonable response buffer */
    const int max_body = 4096;
    char *buf = (char *)malloc(max_body + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    memset(buf, 0, max_body + 1);

    s_http_body     = buf;
    s_http_body_len = 0;

    esp_http_client_config_t cfg = {};
    cfg.url         = url;
    cfg.timeout_ms  = timeout_ms;
    cfg.event_handler = http_event_handler;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 512;
    if (strlen(CONFIG_OTA_SERVER_CERT_PEM) > 0) {
        cfg.cert_pem = CONFIG_OTA_SERVER_CERT_PEM;
    } else {
        cfg.skip_cert_common_name_check = true;
        cfg.use_global_ca_store = false;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(buf); s_http_body = nullptr; return ESP_FAIL; }

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    s_http_body = nullptr;

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "HTTP GET %s failed: err=%s status=%d",
                 url, esp_err_to_name(err), status);
        free(buf);
        return (err != ESP_OK) ? err : ESP_FAIL;
    }

    *body_out = buf;
    *len_out  = s_http_body_len;
    return ESP_OK;
}

/* ---- Manifest parsing ----------------------------------------------------- */

static esp_err_t parse_manifest(const char *json, ota_manifest_t *m)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Manifest JSON parse error");
        return ESP_FAIL;
    }
    cJSON *jdev  = cJSON_GetObjectItem(root, "device");
    cJSON *jchan = cJSON_GetObjectItem(root, "channel");
    cJSON *jver  = cJSON_GetObjectItem(root, "version");
    cJSON *jurl  = cJSON_GetObjectItem(root, "url");
    cJSON *jsha  = cJSON_GetObjectItem(root, "sha256");
    cJSON *jbat  = cJSON_GetObjectItem(root, "min_battery_percent");

    if (!cJSON_IsString(jdev)  || !cJSON_IsString(jchan) ||
        !cJSON_IsString(jver)  || !cJSON_IsString(jurl)  ||
        !cJSON_IsString(jsha)) {
        ESP_LOGE(TAG, "Manifest missing required fields");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(m->device,  jdev->valuestring,  sizeof(m->device)-1);
    strncpy(m->channel, jchan->valuestring, sizeof(m->channel)-1);
    strncpy(m->version, jver->valuestring,  sizeof(m->version)-1);
    strncpy(m->url,     jurl->valuestring,  sizeof(m->url)-1);
    strncpy(m->sha256,  jsha->valuestring,  sizeof(m->sha256)-1);
    m->min_battery_percent = cJSON_IsNumber(jbat) ? (int)jbat->valuedouble : 0;
    m->valid = false;

    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- ota_check ------------------------------------------------------------ */

esp_err_t ota_check(ota_check_result_t *result_out)
{
    memset(&s_manifest,    0, sizeof(s_manifest));
    memset(&s_last_result, 0, sizeof(s_last_result));

    const char *url = CONFIG_OTA_MANIFEST_URL;
    if (!url || !url[0]) {
        ESP_LOGE(TAG, "[X4] OTA_CHECK_START url=(none) — manifest URL not configured");
        s_last_result.status = OTA_CHECK_REJECTED;
        strncpy(s_last_result.reject_reason, "no_manifest_url",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[X4] OTA_CHECK_START url=%s", url);

    /* Fetch manifest */
    char *body = nullptr;
    int   body_len = 0;
    esp_err_t err = http_get_body(url, &body, &body_len, 10000);
    if (err != ESP_OK || !body) {
        ESP_LOGE(TAG, "Manifest fetch failed");
        s_last_result.status = OTA_CHECK_ERROR;
        strncpy(s_last_result.reject_reason, "fetch_failed",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        if (body) free(body);
        return err;
    }

    ota_manifest_t m = {};
    err = parse_manifest(body, &m);
    free(body);
    if (err != ESP_OK) {
        s_last_result.status = OTA_CHECK_ERROR;
        strncpy(s_last_result.reject_reason, "parse_failed",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        return err;
    }

    ESP_LOGI(TAG, "[X4] OTA_MANIFEST_OK version=%s", m.version);

    /* Rule 1: wrong device */
    if (strcmp(m.device, "xteink-x4") != 0) {
        ESP_LOGW(TAG, "OTA rejected reason=wrong_device");
        s_last_result.status = OTA_CHECK_REJECTED;
        strncpy(s_last_result.reject_reason, "wrong_device",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        return ESP_FAIL;
    }

    /* Rule 2: wrong channel */
    if (strcmp(m.channel, CONFIG_OTA_CHANNEL) != 0) {
        ESP_LOGW(TAG, "OTA rejected reason=wrong_channel (manifest=%s config=%s)",
                 m.channel, CONFIG_OTA_CHANNEL);
        s_last_result.status = OTA_CHECK_REJECTED;
        strncpy(s_last_result.reject_reason, "wrong_channel",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        return ESP_FAIL;
    }

    /* Rule 3: version not newer */
    const esp_app_desc_t *running = esp_app_get_description();
    const char *cur_ver = running ? running->version : "";
    strncpy(s_last_result.current_version, cur_ver,
            sizeof(s_last_result.current_version)-1);

    if (version_cmp(cur_ver, m.version) <= 0) {
        ESP_LOGI(TAG, "OTA up-to-date: running=%s manifest=%s", cur_ver, m.version);
        s_last_result.status = OTA_CHECK_UP_TO_DATE;
        strncpy(s_last_result.new_version, m.version,
                sizeof(s_last_result.new_version)-1);
        if (result_out) *result_out = s_last_result;
        return ESP_OK;
    }

    /* Rule 4: insufficient flash */
    const esp_partition_t *inactive = esp_ota_get_next_update_partition(nullptr);
    if (!inactive) {
        ESP_LOGE(TAG, "OTA rejected reason=no_inactive_slot");
        s_last_result.status = OTA_CHECK_REJECTED;
        strncpy(s_last_result.reject_reason, "no_inactive_slot",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        return ESP_FAIL;
    }

    /* Rule 5: low heap */
    size_t heap_free = esp_get_free_heap_size();
    if (heap_free < CONFIG_OTA_MIN_HEAP_FOR_OTA) {
        ESP_LOGW(TAG, "OTA rejected reason=low_heap free=%zu min=%d",
                 heap_free, CONFIG_OTA_MIN_HEAP_FOR_OTA);
        s_last_result.status = OTA_CHECK_REJECTED;
        strncpy(s_last_result.reject_reason, "low_heap",
                sizeof(s_last_result.reject_reason)-1);
        if (result_out) *result_out = s_last_result;
        return ESP_FAIL;
    }

    /* All pre-download checks passed */
    m.valid = true;
    s_manifest = m;
    s_last_result.status = OTA_CHECK_UPDATE_AVAILABLE;
    strncpy(s_last_result.new_version, m.version,
            sizeof(s_last_result.new_version)-1);

    ESP_LOGI(TAG, "OTA update available: %s → %s", cur_ver, m.version);
    if (result_out) *result_out = s_last_result;
    return ESP_OK;
}

/* ---- ota_apply ------------------------------------------------------------ */

esp_err_t ota_apply(void)
{
    if (!s_manifest.valid) {
        ESP_LOGE(TAG, "ota_apply: no validated manifest — call ota_check() first");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot) {
        ESP_LOGE(TAG, "ota_apply: no inactive OTA slot available");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[X4] OTA_DOWNLOAD_START url=%s", s_manifest.url);

    /* Begin OTA handle */
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Init SHA-256 context */
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); /* 0 = SHA-256 */

    /* Configure HTTP client for firmware binary download */
    esp_http_client_config_t cfg = {};
    cfg.url        = s_manifest.url;
    cfg.timeout_ms = 60000;
    cfg.buffer_size = 4096;
    if (strlen(CONFIG_OTA_SERVER_CERT_PEM) > 0) {
        cfg.cert_pem = CONFIG_OTA_SERVER_CERT_PEM;
    } else {
        cfg.skip_cert_common_name_check = true;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        esp_ota_abort(ota_handle);
        mbedtls_sha256_free(&sha256_ctx);
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        mbedtls_sha256_free(&sha256_ctx);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) content_length = 0;
    if (content_length > 0) {
        ESP_LOGI(TAG, "[X4] OTA_DOWNLOAD_START size=%d", content_length);
    }

    /* Stream download into OTA slot */
    static uint8_t chunk[4096];
    int total_written = 0;
    while (true) {
        int bytes_read = esp_http_client_read(client, (char *)chunk, sizeof(chunk));
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "HTTP read error: %d", bytes_read);
            err = ESP_FAIL;
            break;
        }
        if (bytes_read == 0) break; /* EOF */

        mbedtls_sha256_update(&sha256_ctx, chunk, bytes_read);

        err = esp_ota_write(ota_handle, chunk, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            break;
        }
        total_written += bytes_read;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        esp_ota_abort(ota_handle);
        mbedtls_sha256_free(&sha256_ctx);
        return err;
    }

    ESP_LOGI(TAG, "[X4] OTA_DOWNLOAD_OK bytes=%d", total_written);

    /* Verify SHA-256 */
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha256_ctx, hash);
    mbedtls_sha256_free(&sha256_ctx);

    char hash_hex[65] = {};
    for (int i = 0; i < 32; i++) snprintf(hash_hex + i*2, 3, "%02x", hash[i]);

    if (strcasecmp(hash_hex, s_manifest.sha256) != 0) {
        ESP_LOGE(TAG, "SHA-256 mismatch! expected=%s got=%s",
                 s_manifest.sha256, hash_hex);
        esp_ota_abort(ota_handle);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[X4] OTA_SHA256_OK hash=%.16s", hash_hex);

    /* Finalise OTA and switch boot partition */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "[X4] OTA_APPLY_OK slot=%s", slot->label);

    /* Clear the validated manifest so a stale apply is not possible */
    memset(&s_manifest, 0, sizeof(s_manifest));

    vTaskDelay(pdMS_TO_TICKS(200)); /* let the log flush */
    esp_restart();
    /* Does not return */
    return ESP_OK;
}

/* ---- Rollback & valid mark ------------------------------------------------ */

void ota_rollback(const char *reason)
{
    ESP_LOGE(TAG, "[X4] OTA_ROLLBACK_REQUESTED reason=%s",
             reason ? reason : "unknown");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_ota_mark_app_invalid_rollback_and_reboot();
    /* If the above is unavailable (older ESP-IDF), fall through to esp_restart() */
    esp_restart();
}

void ota_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[X4] OTA_MARK_VALID");
    } else {
        ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback: %s",
                 esp_err_to_name(err));
    }
}

bool ota_is_pending_verify(void)
{
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (!part) return false;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(part, &state) != ESP_OK) return false;
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

/* ---- Manifest reachability check ----------------------------------------- */

esp_err_t ota_can_reach_manifest(void)
{
    const char *url = CONFIG_OTA_MANIFEST_URL;
    if (!url || !url[0]) return ESP_FAIL;

    esp_http_client_config_t cfg = {};
    cfg.url        = url;
    cfg.timeout_ms = 10000;
    cfg.method     = HTTP_METHOD_HEAD;
    cfg.buffer_size = 512;
    if (strlen(CONFIG_OTA_SERVER_CERT_PEM) > 0) {
        cfg.cert_pem = CONFIG_OTA_SERVER_CERT_PEM;
    } else {
        cfg.skip_cert_common_name_check = true;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && (status == 200 || status == 204 || status == 405)) {
        /* 405 = HEAD not allowed but server is reachable */
        return ESP_OK;
    }
    ESP_LOGW(TAG, "ota_can_reach_manifest: err=%s status=%d",
             esp_err_to_name(err), status);
    return (err != ESP_OK) ? err : ESP_FAIL;
}
