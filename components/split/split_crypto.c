#include "split_crypto.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#include "psa/crypto.h"

#define TAG "SPLIT_CR"

/* Maximum ESP-NOW payload that will ever be encrypted/decrypted:
 * SPLIT_ESP_NOW_MAX(250) - SPLIT_FRAME_OVERHEAD(10) = 240 bytes. */
#define CRYPTO_BUF_MAX 244

/* Total DMA Workspace (512 bytes)
 * Hardware engines (PSA/AES) on ESP32-S3 cannot access stack memory. 
 * We use a dedicated, globally-addressable Dram region split into two halves 
 * to ensure input and output NEVER overlap and NEVER touch the stack. */
#define CRYPTO_DMA_WORK_SIZE  512
static uint8_t           s_dma_work[CRYPTO_DMA_WORK_SIZE] __attribute__((aligned(4)));
#define DMA_IN               (s_dma_work + 0)
#define DMA_OUT              (s_dma_work + 256)

static uint8_t s_dma_aad[32] __attribute__((aligned(4))); // DMA-safe AAD buffer (≥ SPLIT_FRAME_HEADER_SIZE = 10)

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

// KDF: SHA-256(shared_secret || KDF_LABEL) -> first 16 bytes become the session key.
#define KDF_LABEL     "split_v1"
#define KDF_LABEL_LEN (sizeof(KDF_LABEL) - 1)

static void build_nonce(uint64_t seq, uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE])
{
    // NIST CCM requires a nonce of 7-13 bytes. We use 13 bytes.
    // CRITICAL: Pre-zero the buffer to ensure bytes 6-12 are stable.
    memset(nonce, 0, SPLIT_CRYPTO_NONCE_SIZE);

    for (int i = 0; i < 6; i++) {
        nonce[i] = (uint8_t)((seq >> (i * 8)) & 0xFF);
    }
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

    ensure_dma_sem();
    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    size_t out_len = 0;
    psa_status_t status = psa_export_public_key(ctx->key_id, DMA_OUT, SPLIT_CRYPTO_PUBKEY_SIZE, &out_len);
    
    if (status == PSA_SUCCESS && out_len == SPLIT_CRYPTO_PUBKEY_SIZE) {
        memcpy(out_pub, DMA_OUT, SPLIT_CRYPTO_PUBKEY_SIZE);
    }
    
    xSemaphoreGive(s_dma_sem);

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

    ensure_dma_sem();
    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    // Use DMA_IN for peer public key
    memcpy(DMA_IN, peer_pub, SPLIT_CRYPTO_PUBKEY_SIZE);

    size_t  secret_len = 0;
    psa_status_t status = psa_raw_key_agreement(PSA_ALG_ECDH, ctx->key_id,
                                                DMA_IN, SPLIT_CRYPTO_PUBKEY_SIZE,
                                                DMA_OUT, 32, &secret_len);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_raw_key_agreement: %d", (int)status);
        xSemaphoreGive(s_dma_sem);
        return ESP_FAIL;
    }

    // Now build KDF input: SHA-256(secret || label)
    // secret is in DMA_OUT[0..31]
    memcpy(DMA_IN,                            DMA_OUT, SPLIT_CRYPTO_PUBKEY_SIZE);
    memcpy(DMA_IN + SPLIT_CRYPTO_PUBKEY_SIZE, KDF_LABEL, KDF_LABEL_LEN);

    size_t digest_len = 0;
    status = psa_hash_compute(PSA_ALG_SHA_256, DMA_IN, SPLIT_CRYPTO_PUBKEY_SIZE + KDF_LABEL_LEN,
                               DMA_OUT, 32, &digest_len);

    if (status == PSA_SUCCESS) {
        memcpy(out_key, DMA_OUT, SPLIT_CRYPTO_KEY_SIZE);
    }

    memset(s_dma_work, 0, CRYPTO_DMA_WORK_SIZE);
    xSemaphoreGive(s_dma_sem);

    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "ecdh_derive hash: %d", (int)status);
        return ESP_FAIL;
    }

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
                                uint64_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                uint8_t out_mic[SPLIT_CRYPTO_MIC_SIZE])
{
    if (!key || !out_mic) return ESP_ERR_INVALID_ARG;
    if (len > CRYPTO_BUF_MAX) return ESP_ERR_INVALID_SIZE;

    ensure_psa_init();
    ensure_dma_sem();
    if (!s_dma_sem) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
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

    // Paranoia guard: if aad_len ever exceeds the DMA buffer the MIC computed
    // by encrypt and decrypt will differ silently. Fail loudly instead.
    assert(aad_len <= sizeof(s_dma_aad));
    memset(s_dma_aad, 0, sizeof(s_dma_aad));
    if (aad && aad_len > 0) {
        memcpy(s_dma_aad, aad, aad_len);
    }

    size_t out_len = 0;
    if (len > 0) memcpy(DMA_IN, buf, len);

    // Hardware DMA Isolation: No stack access.
    status = psa_aead_encrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, SPLIT_CRYPTO_MIC_SIZE),
                               nonce, SPLIT_CRYPTO_NONCE_SIZE,
                               s_dma_aad, aad_len,
                               DMA_IN, len,
                               DMA_OUT, 256, &out_len);

    psa_destroy_key(key_id);

    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_aead_encrypt: %d", (int)status);
        xSemaphoreGive(s_dma_sem);
        return ESP_FAIL;
    }

    if (len > 0) memcpy(buf, DMA_OUT, len);
    memcpy(out_mic, DMA_OUT + len, SPLIT_CRYPTO_MIC_SIZE);
    
    memset(s_dma_work, 0, CRYPTO_DMA_WORK_SIZE);
    xSemaphoreGive(s_dma_sem);
    return ESP_OK;
}

