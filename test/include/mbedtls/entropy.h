#pragma once
#include <stddef.h>

typedef struct { int dummy; } mbedtls_entropy_context;
static inline void mbedtls_entropy_init(mbedtls_entropy_context *ctx) { ctx->dummy = 0; }
static inline void mbedtls_entropy_free(mbedtls_entropy_context *ctx) { (void)ctx; }
static inline int  mbedtls_entropy_func(void *ctx, unsigned char *output, size_t len) {
    (void)ctx;
    /* Deterministic "entropy" — incrementing counter */
    for (size_t i = 0; i < len; i++) output[i] = (unsigned char)(i + 1);
    return 0;
}
