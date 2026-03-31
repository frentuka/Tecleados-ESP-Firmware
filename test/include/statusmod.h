/**
 * @file statusmod.h
 * @brief Shim — status module declarations for host testing.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "test_harness.h"

PACKED_STRUCT_BEGIN
typedef struct PACKED_ATTR {
    uint8_t transport_mode;
    uint8_t selected_profile;
    uint8_t pairing_profile;
    uint16_t connected_profiles_bitmap;
} status_msg_t;
PACKED_STRUCT_END

void status_module_init(void);
