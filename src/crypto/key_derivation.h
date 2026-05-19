#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_LEN 32  /* 256-bit AES key */

esp_err_t kdf_derive_key(const char *passphrase, const uint8_t *salt, size_t salt_len,
                          uint32_t iterations, uint8_t *out_key);
esp_err_t kdf_generate_salt(uint8_t *salt, size_t len);
esp_err_t kdf_generate_recovery_phrase(char *out_buf, size_t len);

#ifdef __cplusplus
}
#endif
