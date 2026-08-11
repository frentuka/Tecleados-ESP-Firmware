#include "cfgmod.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "comm_module.h"
#include "comm_transport.h"

#include "cfg_custom_keys.h"
#include "cfg_macros.h"
#include "cfg_combos.h"
#include "cfg_layouts.h"
#include "cfg_storage_keys.h"
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
extern void cfg_ble_init(void);

extern esp_err_t cfg_ble_bond_read_all(void *out_buf, size_t *inout_len);
extern esp_err_t cfg_ble_bond_write_all(const void *data, size_t len);

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
    [CFGMOD_KIND_SPLIT]      = "cfg_spl",
    [CFGMOD_KIND_COMBO]      = "cfg_cmb",
    [CFGMOD_KIND_BLE_BOND]   = NULL,           // Handled by cfg_ble_bond_{read,write}_all; ns unused
};

typedef struct {
  cfgmod_default_fn def_fn;
  cfgmod_on_update_fn update_fn;
  size_t struct_size;
  bool registered;
  cfgmod_get_fn get_fn;  // optional: overrides cfgmod_get_config in USB GET handler
  cfgmod_set_fn set_fn;  // optional: overrides cfgmod_set_config in USB SET handler
  cfgmod_validate_fn validate_fn; // optional: validates data before writing to NVS
} cfgmod_registry_t;

static cfgmod_registry_t s_registry[CFGMOD_KIND_MAX];

void cfgmod_register_get_set(cfgmod_kind_t kind, cfgmod_get_fn get_fn, cfgmod_set_fn set_fn) {
  if (kind < CFGMOD_KIND_MAX) {
    s_registry[kind].get_fn = get_fn;
    s_registry[kind].set_fn = set_fn;
  }
}

void cfgmod_register_validate(cfgmod_kind_t kind, cfgmod_validate_fn validate_fn) {
  if (kind < CFGMOD_KIND_MAX) {
    s_registry[kind].validate_fn = validate_fn;
  }
}

esp_err_t cfgmod_register_kind(cfgmod_kind_t kind, cfgmod_default_fn def_fn,
                               cfgmod_on_update_fn update_fn,
                               size_t struct_size) {
  if (kind >= CFGMOD_KIND_MAX)
    return ESP_ERR_INVALID_ARG;
  if (!def_fn || struct_size == 0)
    return ESP_ERR_INVALID_ARG;

  s_registry[kind].def_fn = def_fn;
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
   [CFG_KEY_PHYSICAL_LAYOUT] = { CFGMOD_KIND_PHYSICAL, CFG_ST_PHYSICAL_LAYOUT },
   [CFG_KEY_LAYOUTS]       = { CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX },
   [CFG_KEY_LAYOUT_SINGLE] = { CFGMOD_KIND_LAYOUT, NULL },
   [CFG_KEY_MACROS]  = { CFGMOD_KIND_MACRO, "mac_idx" },
   [CFG_KEY_MACRO_SINGLE] = { CFGMOD_KIND_MACRO, NULL },
   [CFG_KEY_CKEYS]        = { CFGMOD_KIND_CKEY, "ck_idx" },
   [CFG_KEY_CKEY_SINGLE]  = { CFGMOD_KIND_CKEY, NULL },
   [CFG_KEY_SYSTEM]       = { CFGMOD_KIND_SYSTEM, "sys" },
   [CFG_KEY_COMBOS]       = { CFGMOD_KIND_COMBO, "cmb_idx" },
   [CFG_KEY_COMBO_SINGLE] = { CFGMOD_KIND_COMBO, NULL },
};

static void update_active_mask(cfgmod_kind_t kind, uint16_t id, bool active) {
    if (kind == CFGMOD_KIND_MACRO) {
        if (id >= 64) return;
        uint64_t mask = 0;
        size_t len = sizeof(mask);
        cfgmod_read_storage(kind, "mac_idx", &mask, &len);
        if (active) mask |= (1ULL << id);
        else mask &= ~(1ULL << id);
        cfgmod_write_storage(kind, "mac_idx", &mask, sizeof(mask));
    } else if (kind == CFGMOD_KIND_COMBO) {
        if (id >= 32) return;
        uint32_t mask = 0;
        size_t len = sizeof(mask);
        cfgmod_read_storage(kind, "cmb_idx", &mask, &len);
        if (active) mask |= (1UL << id);
        else mask &= ~(1UL << id);
        cfgmod_write_storage(kind, "cmb_idx", &mask, sizeof(mask));
    } else if (kind == CFGMOD_KIND_CKEY) {
        if (id >= 120) return;
        uint8_t mask[15] = {0};
        size_t len = sizeof(mask);
        cfgmod_read_storage(kind, "ck_idx", mask, &len);
        if (active) mask[id / 8] |= (1 << (id % 8));
        else mask[id / 8] &= ~(1 << (id % 8));
        cfgmod_write_storage(kind, "ck_idx", mask, sizeof(mask));
    }
}

