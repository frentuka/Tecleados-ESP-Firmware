#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"


// Keep NVS keys short (<= 15 chars including null terminator).
#define CFGMOD_MAX_KEY_LEN 12

typedef enum cfgmod_kind : uint8_t {
  CFGMOD_KIND_LAYOUT = 0,
  CFGMOD_KIND_MACRO,
  CFGMOD_KIND_CONNECTION,
  CFGMOD_KIND_SYSTEM,
  CFGMOD_KIND_PHYSICAL,  // Raw blob kind — not registered, uses direct NVS read/write
  CFGMOD_KIND_CKEY,      // Custom Keys
  CFGMOD_KIND_SPLIT,     // Split keyboard pairing data
  CFGMOD_KIND_COMBO,     // Combo definitions
  CFGMOD_KIND_BLE_BOND,  // Bulk serialization of nimble_bond namespace
  CFGMOD_KIND_MAX
} cfgmod_kind_t;

typedef enum cfgmod_key_id : uint8_t {
  CFG_KEY_TEST = 0,
  CFG_KEY_HELLO,
  CFG_KEY_PHYSICAL_LAYOUT,
  CFG_KEY_LAYOUTS = 0x10,
  CFG_KEY_LAYOUT_SINGLE = 0x11,
  CFG_KEY_LAYOUT_LIMITS = 0x12,
  CFG_KEY_MACROS,
  CFG_KEY_MACRO_LIMITS,
  CFG_KEY_MACRO_SINGLE,
  CFG_KEY_CKEYS,        // Custom Keys outline (all names/IDs)
  CFG_KEY_CKEY_SINGLE,  // Single Custom Key GET / SET / DELETE
  CFG_KEY_SYSTEM,       // Device identity (name, split config)
  CFG_KEY_COMBOS,       // 0x0D — Combo outline (all IDs/names)
  CFG_KEY_COMBO_SINGLE, // 0x0E — Single Combo GET/SET/DELETE
  CFG_KEY_COMBO_LIMITS, // 0x0F — Returns {maxCombos, maxKeys}
  CFG_KEY_MAX
} cfgmod_key_id_t;

typedef enum cfgmod_cmd : uint8_t {
  CFG_CMD_GET = 0,
  CFG_CMD_SET
} cfgmod_cmd_t;

typedef struct {
    uint8_t cmd;
    uint8_t key_id;
    uint16_t item_id;     // ID for *_SINGLE commands (e.g. layer ID, macro ID)
    uint8_t reserved[3];
} cfgmod_req_header_t;

typedef struct {
    uint8_t cmd;
    uint8_t key_id;
    uint8_t reserved;
    uint32_t status;
} cfgmod_rsp_header_t;

// Handle one COMM report and optionally build a response.
esp_err_t cfgmod_handle_usb_comm(const uint8_t *data, size_t len, uint8_t *out,
                                 size_t *out_len, size_t out_max);

bool cfg_is_init(void);

// Initialize cfg module dependencies (NVS, etc.).
esp_err_t cfg_init(void);
// Deinitialize cfg module (placeholder).
esp_err_t cfg_deinit(void);

// Callback signatures for typed configs
typedef void (*cfgmod_default_fn)(void *out_struct);
typedef void (*cfgmod_on_update_fn)(const char *key);

// Register a configuration kind's handler
esp_err_t cfgmod_register_kind(cfgmod_kind_t kind, cfgmod_default_fn def_fn,
                               cfgmod_on_update_fn update_fn,
                               size_t struct_size);

// Optional per-kind overrides for the USB GET/SET command paths.
// When registered, cfgmod_handle_usb_comm uses these instead of the default
// cfgmod_get_config / cfgmod_set_config, allowing a module to apply internal
// post-processing (e.g. cfg_system's sys_id overlay that keeps each half's
// device identity independent of config-sync overwrites).
typedef esp_err_t (*cfgmod_get_fn)(void *out_struct);
typedef esp_err_t (*cfgmod_set_fn)(const void *in_struct);
void cfgmod_register_get_set(cfgmod_kind_t kind, cfgmod_get_fn get_fn, cfgmod_set_fn set_fn);

// Fetch a config struct from storage (applies defaults and parses JSON)
esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key,
                            void *out_struct);

// Save a config struct to storage as a raw binary blob (calls update_fn on success)
esp_err_t cfgmod_set_config(cfgmod_kind_t kind, const char *key,
                            const void *in_struct);

// Basic storage helpers (backed by ESP32-S3 NVS).
// Read a blob from NVS into out_buf; inout_len is size in/out.
esp_err_t cfgmod_read_storage(cfgmod_kind_t kind, const char *key,
                              void *out_buf, size_t *inout_len);

// Write a blob to NVS for a kind/key pair.
esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key,
                               const void *data, size_t len);

// Delete a blob from NVS for a kind/key pair.
esp_err_t cfgmod_delete_storage(cfgmod_kind_t kind, const char *key);