#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    uint8_t transport_mode;  // 0: USB, 1: BLE
    uint8_t selected_profile;
    uint16_t connected_profiles_bitmap;
} status_msg_t;

void status_module_init(void);