esp_err_t cfgmod_handle_usb_comm(const uint8_t *data, size_t len, uint8_t *out,
                                 size_t *out_len, size_t out_max) {
  if (!data || !out || !out_len || out_max == 0) return ESP_ERR_INVALID_ARG;
  *out_len = 0;

  if (len < sizeof(cfgmod_req_header_t)) return ESP_ERR_INVALID_SIZE;

  cfgmod_req_header_t hdr;
  memcpy(&hdr, data, sizeof(hdr));

  if (hdr.key_id >= CFG_KEY_MAX) return ESP_ERR_INVALID_ARG;

  // Handle LIMITS keys (required by configurator frontend)
  if (hdr.cmd == CFG_CMD_GET) {
      if (hdr.key_id == CFG_KEY_LAYOUT_LIMITS) {
          uint8_t limit = 16; // CFG_LAYOUT_MAX_COUNT
          cfgmod_rsp_header_t rsp = { .cmd = hdr.cmd, .key_id = hdr.key_id, .status = ESP_OK };
          memcpy(out, &rsp, sizeof(rsp));
          memcpy(out + sizeof(rsp), &limit, 1);
          *out_len = sizeof(rsp) + 1;
          return ESP_OK;
      }
      if (hdr.key_id == CFG_KEY_MACRO_LIMITS) {
          uint16_t limits[2] = { 64, 256 }; // maxMacros, maxEvents
          cfgmod_rsp_header_t rsp = { .cmd = hdr.cmd, .key_id = hdr.key_id, .status = ESP_OK };
          memcpy(out, &rsp, sizeof(rsp));
          memcpy(out + sizeof(rsp), limits, 4);
          *out_len = sizeof(rsp) + 4;
          return ESP_OK;
      }
      if (hdr.key_id == CFG_KEY_COMBO_LIMITS) {
          uint16_t limits[2] = { 32, 8 }; // maxCombos, maxKeys
          cfgmod_rsp_header_t rsp = { .cmd = hdr.cmd, .key_id = hdr.key_id, .status = ESP_OK };
          memcpy(out, &rsp, sizeof(rsp));
          memcpy(out + sizeof(rsp), limits, 4);
          *out_len = sizeof(rsp) + 4;
          return ESP_OK;
      }
  }

  cfgmod_kind_t kind = s_key_map[hdr.key_id].kind;
  const char *base_key = s_key_map[hdr.key_id].key_name;
  
  char dynamic_key[CFGMOD_MAX_KEY_LEN] = {0};
  const char *target_key = base_key;

  // Handle *_SINGLE commands which use dynamic keys based on item_id
  if (!base_key) {
      if (hdr.key_id == CFG_KEY_LAYOUT_SINGLE) snprintf(dynamic_key, sizeof(dynamic_key), CFG_ST_LAYER_FMT, hdr.item_id);
      else if (hdr.key_id == CFG_KEY_MACRO_SINGLE) snprintf(dynamic_key, sizeof(dynamic_key), "mac_%u", hdr.item_id);
      else if (hdr.key_id == CFG_KEY_CKEY_SINGLE) snprintf(dynamic_key, sizeof(dynamic_key), "ck_%u", hdr.item_id);
      else if (hdr.key_id == CFG_KEY_COMBO_SINGLE) snprintf(dynamic_key, sizeof(dynamic_key), "cmb_%u", hdr.item_id);
      else return ESP_ERR_INVALID_ARG;
      target_key = dynamic_key;
  }

  const uint8_t *data_in = data + sizeof(hdr);
  size_t data_in_len = len - sizeof(hdr);

  cfgmod_rsp_header_t rsp = { .cmd = hdr.cmd, .key_id = hdr.key_id, .status = ESP_OK };
  if (out_max < sizeof(rsp)) return ESP_ERR_NO_MEM;

  uint8_t *out_payload = out + sizeof(rsp);
  size_t out_payload_max = out_max - sizeof(rsp);
  size_t actual_payload_len = 0;

  if (hdr.cmd == CFG_CMD_GET) {
      if (s_registry[kind].get_fn) {
          rsp.status = s_registry[kind].get_fn(out_payload);
          if (rsp.status == ESP_OK) actual_payload_len = s_registry[kind].struct_size;
      } else {
          size_t read_len = out_payload_max;
          rsp.status = cfgmod_read_storage(kind, target_key, out_payload, &read_len);
          if (rsp.status == ESP_OK) {
              actual_payload_len = read_len;
          } else if (rsp.status == ESP_ERR_NVS_NOT_FOUND || rsp.status == ESP_ERR_NOT_FOUND) {
              if (hdr.key_id == CFG_KEY_MACROS) {
                  memset(out_payload, 0, 8);
                  actual_payload_len = 8;
                  rsp.status = ESP_OK;
              } else if (hdr.key_id == CFG_KEY_COMBOS) {
                  memset(out_payload, 0, 4);
                  actual_payload_len = 4;
                  rsp.status = ESP_OK;
              } else if (hdr.key_id == CFG_KEY_CKEYS) {
                  memset(out_payload, 0, 15);
                  actual_payload_len = 15;
                  rsp.status = ESP_OK;
              } else if (s_registry[kind].def_fn && s_registry[kind].struct_size <= out_payload_max) {
                  s_registry[kind].def_fn(out_payload);
                  actual_payload_len = s_registry[kind].struct_size;
                  rsp.status = ESP_OK;
              }
          }
      }
  } else if (hdr.cmd == CFG_CMD_SET) {
      // Intercept layout commands that don't match the default size (create, rename, delete)
      if (hdr.key_id == CFG_KEY_LAYOUT_SINGLE) {
          if (hdr.item_id == 0xFFFF) {
              // Create layout: data_in contains up to 24 chars of name
              uint8_t out_id = 0;
              rsp.status = cfg_layout_create((const char *)data_in, &out_id);
              if (rsp.status == ESP_OK) {
                  uint16_t id16 = out_id;
                  memcpy(out_payload, &id16, sizeof(id16));
                  actual_payload_len = sizeof(id16);
              }
              goto end_set;
          } else if (hdr.item_id & 0x8000) {
              // Rename layout: item_id & ~0x8000 is the real id
              uint16_t id = hdr.item_id & ~0x8000;
              rsp.status = cfg_layout_rename((uint8_t)id, (const char *)data_in);
              goto end_set;
          } else if (data_in_len == 0) {
              // Delete layout
              rsp.status = cfg_layout_delete((uint8_t)hdr.item_id);
              goto end_set;
          }
      }

      if (data_in_len == 0 && !base_key) {
          // 0-length payload for *_SINGLE means DELETE
          rsp.status = cfgmod_delete_storage(kind, target_key);
          if (rsp.status == ESP_OK) {
              update_active_mask(kind, hdr.item_id, false);
              if (s_registry[kind].update_fn) {
                  s_registry[kind].update_fn(target_key);
              }
          }
      } else {
          if (s_registry[kind].registered && s_registry[kind].struct_size != data_in_len && kind != CFGMOD_KIND_PHYSICAL) {
              ESP_LOGE(TAG, "Size mismatch for SET %s: expected %u, got %u", target_key, s_registry[kind].struct_size, data_in_len);
              rsp.status = ESP_ERR_INVALID_SIZE;
          } else {
              if (s_registry[kind].validate_fn) {
                  rsp.status = s_registry[kind].validate_fn((void*)data_in);
              }
              if (rsp.status == ESP_OK) {
                  if (s_registry[kind].set_fn) {
                      rsp.status = s_registry[kind].set_fn(data_in);
                  } else {
                      rsp.status = cfgmod_write_storage(kind, target_key, data_in, data_in_len);
                      if (rsp.status == ESP_OK && !base_key) {
                          update_active_mask(kind, hdr.item_id, true);
                      }
                  }
              }
          }
      }
end_set:;
  } else {
      rsp.status = ESP_ERR_INVALID_ARG;
  }

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

#define CFG_USB_RESP_BUF_SIZE 10240

static uint8_t s_cfg_resp_buf[CFG_USB_RESP_BUF_SIZE] __attribute__((aligned(8)));
static SemaphoreHandle_t s_cfg_resp_mutex = NULL;

bool cfg_usb_callback(comm_transport_t source, uint8_t *data, uint16_t data_len) {
    if (!s_cfg_resp_mutex || xSemaphoreTake(s_cfg_resp_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "cfg_usb_callback failed to acquire mutex");
        return false;
    }
    
    size_t out_len = 0;
    esp_err_t err = cfgmod_handle_usb_comm(data, data_len, s_cfg_resp_buf, &out_len, CFG_USB_RESP_BUF_SIZE);
    
    if (err == ESP_OK && out_len > 0) {
        comm_send_message(source, MODULE_CONFIG, s_cfg_resp_buf, out_len);
    }
    
    xSemaphoreGive(s_cfg_resp_mutex);
    return err == ESP_OK;
}

// Initialize NVS for cfg storage use.
esp_err_t cfg_init(void) {
  if (!s_cfg_resp_mutex) {
      s_cfg_resp_mutex = xSemaphoreCreateMutex();
  }

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
    cfg_combos_register(NULL);      // keyboard module re-registers with its callback in kb_combo_init()
    cfg_system_register();
    cfg_physical_register();
    cfg_ble_init();

    // Register comm callback for the CONFIG MODULE
    comm_register_module(MODULE_CONFIG, cfg_usb_callback);
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

  if (kind == CFGMOD_KIND_BLE_BOND) {
      return cfg_ble_bond_read_all(out_buf, inout_len);
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

esp_err_t cfgmod_delete_storage(cfgmod_kind_t kind, const char *key) {
  const char *ns, *nvs_key;
  char nvs_key_buf[16] = {0};
  esp_err_t err = resolve_ns_and_key(kind, key, &ns, &nvs_key, nvs_key_buf, sizeof(nvs_key_buf));
  if (err != ESP_OK) return err;

  nvs_handle_t handle;
  err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  err = nvs_erase_key(handle, nvs_key);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    ESP_LOGI(TAG, "NVS erase_key %s/%s success", ns, nvs_key);
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = ESP_OK; // Ignore if already doesn't exist
  } else {
    ESP_LOGW(TAG, "NVS erase_key %s/%s failed (err 0x%x)", ns, nvs_key, err);
  }
  
  nvs_close(handle);
  return err;
}

// Write a blob to NVS for the given kind/key.
esp_err_t cfgmod_write_storage(cfgmod_kind_t kind, const char *key,
                               const void *data, size_t len) {
  if (data == NULL || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (kind == CFGMOD_KIND_BLE_BOND) {
      return cfg_ble_bond_write_all(data, len);
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

  // Refresh the in-memory cache for registered kinds.  Config sync writes
  // via cfgmod_write_storage directly (not cfgmod_set_config), so without
  // this the cached state (e.g. cfg_system's ble_shared_addr, cfg_ble's
  // profile nonces) would stay stale after a sync and the new master would
  // compute the wrong BLE address.
  if (err == ESP_OK && kind < CFGMOD_KIND_MAX
      && s_registry[kind].registered && s_registry[kind].update_fn) {
      s_registry[kind].update_fn(key);
  }

  return err;
}

// Fetch a config struct from storage (applies defaults then tries NVS binary)
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

  // Pure binary load
  if (required_size == s_registry[kind].struct_size) {
    if (nvs_get_blob(handle, nvs_key, out_struct, &required_size) == ESP_OK) {
      ESP_LOGI(TAG, "NVS get_blob %s (len=%u) binary load successful", nvs_key, (unsigned)required_size);
      nvs_close(handle);
      return ESP_OK;
    }
  } else {
    ESP_LOGE(TAG, "NVS get_blob %s: size mismatch (expected %u, got %u). Returning defaults.", 
             nvs_key, s_registry[kind].struct_size, (unsigned)required_size);
  }

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
    // update_fn is now called inside cfgmod_write_storage.
    cfgmod_post_update_event(kind, key);
  }
  return err;
}
