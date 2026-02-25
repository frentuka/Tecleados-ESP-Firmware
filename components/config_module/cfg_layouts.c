#include "cfg_layouts.h"

#include <stdbool.h>
#include <string.h>

#include "cfg_storage_keys.h"
#include "cfgmod.h"

#include "cJSON.h"

static cfg_layout_t s_layout;
static bool s_layout_loaded = false;

static void layout_update_cb(const char *key) {
  if (strcmp(key, CFG_ST_LAYOUT1) == 0) {
    s_layout_loaded = false;
  }
}

static void layout_default(void *out_struct) {
  cfg_layout_t *l = (cfg_layout_t *)out_struct;
  memset(l, 0, sizeof(cfg_layout_t));
  l->key_count = CFG_LAYOUT_MAX_KEYS;
}

static bool layout_deserialize(cJSON *root, void *out_struct) {
  cfg_layout_t *l = (cfg_layout_t *)out_struct;
  cJSON *kcs = cJSON_GetObjectItem(root, "keycodes");
  if (!cJSON_IsArray(kcs))
    return false;

  size_t count = 0;
  cJSON *item;
  cJSON_ArrayForEach(item, kcs) {
    if (count >= CFG_LAYOUT_MAX_KEYS)
      break;
    if (cJSON_IsNumber(item)) {
      l->keycodes[count++] = (uint16_t)item->valueint;
    }
  }
  l->key_count = CFG_LAYOUT_MAX_KEYS;
  return true;
}

static cJSON *layout_serialize(const void *in_struct) {
  const cfg_layout_t *l = (const cfg_layout_t *)in_struct;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;

  cJSON *kcs = cJSON_CreateArray();
  if (kcs) {
    for (size_t i = 0; i < l->key_count; i++) {
      cJSON_AddItemToArray(kcs, cJSON_CreateNumber(l->keycodes[i]));
    }
    cJSON_AddItemToObject(root, "keycodes", kcs);
  }
  return root;
}

void cfg_layouts_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_LAYOUT, layout_default, layout_deserialize,
                       layout_serialize, layout_update_cb,
                       sizeof(cfg_layout_t));
}

esp_err_t cfg_layout_get(cfg_layout_t *out_layout) {
  if (!out_layout)
    return ESP_ERR_INVALID_ARG;
  if (!s_layout_loaded) {
    esp_err_t err =
        cfgmod_get_config(CFGMOD_KIND_LAYOUT, CFG_ST_LAYOUT1, &s_layout);
    if (err != ESP_OK)
      return err;
    s_layout_loaded = true;
  }
  *out_layout = s_layout;
  return ESP_OK;
}

esp_err_t cfg_layout_set(const cfg_layout_t *layout) {
  if (!layout)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = cfgmod_set_config(CFGMOD_KIND_LAYOUT, CFG_ST_LAYOUT1, layout);
  if (err == ESP_OK) {
    s_layout = *layout;
    s_layout_loaded = true;
  }
  return err;
}
