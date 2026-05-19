#include "vault.h"
#include "key_derivation.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "vault";

#define NVS_NAMESPACE   "vault"
#define NVS_KEY_SALT    "salt"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_ITERS   "iters"

#define GCM_IV_LEN   12
#define GCM_TAG_LEN  16
#define SALT_LEN     16

static vault_state_t s_state   = VAULT_STATE_UNINITIALIZED;
static bool          s_enabled = false;
static uint8_t       s_key[KEY_LEN];
static uint8_t       s_salt[SALT_LEN];
static uint32_t      s_iterations = 10000;

esp_err_t vault_init(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        /* vault not configured yet */
        s_state   = VAULT_STATE_UNINITIALIZED;
        s_enabled = false;
        return ESP_OK;
    }
    uint8_t enabled = 0;
    nvs_get_u8(h, NVS_KEY_ENABLED, &enabled);
    s_enabled = (enabled != 0);

    size_t salt_len = SALT_LEN;
    nvs_get_blob(h, NVS_KEY_SALT, s_salt, &salt_len);
    nvs_get_u32(h, NVS_KEY_ITERS, &s_iterations);
    nvs_close(h);

    s_state = s_enabled ? VAULT_STATE_LOCKED : VAULT_STATE_UNLOCKED;
    ESP_LOGI(TAG, "vault_init: enabled=%d state=%d", s_enabled, s_state);
    return ESP_OK;
}

vault_state_t vault_get_state(void)  { return s_state; }
bool          vault_is_enabled(void) { return s_enabled; }

esp_err_t vault_setup(const char *passphrase, char *recovery_out, size_t recovery_len)
{
    if (!passphrase) return ESP_ERR_INVALID_ARG;

    ESP_ERROR_CHECK(kdf_generate_salt(s_salt, SALT_LEN));
    ESP_ERROR_CHECK(kdf_derive_key(passphrase, s_salt, SALT_LEN, s_iterations, s_key));
    s_enabled = true;
    s_state   = VAULT_STATE_UNLOCKED;

    /* persist */
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    nvs_set_u8(h,   NVS_KEY_ENABLED, 1);
    nvs_set_blob(h, NVS_KEY_SALT, s_salt, SALT_LEN);
    nvs_set_u32(h,  NVS_KEY_ITERS, s_iterations);
    nvs_commit(h);
    nvs_close(h);

    if (recovery_out) {
        kdf_generate_recovery_phrase(recovery_out, recovery_len);
    }
    ESP_LOGI(TAG, "vault setup complete");
    return ESP_OK;
}

esp_err_t vault_unlock(const char *passphrase)
{
    if (!passphrase || !s_enabled) return ESP_ERR_INVALID_STATE;
    uint8_t derived[KEY_LEN];
    ESP_ERROR_CHECK(kdf_derive_key(passphrase, s_salt, SALT_LEN, s_iterations, derived));
    memcpy(s_key, derived, KEY_LEN);
    s_state = VAULT_STATE_UNLOCKED;
    return ESP_OK;
}

void vault_lock(void)
{
    memset(s_key, 0, sizeof(s_key));
    if (s_enabled) s_state = VAULT_STATE_LOCKED;
}

esp_err_t vault_encrypt_entry(const char *plaintext, size_t pt_len,
                               uint8_t **out, size_t *out_len)
{
    if (!plaintext || !out || !out_len) return ESP_ERR_INVALID_ARG;

    /* pass-through when vault is disabled */
    if (!s_enabled) {
        *out = (uint8_t *)malloc(pt_len + 1);
        if (!*out) return ESP_ERR_NO_MEM;
        memcpy(*out, plaintext, pt_len + 1);
        *out_len = pt_len;
        return ESP_OK;
    }

    if (s_state != VAULT_STATE_UNLOCKED) return ESP_ERR_INVALID_STATE;

    /* format: [12B IV][16B tag][ciphertext] */
    size_t  total = GCM_IV_LEN + GCM_TAG_LEN + pt_len;
    uint8_t *buf  = malloc(total);
    if (!buf) return ESP_ERR_NO_MEM;

    uint8_t *iv  = buf;
    uint8_t *tag = buf + GCM_IV_LEN;
    uint8_t *ct  = buf + GCM_IV_LEN + GCM_TAG_LEN;

    esp_fill_random(iv, GCM_IV_LEN);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, s_key, KEY_LEN * 8);
    if (ret) { free(buf); mbedtls_gcm_free(&gcm); return ESP_FAIL; }

    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                     pt_len, iv, GCM_IV_LEN,
                                     NULL, 0,
                                     (const unsigned char *)plaintext,
                                     ct, GCM_TAG_LEN, tag);
    mbedtls_gcm_free(&gcm);
    if (ret) { free(buf); return ESP_FAIL; }

    *out     = buf;
    *out_len = total;
    return ESP_OK;
}

esp_err_t vault_decrypt_entry(const uint8_t *ciphertext, size_t ct_len,
                               char **out, size_t *out_len)
{
    if (!ciphertext || !out || !out_len) return ESP_ERR_INVALID_ARG;

    /* pass-through when vault is disabled */
    if (!s_enabled) {
        *out = (char *)malloc(ct_len + 1);
        if (!*out) return ESP_ERR_NO_MEM;
        memcpy(*out, ciphertext, ct_len);
        (*out)[ct_len] = '\0';
        *out_len = ct_len;
        return ESP_OK;
    }

    if (s_state != VAULT_STATE_UNLOCKED) return ESP_ERR_INVALID_STATE;
    if (ct_len < (size_t)(GCM_IV_LEN + GCM_TAG_LEN)) return ESP_ERR_INVALID_SIZE;

    const uint8_t *iv  = ciphertext;
    const uint8_t *tag = ciphertext + GCM_IV_LEN;
    const uint8_t *enc = ciphertext + GCM_IV_LEN + GCM_TAG_LEN;
    size_t enc_len     = ct_len - GCM_IV_LEN - GCM_TAG_LEN;

    char *plain = malloc(enc_len + 1);
    if (!plain) return ESP_ERR_NO_MEM;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, s_key, KEY_LEN * 8);

    int ret = mbedtls_gcm_auth_decrypt(&gcm, enc_len,
                                        iv, GCM_IV_LEN,
                                        NULL, 0,
                                        tag, GCM_TAG_LEN,
                                        enc, (unsigned char *)plain);
    mbedtls_gcm_free(&gcm);
    if (ret) { free(plain); return ESP_FAIL; }

    plain[enc_len] = '\0';
    *out     = plain;
    *out_len = enc_len;
    return ESP_OK;
}
