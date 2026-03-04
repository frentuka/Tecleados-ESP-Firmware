#include "cfg_macros.h"
#include "cJSON.h"
#include "cfgmod.h"
#include <string.h>


void macros_default(void *out_struct) {
  cfg_macro_list_t *list = (cfg_macro_list_t *)out_struct;
  list->count = 0;
  memset(list->macros, 0, sizeof(list->macros));
}

bool macros_deserialize(cJSON *root, void *out_struct) {
  cfg_macro_list_t *list = (cfg_macro_list_t *)out_struct;
  
  // The web UI sends { "macros": [ ... ] }
  cJSON *macros_arr = cJSON_GetObjectItem(root, "macros");
  if (!cJSON_IsArray(macros_arr)) {
      // Fallback: maybe it's just the array itself
      if (cJSON_IsArray(root)) {
          macros_arr = root;
      } else {
          return false;
      }
  }

  list->count = 0;
  cJSON *macro_item;
  cJSON_ArrayForEach(macro_item, macros_arr) {
    if (list->count >= CFG_MACROS_MAX_COUNT) break;
    
    cfg_macro_t *m = &list->macros[list->count];
    memset(m, 0, sizeof(cfg_macro_t));
    
    cJSON *id = cJSON_GetObjectItem(macro_item, "id");
    cJSON *name = cJSON_GetObjectItem(macro_item, "name");
    cJSON *elements = cJSON_GetObjectItem(macro_item, "elements");
    cJSON *exec_mode = cJSON_GetObjectItem(macro_item, "execMode");
    cJSON *stack_max = cJSON_GetObjectItem(macro_item, "stackMax");
    cJSON *repeat_count = cJSON_GetObjectItem(macro_item, "repeatCount");
    
    if (cJSON_IsNumber(id)) m->id = (uint16_t)id->valueint;
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
                m->events[m->event_count].type = MACRO_EVT_KEY_TAP; // Default
                
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
    list->count++;
  }
  return true;
}

cJSON *macros_serialize(const void *in_struct) {
  const cfg_macro_list_t *list = (const cfg_macro_list_t *)in_struct;
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;

  cJSON *macros_arr = cJSON_CreateArray();
  for (size_t i = 0; i < list->count; i++) {
    const cfg_macro_t *m = &list->macros[i];
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
      }
      cJSON_AddItemToArray(elements, el);
    }
    cJSON_AddItemToObject(macro_item, "elements", elements);
    cJSON_AddItemToArray(macros_arr, macro_item);
  }
  cJSON_AddItemToObject(root, "macros", macros_arr);
  return root;
}

cJSON *macros_serialize_outline(const cfg_macro_list_t *list) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;

  cJSON *macros_arr = cJSON_CreateArray();
  for (size_t i = 0; i < list->count; i++) {
    const cfg_macro_t *m = &list->macros[i];
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
    // Note: No elements array is added here.
    cJSON_AddItemToArray(macros_arr, macro_item);
  }
  cJSON_AddItemToObject(root, "macros", macros_arr);
  return root;
}

cJSON *macros_serialize_single(uint16_t id, const cfg_macro_list_t *list) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->macros[i].id == id) {
      const cfg_macro_t *m = &list->macros[i];
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
        }
        cJSON_AddItemToArray(elements, el);
      }
      cJSON_AddItemToObject(macro_item, "elements", elements);
      return macro_item; // Returns just the macro object, not { "macros": [...] }
    }
  }
  // If not found, create an empty macro or return null? Returning an empty object is safer.
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

// Deserialize a single macro JSON object into a cfg_macro_t
static bool deserialize_single_macro(cJSON *macro_item, cfg_macro_t *m) {
  memset(m, 0, sizeof(cfg_macro_t));
  
  cJSON *id = cJSON_GetObjectItem(macro_item, "id");
  cJSON *name = cJSON_GetObjectItem(macro_item, "name");
  cJSON *elements = cJSON_GetObjectItem(macro_item, "elements");
  cJSON *exec_mode = cJSON_GetObjectItem(macro_item, "execMode");
  cJSON *stack_max = cJSON_GetObjectItem(macro_item, "stackMax");
  cJSON *repeat_count = cJSON_GetObjectItem(macro_item, "repeatCount");
  
  if (!cJSON_IsNumber(id)) return false;
  m->id = (uint16_t)id->valueint;
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

void macros_upsert_single(cJSON *macro_json, cfg_macro_list_t *list) {
  cfg_macro_t temp;
  if (!deserialize_single_macro(macro_json, &temp)) return;
  
  // Try to find existing macro with same ID
  for (size_t i = 0; i < list->count; i++) {
    if (list->macros[i].id == temp.id) {
      list->macros[i] = temp;
      return;
    }
  }
  
  // Not found — append if there's room
  if (list->count < CFG_MACROS_MAX_COUNT) {
    list->macros[list->count] = temp;
    list->count++;
  }
}

void macros_delete_single(uint16_t id, cfg_macro_list_t *list) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->macros[i].id == id) {
      // Shift remaining macros down
      for (size_t j = i; j < list->count - 1; j++) {
        list->macros[j] = list->macros[j + 1];
      }
      list->count--;
      return;
    }
  }
}

void cfg_macros_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_MACRO, macros_default, macros_deserialize,
                       macros_serialize, NULL, sizeof(cfg_macro_list_t));
}
