#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of lines retained in the circular buffer */
#define LOG_BUFFER_LINES  64
/* Maximum characters per line (including NUL) */
#define LOG_BUFFER_LINE_LEN 192

/* Initialise the log ring buffer and install the vprintf hook that tees
   every ESP-IDF log line into the buffer while still printing to UART. */
void log_buffer_init(void);

/* Copy up to *count_out most-recent lines into lines_out (caller's
   fixed-size array).  On return *count_out holds the actual line count.
   Lines are NUL-terminated strings; pointers are valid until the next call
   that writes to the buffer (i.e. the caller must process them quickly or
   copy them). */
void log_buffer_get_lines(const char **lines_out, size_t *count_out);

/* Discard all buffered lines. */
void log_buffer_clear(void);

/* Returns true if log_buffer_init() has been called. */
bool log_buffer_is_init(void);

#ifdef __cplusplus
}
#endif