esp_err_t split_crypto_decrypt(const uint8_t key[SPLIT_CRYPTO_KEY_SIZE],
                                uint64_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                const uint8_t mic[SPLIT_CRYPTO_MIC_SIZE])
{
    if (!key || !mic) return ESP_ERR_INVALID_ARG;
    if (len > CRYPTO_BUF_MAX) return ESP_ERR_INVALID_SIZE;

    ensure_psa_init();
    ensure_dma_sem();
    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

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

    // Paranoia guard: same constraint as the encrypt path.
    assert(aad_len <= sizeof(s_dma_aad));
    memset(s_dma_aad, 0, sizeof(s_dma_aad));
    if (aad && aad_len > 0) {
        memcpy(s_dma_aad, aad, aad_len);
    }

    // Prepare combined [ciphertext | tag] in DMA_IN
    if (len > 0) memcpy(DMA_IN, buf, len);
    memcpy(DMA_IN + len, mic, SPLIT_CRYPTO_MIC_SIZE);


    size_t out_len = 0;
    // Decrypt directly into DMA_OUT (DMA_OUT must NOT be stack memory)
    status = psa_aead_decrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, SPLIT_CRYPTO_MIC_SIZE),
                               nonce, SPLIT_CRYPTO_NONCE_SIZE,
                               s_dma_aad, aad_len,
                               DMA_IN, len + SPLIT_CRYPTO_MIC_SIZE,
                               DMA_OUT, 256, &out_len);

    psa_destroy_key(key_id);
    
    if (status == PSA_SUCCESS) {
        if (len > 0) memcpy(buf, DMA_OUT, len);
    }

    memset(s_dma_work, 0, CRYPTO_DMA_WORK_SIZE);
    xSemaphoreGive(s_dma_sem);

    if (status != PSA_SUCCESS) {
        ESP_LOGD(TAG, "psa_aead_decrypt: %d (key=%02X.. nonce=%02X..)",
                 (int)status, key[0], nonce[0]);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t split_crypto_derive_session_key(const uint8_t stored_key[SPLIT_CRYPTO_KEY_SIZE],
                                           uint32_t salt_own, uint32_t salt_peer,
                                           uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE])
{
    if (!stored_key || !out_key) return ESP_ERR_INVALID_ARG;
    ensure_psa_init();
    
    ensure_dma_sem();
    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    uint32_t s1 = (salt_own < salt_peer) ? salt_own  : salt_peer;
    uint32_t s2 = (salt_own < salt_peer) ? salt_peer : salt_own;

    // kdf_input: stored_key(16) || s1(4) || s2(4) = 24 bytes
    memcpy(DMA_IN, stored_key, SPLIT_CRYPTO_KEY_SIZE);
    memcpy(DMA_IN + SPLIT_CRYPTO_KEY_SIZE,     &s1, 4);
    memcpy(DMA_IN + SPLIT_CRYPTO_KEY_SIZE + 4, &s2, 4);

    size_t digest_len = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, DMA_IN, SPLIT_CRYPTO_KEY_SIZE + 8,
                                           DMA_OUT, 32, &digest_len);
    
    if (status == PSA_SUCCESS) {
        memcpy(out_key, DMA_OUT, SPLIT_CRYPTO_KEY_SIZE);
    }

    memset(s_dma_work, 0, CRYPTO_DMA_WORK_SIZE);
    xSemaphoreGive(s_dma_sem);

    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "session key KDF failed: %d", (int)status);
        return ESP_FAIL;
    }

    return ESP_OK;
}
