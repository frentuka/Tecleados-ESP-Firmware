#include "cfg_layouts.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "cfg_storage_keys.h"
#include "cfgmod.h"
#include "kb_layout.h"
#include "event_bus.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "cfg_layouts"

static cfg_layer_t  s_dram_base;
static cfg_layer_t  s_dram_swap;
static uint8_t      s_swap_layer_idx = 0xFF; // Currently swapped layer
static cfg_layer_t *s_psram_cache = NULL;
static cfg_layout_index_t s_idx = {0};

/*
    Defaults: copy from compile-time keymaps[] in kb_layout.h
*/
static void layout_default(void *out_struct) {
  cfg_layer_t *l = (cfg_layer_t *)out_struct;
  memset(l, 0, sizeof(cfg_layer_t));
}

/*
    Serialize: { "keys": [[c0,c1,...],[c0,c1,...],...] }
*/
static cJSON *layout_serialize(const void *in_struct) {
  const cfg_layer_t *l = (const cfg_layer_t *)in_struct;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;

  cJSON *rows = cJSON_CreateArray();
  if (!rows) {
    cJSON_Delete(root);
    return NULL;
  }

  for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
    cJSON *row_arr = cJSON_CreateArray();
    if (!row_arr) {
      cJSON_Delete(root);
      return NULL;
    }
    for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
      cJSON_AddItemToArray(row_arr, cJSON_CreateNumber(l->keys[r][c]));
    }
    cJSON_AddItemToArray(rows, row_arr);
  }
  cJSON_AddItemToObject(root, "keys", rows);
  return root;
}

/*
    Deserialize: Parse { "keys": [[...],[...],...] }
*/
static bool layout_deserialize(cJSON *root, void *out_struct) {
  cfg_layer_t *l = (cfg_layer_t *)out_struct;
  cJSON *rows = cJSON_GetObjectItem(root, "keys");
  if (!cJSON_IsArray(rows))
    return false;

  int r = 0;
  cJSON *row_item;
  cJSON_ArrayForEach(row_item, rows) {
    if (r >= KB_MATRIX_ROW_COUNT)
      break;
    if (!cJSON_IsArray(row_item))
      return false;

    int c = 0;
    cJSON *col_item;
    cJSON_ArrayForEach(col_item, row_item) {
      if (c >= KB_MATRIX_COL_COUNT)
        break;
      if (cJSON_IsNumber(col_item)) {
        l->keys[r][c] = (uint16_t)col_item->valueint;
      }
      c++;
    }
    r++;
  }
  return true;
}

/*
    Update callback: when an external SET arrives via USB,
    reload the affected layer into cache.
*/
static void layout_update_cb(const char *key) {
  if (strncmp(key, "ly_", 3) == 0) {
    uint8_t layer_id = (uint8_t)atoi(key + 3);
    if (layer_id < CFG_LAYOUT_MAX_COUNT) {
      ESP_LOGI(TAG, "Reloading layer %d from NVS", layer_id);
      cfg_layer_t tmp;
      if (cfgmod_get_config(CFGMOD_KIND_LAYOUT, key, &tmp) == ESP_OK) {
        if (s_psram_cache) s_psram_cache[layer_id] = tmp;
        if (layer_id == 0) s_dram_base = tmp;
        if (layer_id == s_swap_layer_idx) s_dram_swap = tmp;
      }
    }
  } else if (strcmp(key, CFG_ST_LAYER_IDX) == 0) {
      size_t len = sizeof(s_idx);
      cfgmod_read_storage(CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX, &s_idx, &len);
  }
}

/*
    Registration
*/
void cfg_layouts_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_LAYOUT, layout_default, layout_deserialize,
                       layout_serialize, layout_update_cb,
                       sizeof(cfg_layer_t));
}

