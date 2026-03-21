#include "cfgmod.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "usbmod.h"
#include "usb_send.h"

#include "cfg_custom_keys.h"
#include "cfg_macros.h"
#include "event_bus.h"

static inline void cfgmod_post_update_event(cfgmod_kind_t kind, const char *key) {
    config_update_event_t ev = { .kind = (uint8_t)kind };
    strlcpy(ev.key, key ? key : "", sizeof(ev.key));
    esp_event_post(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, &ev, sizeof(ev), 0);
}

// module initializers
extern void cfg_layouts_register(void);
extern void cfg_macros_register(void);
extern void cfg_system_register(void);
extern void cfg_physical_register(void);
extern void cfg_ble_init(void);

#define TAG "cfg_module"

#define CFGMOD_NVS_NAMESPACE "cfg"

/* Per-kind NVS namespaces. NULL = fall back to "cfg" with prefixed key. */
static const char *const s_kind_ns[CFGMOD_KIND_MAX] = {
    [CFGMOD_KIND_LAYOUT]     = "cfg_lay",
    [CFGMOD_KIND_MACRO]      = "cfg_mac",
    [CFGMOD_KIND_CONNECTION] = NULL,
    [CFGMOD_KIND_SYSTEM]     = NULL,
    [CFGMOD_KIND_PHYSICAL]   = NULL,
    [CFGMOD_KIND_CKEY]       = "cfg_ck",
};

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
   [CFG_KEY_CKEYS]        = { CFGMOD_KIND_CKEY, "ckeys" },
   [CFG_KEY_CKEY_SINGLE]  = { CFGMOD_KIND_CKEY, "ckeys" },
};

/*
 * Parse a cJSON object from a raw byte buffer.
 * Allocates a temporary null-terminated copy, parses it, then frees the copy.
 * The caller owns the returned cJSON tree and must call cJSON_Delete() on it.
 * Returns NULL on allocation failure or JSON parse error.
 */
static cJSON *parse_json_from_bytes(const uint8_t *data, size_t data_len) {
    char *tmp = malloc(data_len + 1);
    if (!tmp) return NULL;
    memcpy(tmp, data, data_len);
    tmp[data_len] = '\0';
    cJSON *root = cJSON_Parse(tmp);
    free(tmp);
    return root;
}

/*
 * Serialize `root` as unformatted JSON into out_payload[status_size..].
 * Always calls cJSON_Delete(root) — ownership is transferred.
 * Sets *out_status to ESP_OK or ESP_ERR_NO_MEM.
 * Sets *out_payload_len to (status_size + json_len) on success,
 * or status_size on failure.
 */
