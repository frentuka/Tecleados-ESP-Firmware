#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
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
  CFGMOD_KIND_MAX
} cfgmod_kind_t;

typedef enum cfgmod_key_id : uint8_t {
  CFG_KEY_TEST = 0,
  CFG_KEY_HELLO,
  CFG_KEY_PHYSICAL_LAYOUT,
  CFG_KEY_LAYER_0,
  CFG_KEY_LAYER_1,
  CFG_KEY_LAYER_2,
  CFG_KEY_LAYER_3,
  CFG_KEY_MACROS,
  CFG_KEY_MACRO_LIMITS,
  CFG_KEY_MACRO_SINGLE,
  CFG_KEY_CKEYS,        // Custom Keys outline (all names/IDs)
  CFG_KEY_CKEY_SINGLE,  // Single Custom Key GET / SET / DELETE
  CFG_KEY_MAX
} cfgmod_key_id_t;

typedef enum cfgmod_cmd : uint8_t {
  CFG_CMD_GET = 0,
  CFG_CMD_SET
} cfgmod_cmd_t;

typedef struct __attribute__((packed)) cfgmod_wire_header {
  uint8_t cmd;         // cfgmod_cmd_t (GET/SET)
  uint8_t key_id;      // cfgmod_key_id_t
} cfgmod_wire_header_t;

// Handle one COMM report and optionally build a response.
esp_err_t cfgmod_handle_usb_comm(const uint8_t *data, size_t len, uint8_t *out,
                                 size_t *out_len, size_t out_max);

bool is_init(void);

// Initialize cfg module dependencies (NVS, etc.).
esp_err_t cfg_init(void);
// Deinitialize cfg module (placeholder).
esp_err_t cfg_deinit(void);

// Callback signatures for typed configs
typedef void (*cfgmod_default_fn)(void *out_struct);
typedef bool (*cfgmod_deserialize_fn)(cJSON *root, void *out_struct);
typedef cJSON *(*cfgmod_serialize_fn)(const void *in_struct);
typedef void (*cfgmod_on_update_fn)(const char *key);

// Register a configuration kind's handler
esp_err_t cfgmod_register_kind(cfgmod_kind_t kind, cfgmod_default_fn def_fn,
                               cfgmod_deserialize_fn des_fn,
                               cfgmod_serialize_fn ser_fn,
                               cfgmod_on_update_fn update_fn,
                               size_t struct_size);

// Fetch a config struct from storage (applies defaults and parses JSON)
esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key,
                            void *out_struct);

// Save a config struct to storage (serializes to JSON before saving)
esp_err_t cfgmod_set_config(cfgmod_kind_t kind, const char *key,
                            const void *in_struct);

// Basic storage helpers (backed by ESP32-S3 NVS).
// Read a blob from NVS into out_buf; inout_len is size in/out.
esp_err_t cfgmod_read_storage(cfgmod_kind_t kind, const char *key,
                              void *out_buf, size_t *inout_len);

// Write a blob to NVS for a kind/key pair.
esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key,
                               const void *data, size_t len);