/*
    Load all layers from NVS into cache.
    Falls back to compile-time keymaps[] defaults if NVS is empty/corrupt.
*/
esp_err_t cfg_layout_load_all(void) {
  if (!s_psram_cache) {
    s_psram_cache = heap_caps_malloc(sizeof(cfg_layer_t) * CFG_LAYOUT_MAX_COUNT, MALLOC_CAP_SPIRAM);
    if (!s_psram_cache) {
      ESP_LOGE(TAG, "Failed to allocate layer cache in PSRAM!");
      return ESP_ERR_NO_MEM;
    }
    memset(s_psram_cache, 0, sizeof(cfg_layer_t) * CFG_LAYOUT_MAX_COUNT);
  }

  size_t idx_len = sizeof(s_idx);
  if (cfgmod_read_storage(CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX, &s_idx, &idx_len) != ESP_OK || idx_len != sizeof(cfg_layout_index_t)) {
      // First boot initialization
      memset(&s_idx, 0, sizeof(s_idx));
      s_idx.active_mask = 0x0001;
      strncpy(s_idx.names[0], "Base", CFG_LAYOUT_NAME_LEN);
      cfgmod_write_storage(CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX, &s_idx, sizeof(s_idx));
      ESP_LOGI(TAG, "Initialized default layout index");
  }

  // Enforce Base layer protection
  s_idx.active_mask |= 0x0001;
  strncpy(s_idx.names[0], "Base", CFG_LAYOUT_NAME_LEN);

  for (uint8_t i = 0; i < CFG_LAYOUT_MAX_COUNT; i++) {
    if (s_idx.active_mask & (1 << i)) {
        cfg_layer_t layer_data;
        if (i == 0) {
            memcpy(&layer_data, &keymaps_base, sizeof(cfg_layer_t));
        } else {
            for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
                for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
                    layer_data.keys[r][c] = KB_KEY_TRANSPARENT;
                }
            }
        }

        char key[16];
        snprintf(key, sizeof(key), CFG_ST_LAYER_FMT, i);
        
        cfg_layer_t tmp;
        size_t len = sizeof(tmp);
        if (cfgmod_read_storage(CFGMOD_KIND_LAYOUT, key, &tmp, &len) == ESP_OK
            && len == sizeof(cfg_layer_t)) {
          layer_data = tmp;
          ESP_LOGI(TAG, "Layer %d loaded from NVS", i);
        } else {
          ESP_LOGW(TAG, "Layer %d using defaults", i);
        }

        s_psram_cache[i] = layer_data;
        if (i == 0) s_dram_base = layer_data;
    }
  }
  return ESP_OK;
}

/*
    Fast action code lookup from cache (called from kb_manager hot loop)
*/
uint16_t cfg_layout_get_action_code(uint8_t row, uint8_t col, uint8_t layer) {
  if (row >= KB_MATRIX_ROW_COUNT || col >= KB_MATRIX_COL_COUNT) {
    return ACTION_CODE_NONE;
  }

  if (layer >= CFG_LAYOUT_MAX_COUNT || !cfg_layout_exists(layer)) {
    layer = 0;
  }

  uint16_t kc;
  
  if (layer == 0) {
    kc = s_dram_base.keys[row][col];
  } 
  else if (layer == s_swap_layer_idx) {
    kc = s_dram_swap.keys[row][col];
  }
  else {
    if (s_psram_cache) {
      s_dram_swap = s_psram_cache[layer];
      s_swap_layer_idx = layer;
      kc = s_dram_swap.keys[row][col];
    } else {
      kc = KB_KEY_TRANSPARENT; 
    }
  }

  // No fallback here. kb_layout_get_action_code orchestrates layer fallback.
  return kc;
}

/*
    Per-layer get (from cache, no NVS read)
*/
esp_err_t cfg_layout_get_layer(uint8_t layer, cfg_layer_t *out) {
  if (!out || layer >= CFG_LAYOUT_MAX_COUNT || !cfg_layout_exists(layer))
    return ESP_ERR_INVALID_ARG;
  
  if (s_psram_cache) {
    *out = s_psram_cache[layer];
  } else {
    if (layer == 0) *out = s_dram_base;
    else if (layer == s_swap_layer_idx) *out = s_dram_swap;
    else memset(out, 0, sizeof(cfg_layer_t));
  }
  return ESP_OK;
}

/*
    Per-layer set (updates cache + persists to NVS)
*/
esp_err_t cfg_layout_set_layer(uint8_t layer, const cfg_layer_t *in) {
  if (!in || layer >= CFG_LAYOUT_MAX_COUNT || !cfg_layout_exists(layer))
    return ESP_ERR_INVALID_ARG;

  char key[16];
  snprintf(key, sizeof(key), CFG_ST_LAYER_FMT, layer);

  esp_err_t err = cfgmod_set_config(CFGMOD_KIND_LAYOUT, key, in);
  if (err == ESP_OK) {
    if (s_psram_cache) s_psram_cache[layer] = *in;
    if (layer == 0) s_dram_base = *in;
    if (layer == s_swap_layer_idx) s_dram_swap = *in;
    ESP_LOGI(TAG, "Layer %d saved and cached", layer);
  }
  return err;
}

// ── Dynamic management ──

esp_err_t cfg_layout_create(const char *name, uint8_t *out_id) {
    if (!name || !out_id) return ESP_ERR_INVALID_ARG;
    
    int id = -1;
    for (int i = 1; i < CFG_LAYOUT_MAX_COUNT; i++) {
        if (!(s_idx.active_mask & (1 << i))) {
            id = i;
            break;
        }
    }
    if (id == -1) return ESP_ERR_NO_MEM;

    s_idx.active_mask |= (1 << id);
    strncpy(s_idx.names[id], name, CFG_LAYOUT_NAME_LEN);
    s_idx.names[id][CFG_LAYOUT_NAME_LEN - 1] = '\0';

    if (s_psram_cache) {
        for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
            for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
                s_psram_cache[id].keys[r][c] = KB_KEY_TRANSPARENT;
            }
        }
    }

    char key[16];
    snprintf(key, sizeof(key), CFG_ST_LAYER_FMT, id);
    
    cfgmod_write_storage(CFGMOD_KIND_LAYOUT, key, &s_psram_cache[id], sizeof(cfg_layer_t));
    cfgmod_write_storage(CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX, &s_idx, sizeof(s_idx));

    config_update_event_t ev = { .kind = (uint8_t)CFGMOD_KIND_LAYOUT };
    strlcpy(ev.key, CFG_ST_LAYER_IDX, sizeof(ev.key));
    esp_event_post(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, &ev, sizeof(ev), 0);

    *out_id = (uint8_t)id;
    return ESP_OK;
}

