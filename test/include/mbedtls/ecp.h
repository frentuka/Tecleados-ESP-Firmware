#pragma once
#include <stddef.h>
#include <stdint.h>

#define MBEDTLS_ECP_DP_CURVE25519 30

typedef struct { int grp_id; } mbedtls_ecp_group;
typedef struct { uint8_t p[32]; } mbedtls_ecp_point;
typedef struct { uint8_t p[32]; } mbedtls_mpi;
