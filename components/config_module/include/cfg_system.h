#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>


typedef struct {
  char device_name[32];
  uint32_t sleep_timeout_ms;
  uint8_t rgb_brightness;
  bool bluetooth_enabled;
  // Split identity
  bool is_split;
  bool split_mirror_cols;    // when true, column N maps to (COL_COUNT-1-N) — for mirrored right halves
  char split_variant[16];    // e.g. "Left", "Right", "Numpad"
  // Shared BLE identity for split keyboards — configure the same values on both halves
  // so they can seamlessly hand off BLE connections when roles swap.
  char    ble_shared_name[32]; // BLE advertised name override (empty = use device_name)
  uint8_t ble_shared_addr[6];  // Shared static random BLE address base (all-zero = auto-derive)
  
  // Layer switching fallback behavior
  bool transparent_stack_fallback;
} cfg_system_t;

typedef struct __attribute__((packed)) {
    uint8_t  sys_cmd;       // e.g. SYS_CMD_INJECT_KEY (0x01) or SYS_CMD_TRIGGER_ACTION
    uint8_t  row;           // For INJECT_KEY
    uint8_t  col;           // For INJECT_KEY
    uint8_t  state;         // For INJECT_KEY (1=press, 0=release)
    uint8_t  reserved[1];   // Pad to offset 5 so action_code is naturally aligned
    uint16_t action_code;   // Starts at struct offset 5 (absolute offset 6) -> ALIGNED!
} sysmod_msg_t;

// Registers the system serializer with cfgmod
void cfg_system_register(void);

// Helper to get system config with caching
esp_err_t cfg_system_get(cfg_system_t *out_sys);
// Helper to set system config via API
esp_err_t cfg_system_set(const cfg_system_t *in_sys);
