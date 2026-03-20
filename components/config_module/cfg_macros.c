#include "cfg_macros.h"
#include "cJSON.h"
#include "cfgmod.h"
#include <string.h>


void macros_default(void *out_struct) {
  cfg_macro_list_t *list = (cfg_macro_list_t *)out_struct;
  list->count = 0;
  memset(list->macros, 0, sizeof(list->macros));
}

// Deserialize a single macro from JSON
bool macros_deserialize(cJSON *root, void *out_struct) {
  cfg_macro_t *m = (cfg_macro_t *)out_struct;
  memset(m, 0, sizeof(cfg_macro_t));
  
  cJSON *macro_item = cJSON_GetObjectItem(root, "macros");
  if (!macro_item) {
      if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
          macro_item = cJSON_GetArrayItem(root, 0);
      } else {
          macro_item = root; // Assume root is the macro object
      }
  } else if (cJSON_IsArray(macro_item) && cJSON_GetArraySize(macro_item) > 0) {
      macro_item = cJSON_GetArrayItem(macro_item, 0);
  }

  cJSON *id = cJSON_GetObjectItem(macro_item, "id");
  cJSON *name = cJSON_GetObjectItem(macro_item, "name");
  cJSON *elements = cJSON_GetObjectItem(macro_item, "elements");
  cJSON *exec_mode = cJSON_GetObjectItem(macro_item, "execMode");
  cJSON *stack_max = cJSON_GetObjectItem(macro_item, "stackMax");
  cJSON *repeat_count = cJSON_GetObjectItem(macro_item, "repeatCount");
  
  if (!cJSON_IsNumber(id)) return false;
  m->id = (uint16_t)id->valueint;
  if (m->id >= CFG_MACROS_MAX_COUNT) return false;

  if (cJSON_IsString(name)) strncpy(m->name, name->valuestring, sizeof(m->name) - 1);
  m->exec_mode = cJSON_IsNumber(exec_mode) ? (uint8_t)exec_mode->valueint : 0;
  m->stack_max = cJSON_IsNumber(stack_max) ? (uint8_t)stack_max->valueint : 1;
  m->repeat_count = cJSON_IsNumber(repeat_count) ? (uint8_t)repeat_count->valueint : 1;
  
  if (cJSON_IsArray(elements)) {
    cJSON *el;
    cJSON_ArrayForEach(el, elements) {
      if (m->event_count >= CFG_MACRO_MAX_EVENTS) break;
      
      cJSON *type = cJSON_GetObjectItem(el, "type");
      if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "key") == 0) {
          m->events[m->event_count].type = MACRO_EVT_KEY_TAP;
          
          cJSON *action = cJSON_GetObjectItem(el, "action");
          if (cJSON_IsString(action)) {
            if (strcmp(action->valuestring, "press") == 0) {
              m->events[m->event_count].type = MACRO_EVT_KEY_PRESS;
            } else if (strcmp(action->valuestring, "release") == 0) {
              m->events[m->event_count].type = MACRO_EVT_KEY_RELEASE;
            }
          }
          
          cJSON *key = cJSON_GetObjectItem(el, "key");
          if (cJSON_IsNumber(key)) m->events[m->event_count].value = key->valueint;
          
          cJSON *inline_sleep = cJSON_GetObjectItem(el, "inlineSleep");
          if (cJSON_IsNumber(inline_sleep)) m->events[m->event_count].delay_ms = inline_sleep->valueint;
          
          cJSON *press_time = cJSON_GetObjectItem(el, "pressTime");
          if (cJSON_IsNumber(press_time)) {
              m->events[m->event_count].press_duration_ms = press_time->valueint;
          } else {
              m->events[m->event_count].press_duration_ms = 10; // Default 10ms if missing
          }
          
          m->event_count++;
        } else if (strcmp(type->valuestring, "sleep") == 0) {
          m->events[m->event_count].type = MACRO_EVT_DELAY_MS;
          cJSON *dur = cJSON_GetObjectItem(el, "duration");
          if (cJSON_IsNumber(dur)) m->events[m->event_count].value = dur->valueint;
          m->event_count++;
        }
      }
    }
  }
  return true;
}

cJSON *macros_serialize(const void *in_struct) {
    /*
     * Always returns NULL. Individual macros are serialized via
     * macros_serialize_single(). This stub exists only to satisfy the
     * cfgmod_serialize_fn signature required by cfgmod_register_kind().
     * The generic USB GET path for macros is handled by the custom block
     * in cfgmod_handle_usb_comm() before reaching the generic handler.
     */
    (void)in_struct;
    return NULL;
}

