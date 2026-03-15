#include "cfg_layouts.h"

#include <stdbool.h>
#include <string.h>

#include "cfg_storage_keys.h"
#include "cfgmod.h"
#include "kb_layout.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "cfg_layouts"

// Memory Strategy:
// 1. All layers are cached in PSRAM (s_psram_cache).
// 2. Layer 0 (Base) is always in DRAM (s_dram_base) for hot-path speed.
// 3. One additional layer (s_swap_layer_idx) is cached in DRAM (s_dram_swap).
static cfg_layer_t  s_dram_base;
static cfg_layer_t  s_dram_swap;
static uint8_t      s_swap_layer_idx = 0xFF; // Currently swapped layer
static cfg_layer_t *s_psram_cache = NULL;

// NVS key names for each layer
static const char *s_layer_keys[KB_LAYER_COUNT] = {
    CFG_ST_LAYER_0,
    CFG_ST_LAYER_1,
    CFG_ST_LAYER_2,
    CFG_ST_LAYER_3,
};

/*
    Defaults: copy from compile-time keymaps[] in kb_layout.h
*/
static void layout_default(void *out_struct) {
  // This fills a cfg_layer_t with zeros as a safe fallback.
  // The real per-layer defaults are applied in cfg_layout_load_all().
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
  for (int i = 0; i < KB_LAYER_COUNT; i++) {
    if (strcmp(key, s_layer_keys[i]) == 0) {
      ESP_LOGI(TAG, "Reloading layer %d from NVS", i);
      cfg_layer_t tmp;
      if (cfgmod_get_config(CFGMOD_KIND_LAYOUT, s_layer_keys[i], &tmp) == ESP_OK) {
        if (s_psram_cache) s_psram_cache[i] = tmp;
        if (i == 0) s_dram_base = tmp;
        if (i == s_swap_layer_idx) s_dram_swap = tmp;
      }
      return;
    }
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
    On first boot, also persists the defaults to NVS.
*/
esp_err_t cfg_layout_load_all(void) {
  if (!s_psram_cache) {
    s_psram_cache = heap_caps_malloc(sizeof(cfg_layer_t) * KB_LAYER_COUNT, MALLOC_CAP_SPIRAM);
    if (!s_psram_cache) {
      ESP_LOGE(TAG, "Failed to allocate layer cache in PSRAM!");
      return ESP_ERR_NO_MEM;
    }
  }

  for (int i = 0; i < KB_LAYER_COUNT; i++) {
    // Start with the compiled default for this layer
    cfg_layer_t layer_data;
    memcpy(&layer_data, &keymaps[i], sizeof(cfg_layer_t));

    // Try to load from NVS (if it exists and is valid, overwrites the default)
    cfg_layer_t tmp;
    memcpy(&tmp, &keymaps[i], sizeof(cfg_layer_t)); // default first
    esp_err_t err = cfgmod_get_config(CFGMOD_KIND_LAYOUT, s_layer_keys[i], &tmp);
    if (err == ESP_OK) {
      layer_data = tmp;
      ESP_LOGI(TAG, "Layer %d loaded from NVS", i);
    } else {
      ESP_LOGW(TAG, "Layer %d loaded from DEFAULTS", i);
    }

    s_psram_cache[i] = layer_data;
    if (i == 0) s_dram_base = layer_data;
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

  if (layer >= KB_LAYER_COUNT) {
    layer = KB_LAYER_BASE;
  }

  uint16_t kc;
  
  // Fast Path: Layer 0
  if (layer == 0) {
    kc = s_dram_base.keys[row][col];
  } 
  // Fast Path: Current Swap
  else if (layer == s_swap_layer_idx) {
    kc = s_dram_swap.keys[row][col];
  }
  // Slow Path: Swap from PSRAM
  else {
    if (s_psram_cache) {
      s_dram_swap = s_psram_cache[layer];
      s_swap_layer_idx = layer;
      kc = s_dram_swap.keys[row][col];
    } else {
      kc = KB_KEY_TRANSPARENT; // Should not happen
    }
  }

  if (kc == KB_KEY_TRANSPARENT && layer != 0) {
    kc = s_dram_base.keys[row][col];
  }
  return kc;
}

/*
    Per-layer get (from cache, no NVS read)
*/
esp_err_t cfg_layout_get_layer(uint8_t layer, cfg_layer_t *out) {
  if (!out || layer >= KB_LAYER_COUNT)
    return ESP_ERR_INVALID_ARG;
  
  if (s_psram_cache) {
    *out = s_psram_cache[layer];
  } else {
    // Fallback for safety if PSRAM failed
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
  if (!in || layer >= KB_LAYER_COUNT)
    return ESP_ERR_INVALID_ARG;

  esp_err_t err = cfgmod_set_config(CFGMOD_KIND_LAYOUT, s_layer_keys[layer], in);
  if (err == ESP_OK) {
    if (s_psram_cache) s_psram_cache[layer] = *in;
    if (layer == 0) s_dram_base = *in;
    if (layer == s_swap_layer_idx) s_dram_swap = *in;
    ESP_LOGI(TAG, "Layer %d saved and cached", layer);
  }
  return err;
}
