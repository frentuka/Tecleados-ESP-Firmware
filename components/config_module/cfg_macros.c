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
    
    if (cJSON_IsNumber(id)) m->id = (uint16_t)id->valueint;
    if (cJSON_IsString(name)) strncpy(m->name, name->valuestring, sizeof(m->name) - 1);
    
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

void cfg_macros_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_MACRO, macros_default, macros_deserialize,
                       macros_serialize, NULL, sizeof(cfg_macro_list_t));
}
