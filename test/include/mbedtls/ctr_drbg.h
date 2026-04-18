#pragma once
#include <stddef.h>
#include "entropy.h"

typedef struct { int counter; } mbedtls_ctr_drbg_context;
static inline void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *ctx) { ctx->counter = 0; }
static inline void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *ctx) { (void)ctx; }
static inline int  mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *ctx,
                                          int (*f_entropy)(void*, unsigned char*, size_t),
                                          void *p_entropy,
                                          const unsigned char *custom, size_t len) {
    (void)f_entropy; (void)p_entropy; (void)custom; (void)len;
    ctx->counter = 0;
    return 0;
}
static inline int mbedtls_ctr_drbg_random(void *ctx, unsigned char *output, size_t len) {
    mbedtls_ctr_drbg_context *c = (mbedtls_ctr_drbg_context *)ctx;
    for (size_t i = 0; i < len; i++) output[i] = (unsigned char)(c->counter++ & 0xFF);
    return 0;
}
