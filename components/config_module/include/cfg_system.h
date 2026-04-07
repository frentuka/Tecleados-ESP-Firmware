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
  int8_t split_col_offset;   // added to raw col when is_split is true
  char split_variant[16];    // e.g. "Left", "Right", "Numpad"
  // Shared BLE identity for split keyboards — configure the same values on both halves
  // so they can seamlessly hand off BLE connections when roles swap.
  char    ble_shared_name[32]; // BLE advertised name override (empty = use device_name)
  uint8_t ble_shared_addr[6];  // Shared static random BLE address base (all-zero = auto-derive)
} cfg_system_t;

// Registers the system serializer with cfgmod
void cfg_system_register(void);

// Helper to get system config with caching
esp_err_t cfg_system_get(cfg_system_t *out_sys);
// Helper to set system config via API
esp_err_t cfg_system_set(const cfg_system_t *in_sys);
