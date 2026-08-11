#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    uint8_t  transport_mode;
    uint8_t  selected_profile;
    uint8_t  pairing_profile;
    uint8_t  split_state;
    uint8_t  split_role;
    uint8_t  reserved[2];      // Pad so the next field is at struct offset 7. (1+7 = 8 aligned!)
    uint16_t connected_bitmap; // Starts at struct offset 7. In s_rx_buf (starts at offset 1), it is at 1+7 = 8 (Aligned!)
} statusmod_msg_t;

void status_module_init(void);
