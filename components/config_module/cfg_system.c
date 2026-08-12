#include "cfg_system.h"

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



void cfg_system_register(void) {
  cfgmod_register_kind(CFGMOD_KIND_SYSTEM, sys_default, sys_update_cb, sizeof(cfg_system_t));

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
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
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
