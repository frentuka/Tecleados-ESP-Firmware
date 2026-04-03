/**
 * Minimal ECDH mock for host tests.
 *
 * Generates a deterministic "public key" = counter bytes so both sides
 * produce predictable shared secrets for testing round-trip behaviour.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ecp.h"

typedef struct {
    int    grp_id;
    uint8_t priv[32];   /* our private key bytes */
    uint8_t peer[32];   /* peer's public key */
    int    have_peer;
} mbedtls_ecdh_context;

static inline void mbedtls_ecdh_init(mbedtls_ecdh_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
static inline void mbedtls_ecdh_free(mbedtls_ecdh_context *ctx) { (void)ctx; }

static inline int mbedtls_ecdh_setup(mbedtls_ecdh_context *ctx, int grp_id) {
    ctx->grp_id = grp_id;
    return 0;
}

/* Produce public key = {0x01, 0x02, ..., 0x20} as a deterministic value.
 * Output format: [1-byte-len=32][32 bytes] — matching what split_crypto.c expects. */
static inline int mbedtls_ecdh_make_public(mbedtls_ecdh_context *ctx,
                                            size_t *olen,
                                            unsigned char *buf, size_t blen,
                                            int (*f_rng)(void*, unsigned char*, size_t),
                                            void *p_rng)
{
    (void)f_rng; (void)p_rng; (void)blen;
    buf[0] = 32; /* length prefix */
    for (int i = 0; i < 32; i++) {
        ctx->priv[i] = (uint8_t)(ctx->grp_id + i + 1);
        buf[1 + i]   = (uint8_t)(ctx->grp_id + i + 1);
    }
    *olen = 33;
    return 0;
}

static inline int mbedtls_ecdh_read_public(mbedtls_ecdh_context *ctx,
                                            const unsigned char *buf, size_t blen)
{
    if (blen < 33 || buf[0] != 32) return -1;
    memcpy(ctx->peer, buf + 1, 32);
    ctx->have_peer = 1;
    return 0;
}

/* Shared secret = XOR of our private key and peer public key.
 * Both sides will arrive at the same shared secret if they exchanged each other's keys. */
static inline int mbedtls_ecdh_calc_secret(mbedtls_ecdh_context *ctx,
                                            size_t *olen,
                                            unsigned char *buf, size_t blen,
                                            int (*f_rng)(void*, unsigned char*, size_t),
                                            void *p_rng)
{
    (void)blen; (void)f_rng; (void)p_rng;
    for (int i = 0; i < 32; i++) {
        buf[i] = (uint8_t)(ctx->priv[i] ^ ctx->peer[i]);
    }
    *olen = 32;
    return 0;
}
