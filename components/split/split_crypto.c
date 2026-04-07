#include "split_crypto.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ccm.h"

#define TAG "SPLIT_CR"

/* Maximum ESP-NOW payload that will ever be encrypted/decrypted:
 * SPLIT_ESP_NOW_MAX(250) - SPLIT_FRAME_OVERHEAD(10) = 240 bytes.
 * Defined locally so split_crypto.c doesn't depend on split_protocol.h. */
#define CRYPTO_BUF_MAX 240

/* Single static DMA-capable bounce buffer shared by encrypt and decrypt.
 * Protected by a binary semaphore so concurrent calls from the WiFi task and
 * split_task cannot race.  Eliminates all per-call DMA heap allocations —
 * avoids fragmentation failures when internal SRAM is tight (BLE + WiFi). */
static uint8_t           s_dma_buf[CRYPTO_BUF_MAX];
static SemaphoreHandle_t s_dma_sem = NULL;
static portMUX_TYPE      s_init_mux = portMUX_INITIALIZER_UNLOCKED;

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

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

// Build a 13-byte CCM nonce from the 16-bit sequence number.
static void build_nonce(uint16_t seq, uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE])
{
    nonce[0] = (uint8_t)(seq & 0xFF);
    nonce[1] = (uint8_t)(seq >> 8);
    memset(nonce + 2, 0, SPLIT_CRYPTO_NONCE_SIZE - 2);
}

