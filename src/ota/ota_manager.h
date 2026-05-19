#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Configuration macros (override via build_flags) --------------------- */

#ifndef CONFIG_OTA_MANIFEST_URL
#define CONFIG_OTA_MANIFEST_URL ""
#endif

#ifndef CONFIG_OTA_CHANNEL
#define CONFIG_OTA_CHANNEL "dev"
#endif

#ifndef CONFIG_OTA_MIN_HEAP_FOR_OTA
#define CONFIG_OTA_MIN_HEAP_FOR_OTA 65536
#endif

/* CA certificate PEM for TLS verification.  Empty = skip cert check (dev). */
#ifndef CONFIG_OTA_SERVER_CERT_PEM
#define CONFIG_OTA_SERVER_CERT_PEM ""
#endif

/* ---- OTA check result ----------------------------------------------------- */

typedef enum {
    OTA_CHECK_UPDATE_AVAILABLE = 0,
    OTA_CHECK_UP_TO_DATE,
    OTA_CHECK_REJECTED,
    OTA_CHECK_ERROR,
} ota_check_status_t;

typedef struct {
    ota_check_status_t status;
    char new_version[64];
    char current_version[64];
    char reject_reason[64]; /* populated when status == OTA_CHECK_REJECTED */
} ota_check_result_t;

/* ---- Public API ----------------------------------------------------------- */

/* Fetch and validate the OTA manifest.  Does NOT download the firmware.
   Must be called before ota_apply().  Thread-safe. */
esp_err_t ota_check(ota_check_result_t *result_out);

/* Download, verify SHA-256, and apply the most recently validated manifest.
   Calls esp_restart() on success — this function does not return normally.
   Returns an error code only if the process fails before reboot. */
esp_err_t ota_apply(void);

/* Rollback: mark current slot invalid and reboot into the previous slot.
   reason is a short string logged as: [X4] OTA_ROLLBACK_REQUESTED reason=<reason> */
void ota_rollback(const char *reason);

/* Mark the current slot valid (called after all health checks pass). */
void ota_mark_valid(void);

/* Returns true when the running partition is in PENDING_VERIFY state. */
bool ota_is_pending_verify(void);

/* Perform a quick TCP+TLS connection to the manifest server.
   Returns ESP_OK if reachable.  Timeout ≤ 10 seconds. */
esp_err_t ota_can_reach_manifest(void);

/* Returns a redacted manifest URL (query string stripped). */
void ota_get_manifest_url_redacted(char *buf, size_t len);

/* Returns CONFIG_OTA_CHANNEL. */
const char *ota_get_channel(void);

/* Returns the last check result (valid between ota_check() and ota_apply()). */
const ota_check_result_t *ota_last_check_result(void);

#ifdef __cplusplus
}
#endif