static cJSON *serialize_single_macro_to_json(const cfg_macro_t *m) {
    cJSON *macro_item = cJSON_CreateObject();
    cJSON_AddNumberToObject(macro_item, "id", m->id);
    cJSON_AddStringToObject(macro_item, "name", m->name);
    cJSON_AddNumberToObject(macro_item, "execMode", m->exec_mode);
    if (m->exec_mode == MACRO_EXEC_ONCE_STACK_N) {
      cJSON_AddNumberToObject(macro_item, "stackMax", m->stack_max);
    }
    if (m->exec_mode == MACRO_EXEC_BURST_N) {
      cJSON_AddNumberToObject(macro_item, "repeatCount", m->repeat_count);
    }
    
    cJSON *elements = cJSON_CreateArray();
    for (size_t j = 0; j < m->event_count; j++) {
      cJSON *el = cJSON_CreateObject();
      if (m->events[j].type == MACRO_EVT_DELAY_MS) {
          cJSON_AddStringToObject(el, "type", "sleep");
          cJSON_AddNumberToObject(el, "duration", m->events[j].value);
      } else {
          cJSON_AddStringToObject(el, "type", "key");
          cJSON_AddNumberToObject(el, "key", m->events[j].value);
          
          if (m->events[j].type == MACRO_EVT_KEY_PRESS) {
              cJSON_AddStringToObject(el, "action", "press");
          } else if (m->events[j].type == MACRO_EVT_KEY_RELEASE) {
              cJSON_AddStringToObject(el, "action", "release");
          } else {
              cJSON_AddStringToObject(el, "action", "tap");
          }
          if (m->events[j].type == MACRO_EVT_KEY_TAP) {
              cJSON_AddNumberToObject(el, "pressTime", m->events[j].press_duration_ms);
          }
          if (m->events[j].delay_ms > 0) {
              cJSON_AddNumberToObject(el, "inlineSleep", m->events[j].delay_ms);
          }
      }
      cJSON_AddItemToArray(elements, el);
    }
    cJSON_AddItemToObject(macro_item, "elements", elements);
    return macro_item;
}

cJSON *macros_serialize_outline(const cfg_macro_index_t *idx) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;

  cJSON *macros_arr = cJSON_CreateArray();
  
  for (uint16_t i = 0; i < CFG_MACROS_MAX_COUNT; i++) {
      if ((idx->active_mask & (UINT64_C(1) << i))) {
          char key[16];
          snprintf(key, sizeof(key), "mac_%u", i);
          
          cfg_macro_t *temp = malloc(sizeof(cfg_macro_t));
          if (!temp) continue;
          
          size_t len = sizeof(cfg_macro_t);
          if (cfgmod_read_storage(CFGMOD_KIND_MACRO, key, temp, &len) == ESP_OK && len == sizeof(cfg_macro_t)) {
                cJSON *macro_item = cJSON_CreateObject();
                cJSON_AddNumberToObject(macro_item, "id", temp->id);
                cJSON_AddStringToObject(macro_item, "name", temp->name);
                cJSON_AddNumberToObject(macro_item, "execMode", temp->exec_mode);
                if (temp->exec_mode == MACRO_EXEC_ONCE_STACK_N) {
                  cJSON_AddNumberToObject(macro_item, "stackMax", temp->stack_max);
                }
                if (temp->exec_mode == MACRO_EXEC_BURST_N) {
                  cJSON_AddNumberToObject(macro_item, "repeatCount", temp->repeat_count);
                }
                cJSON_AddItemToArray(macros_arr, macro_item);
          }
          free(temp);
      }
  }

  cJSON_AddItemToObject(root, "macros", macros_arr);
  return root;
}

cJSON *macros_serialize_single(uint16_t id, const cfg_macro_index_t *idx) {
  if (id < CFG_MACROS_MAX_COUNT && (idx->active_mask & (UINT64_C(1) << id))) {
      char key[16];
      snprintf(key, sizeof(key), "mac_%u", id);
      
      cfg_macro_t *temp = malloc(sizeof(cfg_macro_t));
      if (!temp) return NULL;
      
      size_t len = sizeof(cfg_macro_t);
      if (cfgmod_read_storage(CFGMOD_KIND_MACRO, key, temp, &len) == ESP_OK && len == sizeof(cfg_macro_t)) {
          cJSON *res = serialize_single_macro_to_json(temp);
          free(temp);
          return res;
      }
      free(temp);
  }

  // Not found
  cJSON *empty = cJSON_CreateObject();
  cJSON_AddNumberToObject(empty, "id", id);
  cJSON_AddStringToObject(empty, "name", "");
  cJSON_AddItemToObject(empty, "elements", cJSON_CreateArray());
  return empty;
}

cJSON *macros_serialize_limits(void) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;
  cJSON_AddNumberToObject(root, "maxEvents", CFG_MACRO_MAX_EVENTS);
  cJSON_AddNumberToObject(root, "maxMacros", CFG_MACROS_MAX_COUNT);
  return root;
}

esp_err_t macros_upsert_single(cJSON *macro_json, cfg_macro_index_t *idx) {
  cfg_macro_t *temp = malloc(sizeof(cfg_macro_t));
  if (!temp) return ESP_ERR_NO_MEM;

  if (!macros_deserialize(macro_json, temp)) {
    free(temp);
    return ESP_ERR_INVALID_ARG;
  }
  
  if (temp->id >= CFG_MACROS_MAX_COUNT) {
    free(temp);
    return ESP_ERR_INVALID_ARG;
  }

  // Save the macro directly
  char key[16];
  snprintf(key, sizeof(key), "mac_%u", temp->id);
  
  esp_err_t err = cfgmod_write_storage(CFGMOD_KIND_MACRO, key, temp, sizeof(cfg_macro_t));
  if (err == ESP_OK) {
      // Update index
      idx->active_mask |= (UINT64_C(1) << temp->id);
      cfgmod_write_storage(CFGMOD_KIND_MACRO, "mac_idx", idx, sizeof(cfg_macro_index_t));
  }
  free(temp);
  return err;
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