static void write_json_response(cJSON *root,
                                uint8_t *out_payload, size_t out_payload_max,
                                size_t status_size,
                                esp_err_t *out_status,
                                size_t *out_payload_len) {
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        *out_status = ESP_ERR_NO_MEM;
        *out_payload_len = status_size;
        return;
    }
    size_t json_len = strlen(json_str) + 1;
    if (json_len <= out_payload_max - status_size) {
        memcpy(out_payload + status_size, json_str, json_len);
        *out_payload_len = status_size + json_len;
        *out_status = ESP_OK;
    } else {
        *out_status = ESP_ERR_NO_MEM;
        *out_payload_len = status_size;
    }
    free(json_str);
}

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
  const char *key = s_key_map[hdr.key_id].key_name;

  const uint8_t *data_in = data + sizeof(hdr);
  size_t data_in_len = len - sizeof(hdr);

  // Build response header
  cfgmod_wire_header_t rsp = { .cmd = hdr.cmd, .key_id = hdr.key_id };

  const size_t status_size = sizeof(esp_err_t);
  if (out_max < sizeof(rsp) + status_size)
    return ESP_ERR_NO_MEM;

  esp_err_t status = ESP_OK;
  uint8_t *out_payload = out + sizeof(rsp);
  size_t out_payload_max = out_max - sizeof(rsp);
  memset(out_payload, 0, out_payload_max);
  size_t actual_payload_len = 0;

  // ---- Macro handlers ----
  if (hdr.key_id == CFG_KEY_MACRO_LIMITS && hdr.cmd == CFG_CMD_GET) {
    write_json_response(macros_serialize_limits(),
                        out_payload, out_payload_max, status_size,
                        &status, &actual_payload_len);

  } else if (hdr.key_id == CFG_KEY_MACRO_SINGLE && hdr.cmd == CFG_CMD_SET) {
    cJSON *root = parse_json_from_bytes(data_in, data_in_len);
    if (root) {
      cfg_macro_index_t idx = {0};
      size_t idx_len = sizeof(idx);
      cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len);

      cJSON *del = cJSON_GetObjectItem(root, "delete");
      status = cJSON_IsNumber(del)
               ? macros_delete_single((uint16_t)del->valueint, &idx)
               : macros_upsert_single(root, &idx);

      if (status != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed for macro: 0x%X", (unsigned)status);
      } else {
        if (s_registry[CFGMOD_KIND_MACRO].update_fn) {
          s_registry[CFGMOD_KIND_MACRO].update_fn("macros");
        }
        cfgmod_post_update_event(CFGMOD_KIND_MACRO, "macros");
      }
      cJSON_Delete(root);
    } else {
      status = ESP_ERR_INVALID_ARG;
    }
    actual_payload_len = status_size;

  } else if (hdr.key_id == CFG_KEY_MACRO_SINGLE && hdr.cmd == CFG_CMD_GET) {
    cJSON *req = parse_json_from_bytes(data_in, data_in_len);
    uint16_t requested_id = 0xFFFF;
    if (req) {
      cJSON *id_item = cJSON_GetObjectItem(req, "id");
      if (cJSON_IsNumber(id_item)) requested_id = (uint16_t)id_item->valueint;
      cJSON_Delete(req);
    }
    if (requested_id != 0xFFFF) {
      cfg_macro_index_t idx = {0};
      size_t idx_len = sizeof(idx);
      cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len);
      write_json_response(macros_serialize_single(requested_id, &idx),
                          out_payload, out_payload_max, status_size,
                          &status, &actual_payload_len);
    } else {
      status = ESP_ERR_INVALID_ARG;
      actual_payload_len = status_size;
    }

  } else if (hdr.key_id == CFG_KEY_MACROS && hdr.cmd == CFG_CMD_GET) {
    cfg_macro_index_t idx = {0};
    size_t idx_len = sizeof(idx);
    cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len);
    write_json_response(macros_serialize_outline(&idx),
                        out_payload, out_payload_max, status_size,
                        &status, &actual_payload_len);

  // ---- Custom Key handlers ----
  } else if (hdr.key_id == CFG_KEY_CKEYS && hdr.cmd == CFG_CMD_GET) {
    cfg_ckey_index_t ck_idx = {0};
    size_t ck_idx_len = sizeof(ck_idx);
    cfgmod_read_storage(CFGMOD_KIND_CKEY, "ck_idx", &ck_idx, &ck_idx_len);
    write_json_response(ckeys_serialize_outline(&ck_idx),
                        out_payload, out_payload_max, status_size,
                        &status, &actual_payload_len);

  } else if (hdr.key_id == CFG_KEY_CKEY_SINGLE && hdr.cmd == CFG_CMD_GET) {
    cJSON *req = parse_json_from_bytes(data_in, data_in_len);
    uint16_t requested_id = 0xFFFF;
    if (req) {
      cJSON *id_item = cJSON_GetObjectItem(req, "id");
      if (cJSON_IsNumber(id_item)) requested_id = (uint16_t)id_item->valueint;
      cJSON_Delete(req);
    }
    if (requested_id != 0xFFFF) {
      cfg_ckey_index_t ck_idx = {0};
      size_t ck_idx_len = sizeof(ck_idx);
      cfgmod_read_storage(CFGMOD_KIND_CKEY, "ck_idx", &ck_idx, &ck_idx_len);
      write_json_response(ckeys_serialize_single(requested_id, &ck_idx),
                          out_payload, out_payload_max, status_size,
                          &status, &actual_payload_len);
    } else {
      status = ESP_ERR_INVALID_ARG;
      actual_payload_len = status_size;
    }

  } else if (hdr.key_id == CFG_KEY_CKEY_SINGLE && hdr.cmd == CFG_CMD_SET) {
    cJSON *root = parse_json_from_bytes(data_in, data_in_len);
    if (root) {
      cfg_ckey_index_t ck_idx = {0};
      size_t ck_idx_len = sizeof(ck_idx);
      cfgmod_read_storage(CFGMOD_KIND_CKEY, "ck_idx", &ck_idx, &ck_idx_len);

      cJSON *del = cJSON_GetObjectItem(root, "delete");
      status = cJSON_IsNumber(del)
               ? ckeys_delete_single((uint16_t)del->valueint, &ck_idx)
               : ckeys_upsert_single(root, &ck_idx);

      if (status != ESP_OK) {
        ESP_LOGE(TAG, "Custom key NVS write failed: 0x%X", (unsigned)status);
      } else {
        if (s_registry[CFGMOD_KIND_CKEY].update_fn) {
          s_registry[CFGMOD_KIND_CKEY].update_fn("ckeys");
        }
        cfgmod_post_update_event(CFGMOD_KIND_CKEY, "ckeys");
      }
      cJSON_Delete(root);
    } else {
      status = ESP_ERR_INVALID_ARG;
    }
    actual_payload_len = status_size;

  // ---- Generic GET/SET handlers ----
  } else if (hdr.cmd == CFG_CMD_GET) {
    ESP_LOGI(TAG, "Received GET message for %s (kind=%d, key_id=%d)", key, kind, (int)hdr.key_id);

    if (kind < CFGMOD_KIND_MAX && s_registry[kind].registered) {
      void *temp_struct = malloc(s_registry[kind].struct_size);
      if (temp_struct) {
        cfgmod_get_config(kind, key, temp_struct);
        cJSON *root = s_registry[kind].ser_fn(temp_struct);
        free(temp_struct);
        write_json_response(root,
                            out_payload, out_payload_max, status_size,
                            &status, &actual_payload_len);
      } else {
        status = ESP_ERR_NO_MEM;
        actual_payload_len = status_size;
      }
    } else {
      size_t read_len = out_payload_max - status_size;
      status = cfgmod_read_storage(kind, key, out_payload + status_size, &read_len);
      actual_payload_len = (status == ESP_OK) ? status_size + read_len : status_size;
    }

  } else if (hdr.cmd == CFG_CMD_SET) {
    ESP_LOGI(TAG, "Received SET message for %s (kind=%d, key_id=%d, len=%d)", key, kind, (int)hdr.key_id, (int)data_in_len);

    cJSON *root = parse_json_from_bytes(data_in, data_in_len);
    if (root) {
      if (kind < CFGMOD_KIND_MAX && s_registry[kind].registered) {
        void *temp_struct = malloc(s_registry[kind].struct_size);
        if (temp_struct) {
          // Load existing config first to support partial JSON updates
          cfgmod_get_config(kind, key, temp_struct);
          status = s_registry[kind].des_fn(root, temp_struct)
                   ? cfgmod_set_config(kind, key, temp_struct)
                   : ESP_ERR_INVALID_ARG;
          free(temp_struct);
        } else {
          status = ESP_ERR_NO_MEM;
        }
      } else {
        // Fallback for non-registered kinds (raw JSON string write)
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
    actual_payload_len = status_size;

  } else {
    status = ESP_ERR_INVALID_ARG;
    actual_payload_len = status_size;
  }

  memcpy(out_payload, &status, status_size);
  memcpy(out, &rsp, sizeof(rsp));
  *out_len = sizeof(rsp) + actual_payload_len;

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

/*
 * Resolve the NVS namespace and key name for a given (kind, key) pair.
 * Kinds with a dedicated namespace use it directly; all others use the
 * shared "cfg" namespace with a "k<kind>_<key>" prefix to avoid collisions.
 *
 * key_buf / key_buf_len is scratch space owned by the caller; out_nvs_key
 * points into it when the prefix scheme is used, or directly into key otherwise.
 */
static esp_err_t resolve_ns_and_key(cfgmod_kind_t kind, const char *key,
                                    const char **out_ns, const char **out_nvs_key,
                                    char *key_buf, size_t key_buf_len) {
  if (kind < CFGMOD_KIND_MAX && s_kind_ns[kind]) {
    *out_ns      = s_kind_ns[kind];
    *out_nvs_key = key;
  } else {
    *out_ns = CFGMOD_NVS_NAMESPACE;
    esp_err_t err = cfgmod_build_key(kind, key, key_buf, key_buf_len);
    if (err != ESP_OK) return err;
    *out_nvs_key = key_buf;
  }
  return ESP_OK;
}

static bool s_init = false;
bool cfg_is_init(void) { return s_init; }

#define CFG_USB_RESP_BUF_SIZE 32000

bool cfg_usb_callback(uint8_t *data, uint16_t data_len) {
    size_t buf_size = CFG_USB_RESP_BUF_SIZE;
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

    cfg_layouts_register();
    cfg_macros_register();
    cfg_custom_keys_register(NULL); // keyboard module re-registers with its callback in kb_custom_key_init()
    cfg_system_register();
    cfg_physical_register();
    cfg_ble_init();

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

  const char *ns, *nvs_key;
  char nvs_key_buf[16] = {0};
  esp_err_t err = resolve_ns_and_key(kind, key, &ns, &nvs_key, nvs_key_buf, sizeof(nvs_key_buf));
  if (err != ESP_OK) return err;

  nvs_handle_t handle;
  err = nvs_open(ns, NVS_READONLY, &handle);
  if (err != ESP_OK) return err;

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

  const char *ns, *nvs_key;
  char nvs_key_buf[16] = {0};
  esp_err_t err = resolve_ns_and_key(kind, key, &ns, &nvs_key, nvs_key_buf, sizeof(nvs_key_buf));
  if (err != ESP_OK) return err;

  nvs_handle_t handle;
  err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  err = nvs_set_blob(handle, nvs_key, data, len);
  ESP_LOGI(TAG, "NVS set_blob %s/%s (len=%u) ret=0x%X", ns, nvs_key, (unsigned)len, (unsigned)err);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

// Fetch a config struct from storage (applies defaults then tries NVS binary/JSON)
esp_err_t cfgmod_get_config(cfgmod_kind_t kind, const char *key,
                            void *out_struct) {
  if (kind >= CFGMOD_KIND_MAX || !s_registry[kind].registered || !out_struct) {
    return ESP_ERR_INVALID_ARG;
  }

  // 1. Apply defaults first (fail-safe baseline)
  s_registry[kind].def_fn(out_struct);

  // 2. Resolve NVS namespace and key name
  const char *ns, *nvs_key;
  char nvs_key_buf[16] = {0};
  if (resolve_ns_and_key(kind, key, &ns, &nvs_key, nvs_key_buf, sizeof(nvs_key_buf)) != ESP_OK) {
    return ESP_OK; // fallback to default
  }

  nvs_handle_t handle;
  if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) {
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

  if (err == ESP_OK) {
    if (s_registry[kind].update_fn) {
      s_registry[kind].update_fn(key);
    }
    cfgmod_post_update_event(kind, key);
  }
  return err;
}
