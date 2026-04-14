#include "split_crypto.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "psa/crypto.h"

#define TAG "SPLIT_CR"

/* Maximum ESP-NOW payload that will ever be encrypted/decrypted:
 * SPLIT_ESP_NOW_MAX(250) - SPLIT_FRAME_OVERHEAD(10) = 240 bytes. */
#define CRYPTO_BUF_MAX 244

/* Single static DMA-capable bounce buffer protected by a binary semaphore. */
static uint8_t           s_dma_buf[CRYPTO_BUF_MAX];
static SemaphoreHandle_t s_dma_sem = NULL;
static portMUX_TYPE      s_init_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    psa_key_id_t key_id;
} split_ecdh_context_t;

static void ensure_psa_init(void)
{
    static bool s_psa_init = false;
    if (s_psa_init) return;

    portENTER_CRITICAL(&s_init_mux);
    if (!s_psa_init) {
        if (psa_crypto_init() == PSA_SUCCESS) {
            s_psa_init = true;
        } else {
            ESP_LOGE(TAG, "psa_crypto_init failed");
        }
    }
    portEXIT_CRITICAL(&s_init_mux);
}

static void ensure_dma_sem(void)
{
    if (s_dma_sem) return;
    portENTER_CRITICAL(&s_init_mux);
    if (!s_dma_sem) {
        s_dma_sem = xSemaphoreCreateBinary();
        if (s_dma_sem) xSemaphoreGive(s_dma_sem);
    }
    portEXIT_CRITICAL(&s_init_mux);
}

// KDF: SHA-256(shared_secret || KDF_LABEL) → first 16 bytes become the session key.
#define KDF_LABEL     "split_v1"
#define KDF_LABEL_LEN (sizeof(KDF_LABEL) - 1)

static void build_nonce(uint16_t seq, uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE])
{
    nonce[0] = (uint8_t)(seq & 0xFF);
    nonce[1] = (uint8_t)(seq >> 8);
    memset(nonce + 2, 0, SPLIT_CRYPTO_NONCE_SIZE - 2);
}

/* =========================================================================
 * ECDH (X25519)
 * ========================================================================= */

esp_err_t split_crypto_ecdh_start(split_crypto_ecdh_t *out_handle)
{
    if (!out_handle) return ESP_ERR_INVALID_ARG;
    ensure_psa_init();

    split_ecdh_context_t *ctx = malloc(sizeof(split_ecdh_context_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 255);

    psa_status_t status = psa_generate_key(&attributes, &ctx->key_id);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_generate_key: %d", (int)status);
        free(ctx);
        return ESP_FAIL;
    }

    *out_handle = ctx;
    return ESP_OK;
}

esp_err_t split_crypto_ecdh_get_public(split_crypto_ecdh_t handle,
                                        uint8_t out_pub[SPLIT_CRYPTO_PUBKEY_SIZE])
{
    if (!handle || !out_pub) return ESP_ERR_INVALID_ARG;
    split_ecdh_context_t *ctx = (split_ecdh_context_t *)handle;

    size_t out_len = 0;
    psa_status_t status = psa_export_public_key(ctx->key_id, out_pub, SPLIT_CRYPTO_PUBKEY_SIZE, &out_len);
    if (status != PSA_SUCCESS || out_len != SPLIT_CRYPTO_PUBKEY_SIZE) {
        ESP_LOGE(TAG, "psa_export_public_key: %d", (int)status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t split_crypto_ecdh_derive_key(split_crypto_ecdh_t handle,
                                        const uint8_t peer_pub[SPLIT_CRYPTO_PUBKEY_SIZE],
                                        uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE])
{
    if (!handle || !peer_pub || !out_key) return ESP_ERR_INVALID_ARG;
    split_ecdh_context_t *ctx = (split_ecdh_context_t *)handle;

    uint8_t secret[SPLIT_CRYPTO_PUBKEY_SIZE];
    size_t  secret_len = 0;

    psa_status_t status = psa_raw_key_agreement(PSA_ALG_ECDH, ctx->key_id,
                                                peer_pub, SPLIT_CRYPTO_PUBKEY_SIZE,
                                                secret, sizeof(secret), &secret_len);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_raw_key_agreement: %d", (int)status);
        return ESP_FAIL;
    }

    // KDF: SHA-256(secret || KDF_LABEL) → first 16 bytes become the AES-128 session key.
    uint8_t kdf_input[SPLIT_CRYPTO_PUBKEY_SIZE + KDF_LABEL_LEN];
    memcpy(kdf_input,                         secret,    SPLIT_CRYPTO_PUBKEY_SIZE);
    memcpy(kdf_input + SPLIT_CRYPTO_PUBKEY_SIZE, KDF_LABEL, KDF_LABEL_LEN);

    uint8_t digest[32];
    size_t digest_len = 0;
    status = psa_hash_compute(PSA_ALG_SHA_256, kdf_input, sizeof(kdf_input),
                              digest, sizeof(digest), &digest_len);

    // Zero sensitive material immediately.
    memset(secret,    0, sizeof(secret));
    memset(kdf_input, 0, sizeof(kdf_input));

    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_hash_compute: %d", (int)status);
        return ESP_FAIL;
    }

    memcpy(out_key, digest, SPLIT_CRYPTO_KEY_SIZE);
    return ESP_OK;
}

