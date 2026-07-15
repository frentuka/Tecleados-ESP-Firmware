#include "cfg_system.h"
#include "cJSON.h"
#include "cfgmod.h"
#include <string.h>
#include <stdio.h>


// Per-device identity fields — stored in "sys_id", never synced to the other half.
// Keeps split_variant and split_mirror_cols independent on each half.
// device_name is purposefully EXCLUDED here so it synchronizes across split halves.
typedef struct __attribute__((packed)) {
  bool is_split;
  bool split_mirror_cols;
  char split_variant[16];
} cfg_sys_id_t;

static cfg_system_t s_sys;
static bool s_sys_loaded = false;

static void sys_update_cb(const char *key) { s_sys_loaded = false; }

// Thin wrappers with void* signatures so they can be registered as cfgmod get/set hooks.
static esp_err_t sys_get_any(void *out)        { return cfg_system_get((cfg_system_t *)out); }
static esp_err_t sys_set_any(const void *in)   { return cfg_system_set((const cfg_system_t *)in); }

// Overlay the per-device identity fields from "sys_id" onto *s.
// Falls back silently if "sys_id" doesn't exist yet (fresh device or legacy).
static void sys_apply_local_id(cfg_system_t *s) {
  cfg_sys_id_t id;
  size_t len = sizeof(id);
  if (cfgmod_read_storage(CFGMOD_KIND_SYSTEM, "sys_id", &id, &len) == ESP_OK
      && len == sizeof(cfg_sys_id_t)) {
    s->is_split        = id.is_split;
    s->split_mirror_cols = id.split_mirror_cols;
    memcpy(s->split_variant,  id.split_variant,  sizeof(s->split_variant));
  }
}

static void sys_default(void *out_struct) {
  cfg_system_t *s = (cfg_system_t *)out_struct;
  strncpy(s->device_name, "Tecleados MK1", sizeof(s->device_name) - 1);
  s->device_name[sizeof(s->device_name) - 1] = '\0';
  s->sleep_timeout_ms = 300000; // 5 mins
  s->rgb_brightness = 255;
  s->bluetooth_enabled = false;
  s->is_split = false;
  s->split_mirror_cols = false;
  s->split_variant[0] = '\0';
  s->ble_shared_name[0] = '\0';
  memset(s->ble_shared_addr, 0, sizeof(s->ble_shared_addr));
  s->transparent_stack_fallback = false; // Direct-to-Base by default
}

static bool sys_deserialize(cJSON *root, void *out_struct) {
  cfg_system_t *s = (cfg_system_t *)out_struct;
  cJSON *name       = cJSON_GetObjectItem(root, "name");
  cJSON *sleep      = cJSON_GetObjectItem(root, "sleep");
  cJSON *rgb        = cJSON_GetObjectItem(root, "rgb_brightness");
  cJSON *bt         = cJSON_GetObjectItem(root, "bt_en");
  cJSON *is_split   = cJSON_GetObjectItem(root, "is_split");
  cJSON *mirror     = cJSON_GetObjectItem(root, "split_mirror_cols");
  cJSON *variant    = cJSON_GetObjectItem(root, "split_variant");
  cJSON *ble_name   = cJSON_GetObjectItem(root, "ble_shared_name");
  cJSON *ble_addr   = cJSON_GetObjectItem(root, "ble_shared_addr");

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
  if (cJSON_IsBool(is_split))
    s->is_split = cJSON_IsTrue(is_split);
  if (cJSON_IsBool(mirror))
    s->split_mirror_cols = cJSON_IsTrue(mirror);
  if (cJSON_IsString(variant)) {
    strncpy(s->split_variant, variant->valuestring, sizeof(s->split_variant) - 1);
    s->split_variant[sizeof(s->split_variant) - 1] = '\0';
  }
  if (cJSON_IsString(ble_name)) {
    strncpy(s->ble_shared_name, ble_name->valuestring, sizeof(s->ble_shared_name) - 1);
    s->ble_shared_name[sizeof(s->ble_shared_name) - 1] = '\0';
  }
  if (cJSON_IsString(ble_addr) && strlen(ble_addr->valuestring) == 17) {
    unsigned int b[6];
    if (sscanf(ble_addr->valuestring, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
      for (int i = 0; i < 6; i++) s->ble_shared_addr[i] = (uint8_t)b[i];
    }
  }
  
  cJSON *fallback = cJSON_GetObjectItem(root, "transparent_stack_fallback");
  if (cJSON_IsBool(fallback)) {
      s->transparent_stack_fallback = cJSON_IsTrue(fallback);
  }

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
  cJSON_AddBoolToObject(root, "is_split", s->is_split);
  cJSON_AddBoolToObject(root, "split_mirror_cols", s->split_mirror_cols);
  cJSON_AddStringToObject(root, "split_variant", s->split_variant);
  cJSON_AddStringToObject(root, "ble_shared_name", s->ble_shared_name);
  // Serialize zero address as empty string so the UI can tell "not configured"
  // from a real address. All-zero address is not a valid static random address anyway.
  bool addr_nonzero = false;
  for (int i = 0; i < 6; i++) { if (s->ble_shared_addr[i]) { addr_nonzero = true; break; } }
  if (addr_nonzero) {
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             s->ble_shared_addr[0], s->ble_shared_addr[1], s->ble_shared_addr[2],
             s->ble_shared_addr[3], s->ble_shared_addr[4], s->ble_shared_addr[5]);
    cJSON_AddStringToObject(root, "ble_shared_addr", addr_str);
  } else {
    cJSON_AddStringToObject(root, "ble_shared_addr", "");
  }
  cJSON_AddBoolToObject(root, "transparent_stack_fallback", s->transparent_stack_fallback);

  return root;
}

