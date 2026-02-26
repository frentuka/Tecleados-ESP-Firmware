#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>


typedef struct {
  char device_name[32];
  uint32_t sleep_timeout_ms;
  uint8_t rgb_brightness;
  bool bluetooth_enabled;
} cfg_system_t;

// Registers the system serializer with cfgmod
void cfg_system_register(void);

// Helper to get system config with caching
esp_err_t cfg_system_get(cfg_system_t *out_sys);
// Helper to set system config via API
esp_err_t cfg_system_set(const cfg_system_t *in_sys);