esp_err_t cfg_layout_delete(uint8_t id) {
    if (id == 0 || id >= CFG_LAYOUT_MAX_COUNT) return ESP_ERR_NOT_ALLOWED;
    if (!cfg_layout_exists(id)) return ESP_ERR_NOT_FOUND;

    s_idx.active_mask &= ~(1 << id);
    memset(s_idx.names[id], 0, CFG_LAYOUT_NAME_LEN);

    if (s_psram_cache) {
        memset(&s_psram_cache[id], 0, sizeof(cfg_layer_t));
    }

    char key[16];
    snprintf(key, sizeof(key), CFG_ST_LAYER_FMT, id);
    cfgmod_delete_storage(CFGMOD_KIND_LAYOUT, key);
    cfgmod_write_storage(CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX, &s_idx, sizeof(s_idx));

    if (id == s_swap_layer_idx) {
        s_swap_layer_idx = 0xFF;
        memset(&s_dram_swap, 0, sizeof(cfg_layer_t));
    }

    config_update_event_t ev = { .kind = (uint8_t)CFGMOD_KIND_LAYOUT };
    strlcpy(ev.key, CFG_ST_LAYER_IDX, sizeof(ev.key));
    esp_event_post(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, &ev, sizeof(ev), 0);

    return ESP_OK;
}

esp_err_t cfg_layout_rename(uint8_t id, const char *new_name) {
    if (id == 0 || id >= CFG_LAYOUT_MAX_COUNT || !new_name) return ESP_ERR_NOT_ALLOWED;
    if (!cfg_layout_exists(id)) return ESP_ERR_NOT_FOUND;

    strncpy(s_idx.names[id], new_name, CFG_LAYOUT_NAME_LEN);
    s_idx.names[id][CFG_LAYOUT_NAME_LEN - 1] = '\0';
    
    cfgmod_write_storage(CFGMOD_KIND_LAYOUT, CFG_ST_LAYER_IDX, &s_idx, sizeof(s_idx));

    config_update_event_t ev = { .kind = (uint8_t)CFGMOD_KIND_LAYOUT };
    strlcpy(ev.key, CFG_ST_LAYER_IDX, sizeof(ev.key));
    esp_event_post(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED, &ev, sizeof(ev), 0);

    return ESP_OK;
}

// ── Index accessors ──

uint8_t cfg_layout_get_count(void) {
    uint8_t count = 0;
    for (int i = 0; i < CFG_LAYOUT_MAX_COUNT; i++) {
        if (s_idx.active_mask & (1 << i)) count++;
    }
    return count;
}

bool cfg_layout_exists(uint8_t id) {
    if (id >= CFG_LAYOUT_MAX_COUNT) return false;
    return (s_idx.active_mask & (1 << id)) != 0;
}

const cfg_layout_index_t *cfg_layout_get_index(void) {
    return &s_idx;
}

// ── Serialization ──

cJSON *layouts_serialize_outline(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    
    cJSON *layouts = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "layouts", layouts);
    
    for (uint8_t i = 0; i < CFG_LAYOUT_MAX_COUNT; i++) {
        if (cfg_layout_exists(i)) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", i);
            cJSON_AddStringToObject(item, "name", s_idx.names[i]);
            cJSON_AddItemToArray(layouts, item);
        }
    }
    return root;
}

cJSON *layouts_serialize_single(uint8_t id) {
    if (!cfg_layout_exists(id)) return NULL;
    
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddStringToObject(root, "name", s_idx.names[id]);
    
    cfg_layer_t layer;
    if (cfg_layout_get_layer(id, &layer) != ESP_OK) {
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON *keys = cJSON_CreateArray();
    for (int r = 0; r < KB_MATRIX_ROW_COUNT; r++) {
        cJSON *row_arr = cJSON_CreateArray();
        for (int c = 0; c < KB_MATRIX_COL_COUNT; c++) {
            cJSON_AddItemToArray(row_arr, cJSON_CreateNumber(layer.keys[r][c]));
        }
        cJSON_AddItemToArray(keys, row_arr);
    }
    cJSON_AddItemToObject(root, "keys", keys);
    
    return root;
}

cJSON *layouts_serialize_limits(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "maxLayouts", CFG_LAYOUT_MAX_COUNT);
    return root;
}
