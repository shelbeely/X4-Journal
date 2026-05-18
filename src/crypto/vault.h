#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    VAULT_STATE_UNLOCKED,
    VAULT_STATE_LOCKED,
    VAULT_STATE_UNINITIALIZED,
} vault_state_t;

esp_err_t    vault_init(void);
vault_state_t vault_get_state(void);
bool         vault_is_enabled(void);
esp_err_t    vault_setup(const char *passphrase, char *recovery_phrase_out, size_t len);
esp_err_t    vault_unlock(const char *passphrase);
void         vault_lock(void);
esp_err_t    vault_encrypt_entry(const char *plaintext, size_t pt_len,
                                  uint8_t **out, size_t *out_len);
esp_err_t    vault_decrypt_entry(const uint8_t *ciphertext, size_t ct_len,
                                  char **out, size_t *out_len);
