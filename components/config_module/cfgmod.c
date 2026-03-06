#include "cfgmod.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cfg_macros.h"
#include "esp_heap_caps.h"

static cfg_macro_list_t *s_macro_scratch = NULL;

// module initializers
extern void cfg_layouts_register(void);
extern void cfg_macros_register(void);
extern void cfg_system_register(void);
extern void cfg_physical_register(void);
extern void cfg_ble_init(void);

#include "esp_log.h"

#define TAG "cfg_module"

#define CFGMOD_NVS_NAMESPACE "cfg"

typedef struct {
  cfgmod_default_fn def_fn;
  cfgmod_deserialize_fn des_fn;
  cfgmod_serialize_fn ser_fn;
  cfgmod_on_update_fn update_fn;
  size_t struct_size;
  bool registered;
} cfgmod_registry_t;

static cfgmod_registry_t s_registry[CFGMOD_KIND_MAX];

esp_err_t cfgmod_register_kind(cfgmod_kind_t kind, cfgmod_default_fn def_fn,
                               cfgmod_deserialize_fn des_fn,
                               cfgmod_serialize_fn ser_fn,
                               cfgmod_on_update_fn update_fn,
                               size_t struct_size) {
  if (kind >= CFGMOD_KIND_MAX)
    return ESP_ERR_INVALID_ARG;
  if (!def_fn || !des_fn || !ser_fn || struct_size == 0)
    return ESP_ERR_INVALID_ARG;

  s_registry[kind].def_fn = def_fn;
  s_registry[kind].des_fn = des_fn;
  s_registry[kind].ser_fn = ser_fn;
  s_registry[kind].update_fn = update_fn;
  s_registry[kind].struct_size = struct_size;
  s_registry[kind].registered = true;
  return ESP_OK;
}

/*
        comms
*/

typedef struct {
  cfgmod_kind_t kind;
  const char* key_name;
} cfgmod_key_map_t;

static const cfgmod_key_map_t s_key_map[CFG_KEY_MAX] = {
   [CFG_KEY_TEST]    = { CFGMOD_KIND_SYSTEM, "test" },
   [CFG_KEY_HELLO]   = { CFGMOD_KIND_SYSTEM, "hello" },
   [CFG_KEY_PHYSICAL_LAYOUT] = { CFGMOD_KIND_PHYSICAL, "physical" },
   [CFG_KEY_LAYER_0] = { CFGMOD_KIND_LAYOUT, "ly0" },
   [CFG_KEY_LAYER_1] = { CFGMOD_KIND_LAYOUT, "ly1" },
   [CFG_KEY_LAYER_2] = { CFGMOD_KIND_LAYOUT, "ly2" },
   [CFG_KEY_LAYER_3] = { CFGMOD_KIND_LAYOUT, "ly3" },
   [CFG_KEY_MACROS]  = { CFGMOD_KIND_MACRO, "macros" },
   [CFG_KEY_MACRO_LIMITS] = { CFGMOD_KIND_MACRO, "macros" },
   [CFG_KEY_MACRO_SINGLE] = { CFGMOD_KIND_MACRO, "macros" },
};

