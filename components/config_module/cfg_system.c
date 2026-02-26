#include "cfg_system.h"
#include "cJSON.h"
#include "cfgmod.h"
#include <string.h>


static cfg_system_t s_sys;
static bool s_sys_loaded = false;

static void sys_update_cb(const char *key) { s_sys_loaded = false; }

static void sys_default(void *out_struct) {
  cfg_system_t *s = (cfg_system_t *)out_struct;
  strcpy(s->device_name, "Antigravity KB");
  s->sleep_timeout_ms = 300000; // 5 mins
  s->rgb_brightness = 255;
  s->bluetooth_enabled = true;
}

static bool sys_deserialize(cJSON *root, void *out_struct) {
  cfg_system_t *s = (cfg_system_t *)out_struct;
  cJSON *name = cJSON_GetObjectItem(root, "name");
  cJSON *sleep = cJSON_GetObjectItem(root, "sleep");
  cJSON *rgb = cJSON_GetObjectItem(root, "rgb_brightness");
  cJSON *bt = cJSON_GetObjectItem(root, "bt_en");

  if (cJSON_IsString(name)) {
    strncpy(s->device_name, name->valuestring, sizeof(s->device_name) - 1);
    s->device_name[sizeof(s->device_name) - 1] = '\0';
  }
  if (cJSON_IsNumber(sleep))
    s->sleep_timeout_ms = (uint32_t)sleep->valuedouble;
  if (cJSON_IsNumber(rgb))
    s->rgb_brightness = (uint8_t)rgb->valueint;
  if (cJSON_IsBool(bt))
    s->bluetooth_enabled = cJSON_IsTrue(bt);

  return true;
}

static cJSON *sys_serialize(const void *in_struct) {
  const cfg_system_t *s = (const cfg_system_t *)in_struct;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;

  cJSON_AddStringToObject(root, "name", s->device_name);
  cJSON_AddNumberToObject(root, "sleep", (double)s->sleep_timeout_ms);
  cJSON_AddNumberToObject(root, "rgb_brightness", (double)s->rgb_brightness);
  cJSON_AddBoolToObject(root, "bt_en", s->bluetooth_enabled);

  return root;
}

void cfg_system_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_SYSTEM, sys_default, sys_deserialize,
                       sys_serialize, sys_update_cb, sizeof(cfg_system_t));
}

esp_err_t cfg_system_get(cfg_system_t *out_sys) {
  if (!out_sys)
    return ESP_ERR_INVALID_ARG;
  if (!s_sys_loaded) {
    esp_err_t err = cfgmod_get_config(CFGMOD_KIND_SYSTEM, "sys", &s_sys);
    if (err != ESP_OK)
      return err;
    s_sys_loaded = true;
  }
  *out_sys = s_sys;
  return ESP_OK;
}

esp_err_t cfg_system_set(const cfg_system_t *in_sys) {
  if (!in_sys)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = cfgmod_set_config(CFGMOD_KIND_SYSTEM, "sys", in_sys);
  if (err == ESP_OK) {
    s_sys = *in_sys;
    s_sys_loaded = true;
  }
  return err;
}