void split_crypto_ecdh_free(split_crypto_ecdh_t handle)
{
    if (!handle) return;
    split_ecdh_context_t *ctx = (split_ecdh_context_t *)handle;
    psa_destroy_key(ctx->key_id);
    free(ctx);
}

/* =========================================================================
 * AES-128-CCM
 * ========================================================================= */

esp_err_t split_crypto_encrypt(const uint8_t key[SPLIT_CRYPTO_KEY_SIZE],
                                uint16_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                uint8_t out_mic[SPLIT_CRYPTO_MIC_SIZE])
{
    if (!key || !out_mic) return ESP_ERR_INVALID_ARG;
    if (len > CRYPTO_BUF_MAX) return ESP_ERR_INVALID_SIZE;

    ensure_psa_init();
    ensure_dma_sem();
    if (!s_dma_sem) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "encrypt: timeout");
        return ESP_ERR_TIMEOUT;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, SPLIT_CRYPTO_MIC_SIZE));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);

    psa_key_id_t key_id;
    psa_status_t status = psa_import_key(&attributes, key, SPLIT_CRYPTO_KEY_SIZE, &key_id);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key: %d", (int)status);
        xSemaphoreGive(s_dma_sem);
        return ESP_FAIL;
    }

    uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE];
    build_nonce(seq, nonce);

    size_t out_len = 0;
    // PSA AEAD appends the tag to the ciphertext. We use a bounce buffer.
    status = psa_aead_encrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, SPLIT_CRYPTO_MIC_SIZE),
                              nonce, SPLIT_CRYPTO_NONCE_SIZE,
                              aad, aad_len,
                              buf, len,
                              s_dma_buf, sizeof(s_dma_buf), &out_len);

    psa_destroy_key(key_id);

    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_aead_encrypt: %d", (int)status);
        xSemaphoreGive(s_dma_sem);
        return ESP_FAIL;
    }

    // s_dma_buf contains [ciphertext (len bytes) | tag (4 bytes)]
    if (len > 0) memcpy(buf, s_dma_buf, len);
    memcpy(out_mic, s_dma_buf + len, SPLIT_CRYPTO_MIC_SIZE);
    memset(s_dma_buf, 0, out_len);

    xSemaphoreGive(s_dma_sem);
    return ESP_OK;
}

esp_err_t split_crypto_decrypt(const uint8_t key[SPLIT_CRYPTO_KEY_SIZE],
                                uint16_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                const uint8_t mic[SPLIT_CRYPTO_MIC_SIZE])
{
    if (!key || !mic) return ESP_ERR_INVALID_ARG;
    if (len > CRYPTO_BUF_MAX) return ESP_ERR_INVALID_SIZE;

    ensure_psa_init();
    ensure_dma_sem();
    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, SPLIT_CRYPTO_MIC_SIZE));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);

    psa_key_id_t key_id;
    psa_status_t status = psa_import_key(&attributes, key, SPLIT_CRYPTO_KEY_SIZE, &key_id);
    if (status != PSA_SUCCESS) {
        xSemaphoreGive(s_dma_sem);
        return ESP_FAIL;
    }

    uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE];
    build_nonce(seq, nonce);

    // Prepare combined buffer [ciphertext | tag] for PSA
    if (len > 0) memcpy(s_dma_buf, buf, len);
    memcpy(s_dma_buf + len, mic, SPLIT_CRYPTO_MIC_SIZE);

    size_t out_len = 0;
    status = psa_aead_decrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, SPLIT_CRYPTO_MIC_SIZE),
                              nonce, SPLIT_CRYPTO_NONCE_SIZE,
                              aad, aad_len,
                              s_dma_buf, len + SPLIT_CRYPTO_MIC_SIZE,
                              buf, len, &out_len);

    psa_destroy_key(key_id);
    memset(s_dma_buf, 0, len + SPLIT_CRYPTO_MIC_SIZE);
    xSemaphoreGive(s_dma_sem);

    if (status != PSA_SUCCESS) {
        ESP_LOGW(TAG, "psa_aead_decrypt: %d", (int)status);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t split_crypto_derive_session_key(const uint8_t stored_key[SPLIT_CRYPTO_KEY_SIZE],
                                           const uint8_t nonce_a[SPLIT_CRYPTO_KEY_SIZE],
                                           const uint8_t nonce_b[SPLIT_CRYPTO_KEY_SIZE],
                                           uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE])
{
    if (!stored_key || !nonce_a || !nonce_b || !out_key) return ESP_ERR_INVALID_ARG;
    ensure_psa_init();

    uint8_t kdf_input[SPLIT_CRYPTO_KEY_SIZE * 2];
    memcpy(kdf_input, stored_key, SPLIT_CRYPTO_KEY_SIZE);
    for (int i = 0; i < SPLIT_CRYPTO_KEY_SIZE; i++) {
        kdf_input[SPLIT_CRYPTO_KEY_SIZE + i] = nonce_a[i] ^ nonce_b[i];
    }

    uint8_t digest[32];
    size_t digest_len = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, kdf_input, sizeof(kdf_input),
                                           digest, sizeof(digest), &digest_len);
    memset(kdf_input, 0, sizeof(kdf_input));

    if (status != PSA_SUCCESS) return ESP_FAIL;

    memcpy(out_key, digest, SPLIT_CRYPTO_KEY_SIZE);
    return ESP_OK;
}