void cfg_system_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_SYSTEM, sys_default, sys_deserialize,
                       sys_serialize, sys_update_cb, sizeof(cfg_system_t));

  // Route USB GET/SET for the system config through cfg_system_get/set so that:
  // - GET: returns the sys_id-overlaid values (each half sees its own identity)
  // - SET: updates sys_id alongside sys (so explicit user edits survive future syncs)
  cfgmod_register_get_set(CFGMOD_KIND_SYSTEM, sys_get_any, sys_set_any);

  // Bootstrap "sys_id" on first run or after a firmware upgrade.
  // This device's identity (device_name, split_variant, etc.) must survive
  // "sys" sync overwrites from the master.  If "sys_id" doesn't exist yet,
  // seed it from the current "sys" so the values are locked in before the
  // first sync can arrive and overwrite "sys".
  cfg_sys_id_t id;
  size_t id_len = sizeof(id);
  if (cfgmod_read_storage(CFGMOD_KIND_SYSTEM, "sys_id", &id, &id_len) != ESP_OK
      || id_len != sizeof(cfg_sys_id_t)) {
    cfg_system_t sys;
    cfgmod_get_config(CFGMOD_KIND_SYSTEM, "sys", &sys);
    id.is_split          = sys.is_split;
    id.split_mirror_cols = sys.split_mirror_cols;
    memcpy(id.split_variant, sys.split_variant, sizeof(id.split_variant));
    cfgmod_write_storage(CFGMOD_KIND_SYSTEM, "sys_id", &id, sizeof(id));
  }
}

esp_err_t cfg_system_get(cfg_system_t *out_sys) {
  if (!out_sys)
    return ESP_ERR_INVALID_ARG;
  if (!s_sys_loaded) {
    esp_err_t err = cfgmod_get_config(CFGMOD_KIND_SYSTEM, "sys", &s_sys);
    if (err != ESP_OK)
      return err;
    // Always overlay per-device identity; survives sync overwrites of "sys".
    sys_apply_local_id(&s_sys);
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
    // Persist per-device identity separately so a future "sys" sync from the
    // master cannot overwrite this half's hardware variant, etc.
    cfg_sys_id_t id;
    id.is_split          = in_sys->is_split;
    id.split_mirror_cols = in_sys->split_mirror_cols;
    memcpy(id.split_variant, in_sys->split_variant, sizeof(id.split_variant));
    cfgmod_write_storage(CFGMOD_KIND_SYSTEM, "sys_id", &id, sizeof(id));
    s_sys = *in_sys;
    s_sys_loaded = true;
  }
  return err;
}
