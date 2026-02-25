#include "cfg_macros.h"
#include "cJSON.h"
#include "cfgmod.h"
#include <string.h>


static void macros_default(void *out_struct) {
  cfg_macro_t *m = (cfg_macro_t *)out_struct;
  m->event_count = 0;
  memset(m->events, 0, sizeof(m->events));
}

static bool macros_deserialize(cJSON *root, void *out_struct) {
  cfg_macro_t *m = (cfg_macro_t *)out_struct;
  cJSON *events = cJSON_GetObjectItem(root, "events");
  if (!cJSON_IsArray(events))
    return false;

  m->event_count = 0;
  cJSON *item;
  cJSON_ArrayForEach(item, events) {
    if (m->event_count >= CFG_MACRO_MAX_EVENTS)
      break;
    cJSON *t = cJSON_GetObjectItem(item, "t");
    cJSON *v = cJSON_GetObjectItem(item, "v");
    if (cJSON_IsNumber(t) && cJSON_IsNumber(v)) {
      m->events[m->event_count].type = (cfg_macro_event_type_t)t->valueint;
      m->events[m->event_count].value = (uint32_t)v->valueint;
      m->event_count++;
    }
  }
  return true;
}

static cJSON *macros_serialize(const void *in_struct) {
  const cfg_macro_t *m = (const cfg_macro_t *)in_struct;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;

  cJSON *events = cJSON_CreateArray();
  if (events) {
    for (size_t i = 0; i < m->event_count; i++) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "t", (double)m->events[i].type);
      cJSON_AddNumberToObject(item, "v", (double)m->events[i].value);
      cJSON_AddItemToArray(events, item);
    }
    cJSON_AddItemToObject(root, "events", events);
  }
  return root;
}

void cfg_macros_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_MACRO, macros_default, macros_deserialize,
                       macros_serialize, NULL, sizeof(cfg_macro_t));
}
