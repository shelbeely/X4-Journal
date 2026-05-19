/* log_buffer.cpp — Circular log ring buffer with vprintf hook.
 *
 * Installs a custom vprintf function via esp_log_set_vprintf() that tees
 * every ESP-IDF log line to both UART (via the original vprintf) and a
 * fixed-size circular buffer in heap memory.  The buffer is protected by a
 * FreeRTOS mutex so it is safe to read from one task while logs arrive from
 * another.
 */
#include "log_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- Internal state ------------------------------------------------------- */

static char s_lines[LOG_BUFFER_LINES][LOG_BUFFER_LINE_LEN];
static size_t s_head   = 0;   /* index of the oldest entry */
static size_t s_count  = 0;   /* number of valid entries */
static SemaphoreHandle_t s_mutex = nullptr;
static bool s_init = false;
static vprintf_like_t s_orig_vprintf = nullptr;

/* ---- vprintf hook --------------------------------------------------------- */

static int log_vprintf_hook(const char *fmt, va_list args)
{
    /* Always write to UART first via the original vprintf */
    int ret = 0;
    if (s_orig_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = s_orig_vprintf(fmt, args_copy);
        va_end(args_copy);
    }

    /* Append formatted line to the ring buffer */
    char line[LOG_BUFFER_LINE_LEN];
    vsnprintf(line, sizeof(line), fmt, args);
    /* Strip trailing newline */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    if (len == 0) return ret;

    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        size_t slot;
        if (s_count < LOG_BUFFER_LINES) {
            slot = (s_head + s_count) % LOG_BUFFER_LINES;
            s_count++;
        } else {
            /* Overwrite oldest */
            slot  = s_head;
            s_head = (s_head + 1) % LOG_BUFFER_LINES;
        }
        strncpy(s_lines[slot], line, LOG_BUFFER_LINE_LEN - 1);
        s_lines[slot][LOG_BUFFER_LINE_LEN - 1] = '\0';
        xSemaphoreGive(s_mutex);
    }
    return ret;
}

/* ---- Public API ----------------------------------------------------------- */

void log_buffer_init(void)
{
    if (s_init) return;
    memset(s_lines, 0, sizeof(s_lines));
    s_head  = 0;
    s_count = 0;
    s_mutex = xSemaphoreCreateMutex();
    s_orig_vprintf = esp_log_set_vprintf(log_vprintf_hook);
    s_init = true;
}

void log_buffer_get_lines(const char **lines_out, size_t *count_out)
{
    if (!lines_out || !count_out) return;
    size_t max = *count_out;
    *count_out = 0;

    if (!s_init || !s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    size_t n = (s_count < max) ? s_count : max;
    for (size_t i = 0; i < n; i++) {
        lines_out[i] = s_lines[(s_head + i) % LOG_BUFFER_LINES];
    }
    *count_out = n;
    xSemaphoreGive(s_mutex);
}

void log_buffer_clear(void)
{
    if (!s_init || !s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    memset(s_lines, 0, sizeof(s_lines));
    s_head  = 0;
    s_count = 0;
    xSemaphoreGive(s_mutex);
}

bool log_buffer_is_init(void)
{
    return s_init;
}