esp_err_t cfgmod_handle_usb_comm(const uint8_t *data, size_t len, uint8_t *out,
                                 size_t *out_len, size_t out_max) {
  if (!data || !out || !out_len || out_max == 0) {
    ESP_LOGE(TAG, "Wrong data");
    return ESP_ERR_INVALID_ARG;
  }
  *out_len = 0;

  if (len < sizeof(cfgmod_wire_header_t)) {
    ESP_LOGE(TAG, "Invalid size (size < sizeof(cfgmod_wire_header_t))");
    return ESP_ERR_INVALID_SIZE;
  }

  cfgmod_wire_header_t hdr;
  memcpy(&hdr, data, sizeof(hdr));


  if (hdr.key_id >= CFG_KEY_MAX) {
    ESP_LOGE(TAG, "Invalid Key ID: %d", hdr.key_id);
    return ESP_ERR_INVALID_ARG;
  }

  cfgmod_kind_t kind = s_key_map[hdr.key_id].kind;
  const char* key = s_key_map[hdr.key_id].key_name;

  // Use actual transport-level length (hdr.payload_len is uint8_t, truncates > 255)
  const uint8_t *data_in = data + sizeof(hdr);
  size_t data_in_len = len - sizeof(hdr);

  // Build response header
  cfgmod_wire_header_t rsp = {
    .cmd = hdr.cmd,
    .key_id = hdr.key_id
  };

  const size_t status_size = sizeof(esp_err_t);
  if (out_max < sizeof(rsp) + status_size)
    return ESP_ERR_NO_MEM;

  esp_err_t status = ESP_OK;
  uint8_t *out_payload = out + sizeof(rsp);
  size_t out_payload_max = out_max - sizeof(rsp);
  memset(out_payload, 0, out_payload_max);

  // Track actual payload length (rsp.payload_len is uint8_t, truncates > 255)
  size_t actual_payload_len = 0;

  // ESP_LOGI(TAG, "RX COMM");
  // log_hex("RX", data, len);

  // ---- Custom macro key handling ----
  if (hdr.key_id == CFG_KEY_MACRO_LIMITS && hdr.cmd == CFG_CMD_GET) {
    // Return compile-time limits as JSON
    cJSON *root = macros_serialize_limits();
    if (root) {
      char *json_str = cJSON_PrintUnformatted(root);
      cJSON_Delete(root);
      if (json_str) {
        size_t json_len = strlen(json_str) + 1;
        if (json_len <= out_payload_max - status_size) {
          memcpy(out_payload + status_size, json_str, json_len);
          actual_payload_len = status_size + json_len;
          status = ESP_OK;
        } else {
          status = ESP_ERR_NO_MEM;
          actual_payload_len = status_size;
        }
        free(json_str);
      } else {
        status = ESP_ERR_NO_MEM;
        actual_payload_len = status_size;
      }
    } else {
      status = ESP_ERR_NO_MEM;
      actual_payload_len = status_size;
    }
    memcpy(out_payload, &status, status_size);

  } else if (hdr.key_id == CFG_KEY_MACRO_SINGLE && hdr.cmd == CFG_CMD_SET) {
    // Parse incoming JSON (single macro or { "delete": id })
    char *temp_json = malloc(data_in_len + 1);
    cJSON *root = NULL;
    if (temp_json) {
      memcpy(temp_json, data_in, data_in_len);
      temp_json[data_in_len] = '\0';
      root = cJSON_Parse(temp_json);
      free(temp_json);
    }

    if (root) {
      // Load existing macro index from NVS
      cfg_macro_index_t idx = {0};
      size_t idx_len = sizeof(idx);
      cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len);

      cJSON *del = cJSON_GetObjectItem(root, "delete");
      if (cJSON_IsNumber(del)) {
        // Delete mode
        status = macros_delete_single((uint16_t)del->valueint, &idx);
      } else {
        // Upsert mode
        status = macros_upsert_single(root, &idx);
      }

      if (status != ESP_OK) {
          ESP_LOGE(TAG, "NVS write failed for macro: 0x%X", (unsigned)status);
      } else {
          if (s_registry[CFGMOD_KIND_MACRO].update_fn) {
              s_registry[CFGMOD_KIND_MACRO].update_fn("macros");
          }
      }

      cJSON_Delete(root);
    } else {
      status = ESP_ERR_INVALID_ARG;
    }

    memcpy(out_payload, &status, status_size);
    actual_payload_len = status_size;

  } else if (hdr.key_id == CFG_KEY_MACRO_SINGLE && hdr.cmd == CFG_CMD_GET) {
    // Expect incoming JSON: { "id": 123 }
    char *temp_json = malloc(data_in_len + 1);
    cJSON *root = NULL;
    uint16_t requested_id = 0xFFFF;
    if (temp_json) {
      memcpy(temp_json, data_in, data_in_len);
      temp_json[data_in_len] = '\0';
      root = cJSON_Parse(temp_json);
      free(temp_json);
      if (root) {
        cJSON *id_item = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsNumber(id_item)) {
          requested_id = (uint16_t)id_item->valueint;
        }
        cJSON_Delete(root);
      }
    }

    if (requested_id != 0xFFFF) {
        cfg_macro_index_t idx = {0};
        size_t idx_len = sizeof(idx);
        cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len);

        cJSON *single_macro = macros_serialize_single(requested_id, &idx);

        if (single_macro) {
          char *json_str = cJSON_PrintUnformatted(single_macro);
          cJSON_Delete(single_macro);
          if (json_str) {
            size_t json_len = strlen(json_str) + 1;
            if (json_len <= out_payload_max - status_size) {
              memcpy(out_payload + status_size, json_str, json_len);
              actual_payload_len = status_size + json_len;
              status = ESP_OK;
            } else {
              status = ESP_ERR_NO_MEM;
              actual_payload_len = status_size;
            }
            free(json_str);
          } else {
             status = ESP_ERR_NO_MEM;
             actual_payload_len = status_size;
          }
        } else {
          status = ESP_ERR_NO_MEM;
          actual_payload_len = status_size;
        }
    } else {
      status = ESP_ERR_INVALID_ARG;
      actual_payload_len = status_size;
    }
    memcpy(out_payload, &status, status_size);

  } else if (hdr.key_id == CFG_KEY_MACROS && hdr.cmd == CFG_CMD_GET) {
    // Return only the macro outline (IDs and names, without elements)
    cfg_macro_index_t idx = {0};
    size_t idx_len = sizeof(idx);
    cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len);
    
    cJSON *outline = macros_serialize_outline(&idx);

    if (outline) {
      char *json_str = cJSON_PrintUnformatted(outline);
      cJSON_Delete(outline);
      if (json_str) {
        size_t json_len = strlen(json_str) + 1;
        if (json_len <= out_payload_max - status_size) {
          memcpy(out_payload + status_size, json_str, json_len);
          actual_payload_len = status_size + json_len;
          status = ESP_OK;
        } else {
          status = ESP_ERR_NO_MEM;
          actual_payload_len = status_size;
        }
        free(json_str);
      } else {
         status = ESP_ERR_NO_MEM;
         actual_payload_len = status_size;
      }
    } else {
      status = ESP_ERR_NO_MEM;
      actual_payload_len = status_size;
    }
    memcpy(out_payload, &status, status_size);

  // ---- Generic GET/SET handlers ----
  } else if (hdr.cmd == CFG_CMD_GET) {
    ESP_LOGI(TAG, "Received GET message for %s (kind=%d, key_id=%d)", key, kind, (int)hdr.key_id);

    if (kind < CFGMOD_KIND_MAX && s_registry[kind].registered) {
      void *temp_struct = malloc(s_registry[kind].struct_size);
      if (temp_struct) {
        cfgmod_get_config(kind, key, temp_struct);
        cJSON *root = s_registry[kind].ser_fn(temp_struct);
        free(temp_struct);

        if (root) {
          char *json_str = cJSON_PrintUnformatted(root);
          cJSON_Delete(root);

          if (json_str) {
            size_t json_len = strlen(json_str) + 1;
            if (json_len <= out_payload_max - status_size) {
              memcpy(out_payload + status_size, json_str, json_len);
              actual_payload_len = status_size + json_len;
              status = ESP_OK;
            } else {
              status = ESP_ERR_NO_MEM;
              actual_payload_len = status_size;
            }
            free(json_str);
          } else {
            status = ESP_ERR_NO_MEM;
            actual_payload_len = status_size;
          }
        } else {
          status = ESP_ERR_NO_MEM;
          actual_payload_len = status_size;
        }
      } else {
        status = ESP_ERR_NO_MEM;
        actual_payload_len = status_size;
      }
    } else {
      size_t read_len = out_payload_max - status_size;
      status = cfgmod_read_storage(kind, key, out_payload + status_size, &read_len);
      if (status == ESP_OK) {
        actual_payload_len = status_size + read_len;
      } else {
        actual_payload_len = status_size;
      }
    }
    memcpy(out_payload, &status, status_size);

  } else if (hdr.cmd == CFG_CMD_SET) {
    ESP_LOGI(TAG, "Received SET message for %s (kind=%d, key_id=%d, len=%d)", key, kind, (int)hdr.key_id, (int)data_in_len);

    char *temp_json = malloc(data_in_len + 1);
    cJSON *root = NULL;
    if (temp_json) {
      memcpy(temp_json, data_in, data_in_len);
      temp_json[data_in_len] = '\0';
      root = cJSON_Parse(temp_json);
      free(temp_json);
    }

    if (root) {
      if (kind < CFGMOD_KIND_MAX && s_registry[kind].registered) {
        void *temp_struct = malloc(s_registry[kind].struct_size);
        if (temp_struct) {
          // Load existing config to apply partial JSON updates properly
          cfgmod_get_config(kind, key, temp_struct);
          if (s_registry[kind].des_fn(root, temp_struct)) {
            status = cfgmod_set_config(kind, key, temp_struct);
          } else {
            status = ESP_ERR_INVALID_ARG;
          }
          free(temp_struct);
        } else {
          status = ESP_ERR_NO_MEM;
        }
      } else {
        // Fallback for non-registered kinds (shouldn't happen really)
        char *json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
          status = cfgmod_write_storage(kind, key, json_str, strlen(json_str) + 1);
          free(json_str);
        } else {
          status = ESP_ERR_NO_MEM;
        }
      }
      cJSON_Delete(root);
    } else {
      status = ESP_ERR_INVALID_ARG;
    }

    memcpy(out_payload, &status, status_size);
    actual_payload_len = status_size;
  } else {
    status = ESP_ERR_INVALID_ARG;
    memcpy(out_payload, &status, status_size);
    actual_payload_len = status_size;
  }

  memcpy(out, &rsp, sizeof(rsp));
  *out_len = sizeof(rsp) + actual_payload_len;     // actual length, never truncated

  // ESP_LOGI(TAG, "TX COMM (actual_len=%d)", (int)actual_payload_len);
  // log_hex("TX", out, *out_len);

  return ESP_OK;
}

