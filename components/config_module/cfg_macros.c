#include "cfg_macros.h"

#include "cfgmod.h"
#include <string.h>


void macros_default(void *out_struct) {
  cfg_macro_list_t *list = (cfg_macro_list_t *)out_struct;
  list->count = 0;
  memset(list->macros, 0, sizeof(list->macros));
}



esp_err_t macros_delete_single(uint16_t id, cfg_macro_index_t *idx) {
    if (id >= CFG_MACROS_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    
    // Unset from mask first
    idx->active_mask &= ~(UINT64_C(1) << id);
    esp_err_t err = cfgmod_write_storage(CFGMOD_KIND_MACRO, "mac_idx", idx, sizeof(cfg_macro_index_t));
    
    // We don't strictly *need* to erase it if it's not in the mask, but it's cleaner
    // Since NVS deleting is missing a wrapper in cfgmod, we just let it be orphaned for now until overwritten.
    return err;
}

void cfg_macros_register(void) {
    /*
     * Intentional no-op. The actual cfgmod_register_kind() call for
     * CFGMOD_KIND_MACRO is made by kb_macro_init() in kb_macro.c, which
     * owns the on_macros_updated callback. cfg_init() calls this function
     * only to keep the registration site visible alongside the other kinds.
     */
}

esp_err_t macros_load_all(cfg_macro_list_t *out_list) {
  if (!out_list) return ESP_ERR_INVALID_ARG;
  out_list->count = 0;
  
  cfg_macro_index_t idx = {0};
  size_t idx_len = sizeof(idx);
  if (cfgmod_read_storage(CFGMOD_KIND_MACRO, "mac_idx", &idx, &idx_len) != ESP_OK) {
      return ESP_OK; // No index yet, empty list
  }
  
  for (uint16_t i = 0; i < CFG_MACROS_MAX_COUNT; i++) {
      if ((idx.active_mask & (UINT64_C(1) << i))) {
          char key[16];
          snprintf(key, sizeof(key), "mac_%u", i);
          size_t len = sizeof(cfg_macro_t);
          if (cfgmod_read_storage(CFGMOD_KIND_MACRO, key, &out_list->macros[out_list->count], &len) == ESP_OK && len == sizeof(cfg_macro_t)) {
              out_list->count++;
          }
      }
  }
  return ESP_OK;
}
