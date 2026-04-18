/**
 * Minimal SHA-256 mock for host tests.
 *
 * Implements the real SHA-256 algorithm so the KDF test vector is
 * deterministic and verifiable against a reference value.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---- SHA-256 implementation ---- */

static const uint32_t _sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define _SHA256_ROR32(x,n) (((x)>>(n)) | ((x)<<(32-(n))))
#define _SHA256_CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define _SHA256_MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define _SHA256_S0(x) (_SHA256_ROR32(x,2)^_SHA256_ROR32(x,13)^_SHA256_ROR32(x,22))
#define _SHA256_S1(x) (_SHA256_ROR32(x,6)^_SHA256_ROR32(x,11)^_SHA256_ROR32(x,25))
#define _SHA256_G0(x) (_SHA256_ROR32(x,7)^_SHA256_ROR32(x,18)^((x)>>3))
#define _SHA256_G1(x) (_SHA256_ROR32(x,17)^_SHA256_ROR32(x,19)^((x)>>10))

static inline int mbedtls_sha256(const unsigned char *input, size_t ilen,
                                   unsigned char output[32], int is224)
{
    (void)is224;
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
    };

    /* Pad */
    size_t padded = ((ilen + 9 + 63) / 64) * 64;
    uint8_t *msg = (uint8_t *)calloc(padded, 1);
    if (!msg) return -1;
    memcpy(msg, input, ilen);
    msg[ilen] = 0x80;
    uint64_t bitlen = (uint64_t)ilen * 8;
    for (int i = 0; i < 8; i++)
        msg[padded - 1 - i] = (uint8_t)(bitlen >> (i * 8));

    /* Process blocks */
    for (size_t block = 0; block < padded; block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i]  = ((uint32_t)msg[block + i*4    ] << 24)
                  | ((uint32_t)msg[block + i*4 + 1] << 16)
                  | ((uint32_t)msg[block + i*4 + 2] <<  8)
                  | ((uint32_t)msg[block + i*4 + 3]);
        }
        for (int i = 16; i < 64; i++)
            w[i] = _SHA256_G1(w[i-2]) + w[i-7] + _SHA256_G0(w[i-15]) + w[i-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + _SHA256_S1(e) + _SHA256_CH(e,f,g) + _sha256_k[i] + w[i];
            uint32_t t2 = _SHA256_S0(a) + _SHA256_MAJ(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(msg);

    for (int i = 0; i < 8; i++) {
        output[i*4  ] = (uint8_t)(h[i] >> 24);
        output[i*4+1] = (uint8_t)(h[i] >> 16);
        output[i*4+2] = (uint8_t)(h[i] >>  8);
        output[i*4+3] = (uint8_t)(h[i]);
    }
    return 0;
}
