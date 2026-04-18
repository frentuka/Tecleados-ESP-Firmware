/**
 * Minimal mbedtls CCM mock for host tests.
 *
 * The "encryption" is a byte-wise XOR with the key (not cryptographically
 * secure, but sufficient to test that encrypt→decrypt is an identity and
 * that MIC mismatch is detected correctly).
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define MBEDTLS_CIPHER_ID_AES 2

typedef struct {
    uint8_t key[16];
    int     keybits;
} mbedtls_ccm_context;

static inline void mbedtls_ccm_init(mbedtls_ccm_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
static inline void mbedtls_ccm_free(mbedtls_ccm_context *ctx) { (void)ctx; }

static inline int mbedtls_ccm_setkey(mbedtls_ccm_context *ctx,
                                      int cipher_id,
                                      const unsigned char *key,
                                      unsigned int keybits)
{
    (void)cipher_id;
    ctx->keybits = (int)keybits;
    memcpy(ctx->key, key, keybits / 8 < 16 ? keybits / 8 : 16);
    return 0;
}

/* XOR-encrypt: output[i] = input[i] ^ key[i % keylen].
 * MIC = XOR of all input bytes, repeated to tag_len. */
static inline int mbedtls_ccm_encrypt_and_tag(mbedtls_ccm_context *ctx,
                                               size_t length,
                                               const unsigned char *iv, size_t iv_len,
                                               const unsigned char *add, size_t add_len,
                                               const unsigned char *input,
                                               unsigned char *output,
                                               unsigned char *tag, size_t tag_len)
{
    (void)iv; (void)iv_len; (void)add; (void)add_len;
    size_t keylen = (size_t)(ctx->keybits / 8);
    uint8_t mic = 0;
    for (size_t i = 0; i < length; i++) {
        output[i] = input[i] ^ ctx->key[i % keylen];
        mic ^= input[i];
    }
    for (size_t i = 0; i < tag_len; i++) {
        tag[i] = mic ^ (uint8_t)i;
    }
    return 0;
}

static inline int mbedtls_ccm_auth_decrypt(mbedtls_ccm_context *ctx,
                                            size_t length,
                                            const unsigned char *iv, size_t iv_len,
                                            const unsigned char *add, size_t add_len,
                                            const unsigned char *input,
                                            unsigned char *output,
                                            const unsigned char *tag, size_t tag_len)
{
    (void)iv; (void)iv_len; (void)add; (void)add_len;
    size_t keylen = (size_t)(ctx->keybits / 8);
    /* Decrypt first (XOR is self-inverse) */
    uint8_t mic = 0;
    for (size_t i = 0; i < length; i++) {
        output[i] = input[i] ^ ctx->key[i % keylen];
        mic ^= output[i];
    }
    /* Verify MIC */
    for (size_t i = 0; i < tag_len; i++) {
        if (tag[i] != (uint8_t)(mic ^ (uint8_t)i)) {
            return -1; /* MBEDTLS_ERR_CCM_AUTH_FAILED */
        }
    }
    return 0;
}