// Allocate and seed an RNG (caller must free both pointers via rng_free).
static esp_err_t rng_alloc(mbedtls_entropy_context **out_e,
                            mbedtls_ctr_drbg_context **out_d)
{
    *out_e = malloc(sizeof(mbedtls_entropy_context));
    *out_d = malloc(sizeof(mbedtls_ctr_drbg_context));
    if (!*out_e || !*out_d) {
        free(*out_e);
        free(*out_d);
        *out_e = NULL;
        *out_d = NULL;
        return ESP_ERR_NO_MEM;
    }
    mbedtls_entropy_init(*out_e);
    mbedtls_ctr_drbg_init(*out_d);
    int rc = mbedtls_ctr_drbg_seed(*out_d, mbedtls_entropy_func, *out_e,
                                   (const unsigned char *)"split", 5);
    if (rc != 0) {
        mbedtls_entropy_free(*out_e);
        mbedtls_ctr_drbg_free(*out_d);
        free(*out_e);
        free(*out_d);
        *out_e = NULL;
        *out_d = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void rng_free(mbedtls_entropy_context *e, mbedtls_ctr_drbg_context *d)
{
    if (e) { mbedtls_entropy_free(e);   free(e); }
    if (d) { mbedtls_ctr_drbg_free(d); free(d); }
}

/* =========================================================================
 * ECDH (X25519)
 * ========================================================================= */

esp_err_t split_crypto_ecdh_start(split_crypto_ecdh_t *out_handle)
{
    if (!out_handle) return ESP_ERR_INVALID_ARG;

    mbedtls_ecdh_context *ctx = malloc(sizeof(mbedtls_ecdh_context));
    if (!ctx) return ESP_ERR_NO_MEM;

    mbedtls_ecdh_init(ctx);

    int rc = mbedtls_ecdh_setup(ctx, MBEDTLS_ECP_DP_CURVE25519);
    if (rc != 0) {
        ESP_LOGE(TAG, "ecdh_setup: -0x%04X", (unsigned)(-rc));
        mbedtls_ecdh_free(ctx);
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
    mbedtls_ecdh_context *ctx = (mbedtls_ecdh_context *)handle;

    mbedtls_entropy_context *e;
    mbedtls_ctr_drbg_context *d;
    esp_err_t ret = rng_alloc(&e, &d);
    if (ret != ESP_OK) return ret;

    // mbedtls encodes X25519 public key as [1-byte length prefix | 32-byte x-coordinate].
    // Allocate with a small safety margin over the known 33-byte output.
    uint8_t pub_buf[1 + SPLIT_CRYPTO_PUBKEY_SIZE + 3];
    size_t  pub_len = 0;
    int rc = mbedtls_ecdh_make_public(ctx, &pub_len, pub_buf, sizeof(pub_buf),
                                      mbedtls_ctr_drbg_random, d);
    rng_free(e, d);

    if (rc != 0) {
        ESP_LOGE(TAG, "ecdh_make_public: -0x%04X", (unsigned)(-rc));
        return ESP_FAIL;
    }

    if (pub_len < 1 + SPLIT_CRYPTO_PUBKEY_SIZE || pub_buf[0] != SPLIT_CRYPTO_PUBKEY_SIZE) {
        ESP_LOGE(TAG, "unexpected pub key encoding (len=%u prefix=%u)", (unsigned)pub_len, pub_buf[0]);
        return ESP_FAIL;
    }

    // Strip the 1-byte length prefix to get the raw 32-byte x-coordinate.
    memcpy(out_pub, pub_buf + 1, SPLIT_CRYPTO_PUBKEY_SIZE);
    return ESP_OK;
}

esp_err_t split_crypto_ecdh_derive_key(split_crypto_ecdh_t handle,
                                        const uint8_t peer_pub[SPLIT_CRYPTO_PUBKEY_SIZE],
                                        uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE])
{
    if (!handle || !peer_pub || !out_key) return ESP_ERR_INVALID_ARG;
    mbedtls_ecdh_context *ctx = (mbedtls_ecdh_context *)handle;

    // Re-encode peer public key with the 1-byte length prefix mbedtls expects.
    uint8_t peer_tls[1 + SPLIT_CRYPTO_PUBKEY_SIZE];
    peer_tls[0] = SPLIT_CRYPTO_PUBKEY_SIZE;
    memcpy(peer_tls + 1, peer_pub, SPLIT_CRYPTO_PUBKEY_SIZE);

    int rc = mbedtls_ecdh_read_public(ctx, peer_tls, sizeof(peer_tls));
    if (rc != 0) {
        ESP_LOGE(TAG, "ecdh_read_public: -0x%04X", (unsigned)(-rc));
        return ESP_FAIL;
    }

    mbedtls_entropy_context *e;
    mbedtls_ctr_drbg_context *d;
    esp_err_t ret = rng_alloc(&e, &d);
    if (ret != ESP_OK) return ret;

    uint8_t secret[SPLIT_CRYPTO_PUBKEY_SIZE];
    size_t  secret_len = 0;
    rc = mbedtls_ecdh_calc_secret(ctx, &secret_len, secret, sizeof(secret),
                                  mbedtls_ctr_drbg_random, d);
    rng_free(e, d);

    if (rc != 0) {
        ESP_LOGE(TAG, "ecdh_calc_secret: -0x%04X", (unsigned)(-rc));
        return ESP_FAIL;
    }

    // KDF: SHA-256(secret || KDF_LABEL) → first 16 bytes become the AES-128 session key.
    uint8_t kdf_input[SPLIT_CRYPTO_PUBKEY_SIZE + KDF_LABEL_LEN];
    memcpy(kdf_input,                         secret,    SPLIT_CRYPTO_PUBKEY_SIZE);
    memcpy(kdf_input + SPLIT_CRYPTO_PUBKEY_SIZE, KDF_LABEL, KDF_LABEL_LEN);

    uint8_t digest[32];
    rc = mbedtls_sha256(kdf_input, sizeof(kdf_input), digest, 0 /* SHA-256 */);

    // Zero sensitive material immediately.
    memset(secret,    0, sizeof(secret));
    memset(kdf_input, 0, sizeof(kdf_input));

    if (rc != 0) {
        ESP_LOGE(TAG, "sha256: -0x%04X", (unsigned)(-rc));
        return ESP_FAIL;
    }

    memcpy(out_key, digest, SPLIT_CRYPTO_KEY_SIZE);
    ESP_LOGD(TAG, "session key derived");
    return ESP_OK;
}

void split_crypto_ecdh_free(split_crypto_ecdh_t handle)
{
    if (!handle) return;
    mbedtls_ecdh_context *ctx = (mbedtls_ecdh_context *)handle;
    mbedtls_ecdh_free(ctx);
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

    ensure_dma_sem();
    if (!s_dma_sem) return ESP_ERR_NO_MEM;

    // Wait up to 50 ms — no legitimate call should take longer than one AES op.
    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "encrypt: timeout waiting for DMA buffer");
        return ESP_ERR_TIMEOUT;
    }

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);

    uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE];
    build_nonce(seq, nonce);

    esp_err_t ret = ESP_OK;
    int rc;

    rc = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc != 0) { ESP_LOGE(TAG, "ccm_setkey: -0x%04X", (unsigned)(-rc)); ret = ESP_FAIL; goto done; }

    rc = mbedtls_ccm_encrypt_and_tag(&ctx, len,
                                     nonce, SPLIT_CRYPTO_NONCE_SIZE,
                                     aad, aad_len,
                                     buf, (len > 0) ? s_dma_buf : NULL,
                                     out_mic, SPLIT_CRYPTO_MIC_SIZE);
    if (rc != 0) {
        ESP_LOGE(TAG, "ccm_encrypt_and_tag: -0x%04X", (unsigned)(-rc));
        ret = ESP_FAIL;
        goto done;
    }
    if (len > 0) {
        memcpy(buf, s_dma_buf, len);
    }

