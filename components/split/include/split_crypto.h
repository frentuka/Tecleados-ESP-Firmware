#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SPLIT_CRYPTO_KEY_SIZE    16   // AES-128 key (bytes)
#define SPLIT_CRYPTO_MIC_SIZE     4   // CCM truncated tag (bytes)
#define SPLIT_CRYPTO_PUBKEY_SIZE 32   // X25519 public-key / x-coordinate (bytes)
#define SPLIT_CRYPTO_NONCE_SIZE  13   // CCM nonce (bytes)

/* =========================================================================
 * ECDH (X25519) — opaque handle, heap-allocated
 *
 * Typical initiator flow:
 *   split_crypto_ecdh_start(&h)
 *   split_crypto_ecdh_get_public(h, our_pub)   ← put our_pub in PAIR_REQUEST
 *   ... wait for PAIR_RESPONSE with peer_pub ...
 *   split_crypto_ecdh_derive_key(h, peer_pub, out_key)
 *   split_crypto_ecdh_free(h)
 *
 * Typical responder flow (all in one callback):
 *   split_crypto_ecdh_start(&h)
 *   split_crypto_ecdh_get_public(h, our_pub)   ← put our_pub in PAIR_RESPONSE
 *   split_crypto_ecdh_derive_key(h, peer_pub, out_key)
 *   split_crypto_ecdh_free(h)
 * ========================================================================= */

typedef void *split_crypto_ecdh_t;

/**
 * @brief Allocate and initialise an X25519 ECDH session.
 */
esp_err_t split_crypto_ecdh_start(split_crypto_ecdh_t *out_handle);

/**
 * @brief Generate our ephemeral keypair and return the 32-byte public key.
 *        Must be called exactly once per handle before derive_key.
 */
esp_err_t split_crypto_ecdh_get_public(split_crypto_ecdh_t handle,
                                        uint8_t out_pub[SPLIT_CRYPTO_PUBKEY_SIZE]);

/**
 * @brief Compute the shared secret from the peer's 32-byte public key and
 *        derive a 16-byte AES-128 session key via SHA-256.
 *        Requires get_public to have been called first.
 */
esp_err_t split_crypto_ecdh_derive_key(split_crypto_ecdh_t handle,
                                        const uint8_t peer_pub[SPLIT_CRYPTO_PUBKEY_SIZE],
                                        uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE]);

/**
 * @brief Free the ECDH session and zero sensitive key material.
 */
void split_crypto_ecdh_free(split_crypto_ecdh_t handle);

/* =========================================================================
 * AES-128-CCM symmetric encryption
 *
 * Frame header (6 bytes) is used as AAD: authenticated but not encrypted.
 * Nonce is derived from the sequence number.
 * Plaintext messages (during pairing): pass key=NULL to skip, mic will be zeroed.
 * ========================================================================= */

/**
 * @brief Encrypt payload in-place and produce a 4-byte MIC.
 *
 * @param key       AES-128 key (16 bytes)
 * @param seq       Frame sequence number (nonce component)
 * @param aad       Frame header bytes (authenticated, not encrypted)
 * @param aad_len   AAD length
 * @param buf       Payload buffer (encrypted in-place)
 * @param len       Payload length
 * @param out_mic   Output: 4-byte authentication tag
 */
esp_err_t split_crypto_encrypt(const uint8_t key[SPLIT_CRYPTO_KEY_SIZE],
                                uint16_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                uint8_t out_mic[SPLIT_CRYPTO_MIC_SIZE]);

/**
 * @brief Decrypt payload in-place and verify the 4-byte MIC.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_CRC on authentication failure.
 */
esp_err_t split_crypto_decrypt(const uint8_t key[SPLIT_CRYPTO_KEY_SIZE],
                                uint16_t seq,
                                const uint8_t *aad, size_t aad_len,
                                uint8_t *buf, size_t len,
                                const uint8_t mic[SPLIT_CRYPTO_MIC_SIZE]);

/**
 * @brief Derive a per-session AES-128 key from the long-term stored key and
 *        two ephemeral nonces (one from each side).
 *
 * Formula: SHA-256(stored_key || nonce_a XOR nonce_b) → first 16 bytes.
 *
 * XOR makes the result symmetric — both sides arrive at the same key regardless
 * of which nonce is "ours" and which is the "peer's".  A fresh nonce from at
 * least one side guarantees a new key every session, providing per-session
 * forward secrecy without re-pairing.
 *
 * @param stored_key  Long-term key from NVS (SPLIT_CRYPTO_KEY_SIZE bytes)
 * @param nonce_a     First  ephemeral nonce  (SPLIT_CRYPTO_KEY_SIZE bytes)
 * @param nonce_b     Second ephemeral nonce  (SPLIT_CRYPTO_KEY_SIZE bytes)
 * @param out_key     Derived session key output (SPLIT_CRYPTO_KEY_SIZE bytes)
 */
esp_err_t split_crypto_derive_session_key(const uint8_t stored_key[SPLIT_CRYPTO_KEY_SIZE],
                                           const uint8_t nonce_a[SPLIT_CRYPTO_KEY_SIZE],
                                           const uint8_t nonce_b[SPLIT_CRYPTO_KEY_SIZE],
                                           uint8_t out_key[SPLIT_CRYPTO_KEY_SIZE]);