/*
        storage
*/

// Build a short NVS key from kind and key name.
static esp_err_t cfgmod_build_key(cfgmod_kind_t kind, const char *key,
                                  char *out_key, size_t out_len) {
  if (out_key == NULL || out_len == 0 || key == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (strlen(key) > CFGMOD_MAX_KEY_LEN) {
    return ESP_ERR_INVALID_ARG;
  }

  int written = snprintf(out_key, out_len, "k%d_%s", (int)kind, key);
  if (written < 0 || (size_t)written >= out_len) {
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

static bool s_init = false;
bool is_init(void) { return s_init; }

#include "usbmod.h"
#include "usb_send.h"

bool cfg_usb_callback(uint8_t *data, uint16_t data_len) {
    size_t buf_size = 32000;
    uint8_t *out_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) out_buf = malloc(buf_size); // fallback to internal RAM
    if (!out_buf) {
        ESP_LOGE(TAG, "cfg_usb_callback failed to allocate memory");
        return false;
    }
    
    size_t out_len = 0;
    
    // Add module ID back as the first byte of response so the web UI knows it's from Config
    out_buf[0] = MODULE_CONFIG;
    
    esp_err_t err = cfgmod_handle_usb_comm(data, data_len, out_buf + 1, &out_len, buf_size - 1);
    
    if (err == ESP_OK && out_len > 0) {
        send_payload(out_buf, out_len + 1);
    }
    
    free(out_buf);
    return err == ESP_OK;
}

static void* spiram_cjson_malloc(size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = malloc(size);
    return ptr;
}

static void spiram_cjson_free(void* ptr) {
    free(ptr);
}

// Initialize NVS for cfg storage use.
esp_err_t cfg_init(void) {
  // Use PSRAM for cJSON to prevent large config ASTs from exhausting internal memory
  cJSON_Hooks hooks;
  hooks.malloc_fn = spiram_cjson_malloc;
  hooks.free_fn = spiram_cjson_free;
  cJSON_InitHooks(&hooks);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_err_t erase_err = nvs_flash_erase();
    if (erase_err != ESP_OK) {
      return erase_err;
    }
    err = nvs_flash_init();
  }

  if (err == ESP_OK) {
    s_init = true;

    // Allocate macro scratch buffer — prefer PSRAM, fall back to internal RAM
    s_macro_scratch = heap_caps_calloc(1, sizeof(cfg_macro_list_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_macro_scratch) {
      ESP_LOGW(TAG, "PSRAM alloc failed for macro scratch, trying internal RAM");
      s_macro_scratch = calloc(1, sizeof(cfg_macro_list_t));
    }
    if (!s_macro_scratch) {
      ESP_LOGE(TAG, "Failed to allocate macro scratch (%u bytes)", (unsigned)sizeof(cfg_macro_list_t));
      // Continue anyway — macros won't work but other features should
    }

    cfg_layouts_register();
    cfg_macros_register();
    cfg_system_register();
    cfg_physical_register();
    cfg_ble_init();

    // Register test data
    static const char hello_msg[] = "\"Hello world\"";
    cfgmod_write_storage(CFGMOD_KIND_SYSTEM, "hello", hello_msg, sizeof(hello_msg));
    static const char test_msg[] = "{\"val\": 123}";
    cfgmod_write_storage(CFGMOD_KIND_SYSTEM, "test", test_msg, sizeof(test_msg));

    // Register USB callback for the CONFIG MODULE
    usbmod_register_callback(MODULE_CONFIG, cfg_usb_callback);
  }

  return err;
}

// Deinitialize cfg module (placeholder for future cleanup).
esp_err_t cfg_deinit(void) { return ESP_OK; }

// Read a blob from NVS for the given kind/key.
esp_err_t cfgmod_read_storage(cfgmod_kind_t kind, const char *key,
                              void *out_buf, size_t *inout_len) {
  if (out_buf == NULL || inout_len == NULL || *inout_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  char nvs_key[16] = {0};
  esp_err_t err = cfgmod_build_key(kind, key, nvs_key, sizeof(nvs_key));
  if (err != ESP_OK) {
    return err;
  }

  nvs_handle_t handle;
  err = nvs_open(CFGMOD_NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_get_blob(handle, nvs_key, out_buf, inout_len);
  nvs_close(handle);
  return err;
}

// Write a blob to NVS for the given kind/key.
esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key,
                               const void *data, size_t len) {
  if (data == NULL || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  char nvs_key[16] = {0};
  esp_err_t err = cfgmod_build_key(kind, key, nvs_key, sizeof(nvs_key));
  if (err != ESP_OK) {
    return err;
  }

  nvs_handle_t handle;
  err = nvs_open(CFGMOD_NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_blob(handle, nvs_key, data, len);
  ESP_LOGI(TAG, "NVS set_blob %s (len=%u) ret=0x%X", nvs_key, (unsigned)len, (unsigned)err);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

// Fetch a config struct from storage
esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key,
                            void *out_struct) {
  if (kind >= CFGMOD_KIND_MAX || !s_registry[kind].registered || !out_struct) {
    return ESP_ERR_INVALID_ARG;
  }

  // 1. Apply defaults first (fail-safe)
  s_registry[kind].def_fn(out_struct);

  // 2. Read JSON from NVS
  char nvs_key[16] = {0};
  if (cfgmod_build_key(kind, key, nvs_key, sizeof(nvs_key)) != ESP_OK) {
    return ESP_OK; // fallback to default
  }

  nvs_handle_t handle;
  if (nvs_open(CFGMOD_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return ESP_OK; // fallback to default
  }

  size_t required_size = 0;
  if (nvs_get_blob(handle, nvs_key, NULL, &required_size) != ESP_OK ||
      required_size == 0) {
    nvs_close(handle);
    return ESP_OK; // fallback to default
  }

  // Attempt binary load first (if size matches exactly)
  if (required_size == s_registry[kind].struct_size) {
    if (nvs_get_blob(handle, nvs_key, out_struct, &required_size) == ESP_OK) {
      ESP_LOGI(TAG, "NVS get_blob %s (len=%u) binary load successful", nvs_key, (unsigned)required_size);
      nvs_close(handle);
      return ESP_OK;
    }
  }

  // Legacy JSON fallback
  char *json_str = malloc(required_size + 1);
  if (!json_str) {
    nvs_close(handle);
    return ESP_ERR_NO_MEM;
  }

  if (nvs_get_blob(handle, nvs_key, json_str, &required_size) == ESP_OK) {
    json_str[required_size] = '\0'; // Ensure null-termination
    ESP_LOGI(TAG, "NVS get_blob %s (len=%u) legacy JSON load", nvs_key, (unsigned)required_size);
    cJSON *root = cJSON_Parse(json_str);
    if (root) {
      if (!s_registry[kind].des_fn(root, out_struct)) {
        ESP_LOGE(TAG,
                 "Failed to deserialize JSON for config %s, using defaults",
                 nvs_key);
        s_registry[kind].def_fn(
            out_struct); // Reset to defaults on deserialization failure
      }
      cJSON_Delete(root);
    } else {
      ESP_LOGE(TAG, "Failed to parse JSON for config %s, using defaults",
               nvs_key);
      s_registry[kind].def_fn(out_struct); // Reset to defaults on parse failure
    }
  }

  free(json_str);
  nvs_close(handle);
  return ESP_OK;
}

// Save a config struct to storage
esp_err_t cfgmod_set_config(cfgmod_kind_t kind, const char *key,
                            const void *in_struct) {
  if (kind >= CFGMOD_KIND_MAX || !s_registry[kind].registered || !in_struct) {
    return ESP_ERR_INVALID_ARG;
  }

  // Write binary directly instead of JSON
  esp_err_t err = cfgmod_write_storage(kind, key, in_struct, s_registry[kind].struct_size);

  if (err == ESP_OK && s_registry[kind].update_fn) {
    s_registry[kind].update_fn(key);
  }
  return err;
}