done:
    if (len > 0) memset(s_dma_buf, 0, len);
    mbedtls_ccm_free(&ctx);
    xSemaphoreGive(s_dma_sem);
    return ret;
}

esp_err_t split_crypto_decrypt(const uint8_t key[SPLIT_CRYPTO_KEY_SIZE],
                                uint16_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                const uint8_t mic[SPLIT_CRYPTO_MIC_SIZE])
{
    if (!key || !mic) return ESP_ERR_INVALID_ARG;
    if (len > CRYPTO_BUF_MAX) return ESP_ERR_INVALID_SIZE;

    ensure_dma_sem();
    if (!s_dma_sem) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "decrypt: timeout waiting for DMA buffer");
        return ESP_ERR_TIMEOUT;
    }

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);

    uint8_t nonce[SPLIT_CRYPTO_NONCE_SIZE];
    build_nonce(seq, nonce);

    esp_err_t ret = ESP_OK;
    int rc;

    rc = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc != 0) { ESP_LOGE(TAG, "ccm_setkey: -0x%04X", (unsigned)(-rc)); ret = ESP_FAIL; goto done; }

    rc = mbedtls_ccm_auth_decrypt(&ctx, len,
                                  nonce, SPLIT_CRYPTO_NONCE_SIZE,
                                  aad, aad_len,
                                  buf, (len > 0) ? s_dma_buf : NULL,
                                  mic, SPLIT_CRYPTO_MIC_SIZE);
    if (rc != 0) {
        ESP_LOGW(TAG, "ccm_auth_decrypt: MIC mismatch (-0x%04X)", (unsigned)(-rc));
        ret = ESP_ERR_INVALID_CRC;
        goto done;
    }
    if (len > 0) {
        memcpy(buf, s_dma_buf, len);
    }

done:
    if (len > 0) memset(s_dma_buf, 0, len);
    mbedtls_ccm_free(&ctx);
    xSemaphoreGive(s_dma_sem);
    return ret;
}

/* =========================================================================
 * Per-session key derivation
 * ========================================================================= */

esp_err_t split_crypto_derive_session_key(const uint8_t stored_key[SPLIT_CRYPTO_KEY_SIZE],
                                           const uint8_t nonce_a[SPLIT_CRYPTO_KEY_SIZE],
                                           const uint8_t nonce_b[SPLIT_CRYPTO_KEY_SIZE],
                                           uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE])
{
    if (!stored_key || !nonce_a || !nonce_b || !out_key) return ESP_ERR_INVALID_ARG;

    // kdf_input = stored_key || (nonce_a XOR nonce_b)
    // XOR is commutative — both sides arrive at the same key regardless of which
    // nonce is "ours" and which is the peer's.
    uint8_t kdf_input[SPLIT_CRYPTO_KEY_SIZE * 2];
    memcpy(kdf_input, stored_key, SPLIT_CRYPTO_KEY_SIZE);
    for (int i = 0; i < SPLIT_CRYPTO_KEY_SIZE; i++) {
        kdf_input[SPLIT_CRYPTO_KEY_SIZE + i] = nonce_a[i] ^ nonce_b[i];
    }

    uint8_t digest[32];
    int rc = mbedtls_sha256(kdf_input, sizeof(kdf_input), digest, 0 /* SHA-256 */);
    memset(kdf_input, 0, sizeof(kdf_input));

    if (rc != 0) {
        ESP_LOGE(TAG, "derive_session_key sha256: -0x%04X", (unsigned)(-rc));
        return ESP_FAIL;
    }

    memcpy(out_key, digest, SPLIT_CRYPTO_KEY_SIZE);
    return ESP_OK;
}
