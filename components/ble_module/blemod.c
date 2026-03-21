#include "blemod.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include "ble_hid_service.h"
#include "cfg_ble.h"
#include "event_bus.h"

static const char *TAG = "ble_hid_mod";

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define BLE_APPEARANCE_HID_KEYBOARD 0x03C1
#define BLE_DEVICE_NAME             "Tecleados MK1"

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

static uint16_t s_conn_handles[CFG_BLE_MAX_PROFILES];

// Currently "advertising for pair" target profile. -1 if not pairing.
static int s_pairing_profile = -1;
// Currently directed advertising target. -1 if not doing directed.
static int s_directed_profile = -1;

// Cooldown timer to prevent instant reconnection loops after manual disconnect
static esp_timer_handle_t s_adv_cooldown_timer;
static void ble_hid_adv_timer_cb(void *arg);

// Buffer to hold peer address during pairing until encryption is complete
static ble_addr_t s_pending_addr;

// Master pairing timeout timer (absolute 60s)
static esp_timer_handle_t s_pairing_timeout_timer;
static void ble_hid_pairing_timeout_cb(void *arg);

/* ========================================================================= */
/* Forward Declarations                                                      */
/* ========================================================================= */

static void bleprph_on_sync(void);
static void bleprph_on_reset(int reason);
static void ble_hid_advertise(void);
static int ble_hid_gap_event(struct ble_gap_event *event, void *arg);

/* ========================================================================= */
/* Callbacks and Event Handlers                                              */
/* ========================================================================= */

static int ble_hid_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      ESP_LOGI(TAG, "Device connected, handle=%d", event->connect.conn_handle);
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
        int profile = (arg != NULL) ? (int)(intptr_t)arg - 1 : -1;

        // Fallback for safety (though arg should be reliable if started via our advertise func)
        if (profile == -1) {
            if (s_pairing_profile != -1) profile = s_pairing_profile;
            else if (s_directed_profile != -1) profile = s_directed_profile;
        }

        if (profile >= 0 && profile < CFG_BLE_MAX_PROFILES) {
            s_conn_handles[profile] = event->connect.conn_handle;
            ESP_LOGI(TAG, "Mapped connection %d to profile %d (via arg)", event->connect.conn_handle, profile);
            esp_event_post(BLE_EVENTS, BLE_EVENT_PROFILE_CONNECTED, &profile, sizeof(int), 0);
        } else {
            ESP_LOGW(TAG, "Could not map connection to profile!");
        }

        // If we are currently pairing to a profile, capture its MAC
        if (s_pairing_profile >= 0 && s_pairing_profile < CFG_BLE_MAX_PROFILES) {
            ESP_LOGI(TAG, "Connection established for pairing profile %d. Waiting for security...", s_pairing_profile);
            s_pending_addr = desc.peer_id_addr;
        }

        // Always clear the directed/reconnect flag once connected
        s_directed_profile = -1;
      }

      int rc = ble_gap_security_initiate(event->connect.conn_handle);
      ESP_LOGI(TAG, "Security initiation requested: %d", rc);
    } else {
      ESP_LOGI(TAG, "Connection failed (status %d)", event->connect.status);
      s_directed_profile = -1;
      // We rely on the DISCONNECT event (which usually follows a failed connect attempt
      // if it got as far as a handle assignment) or the user/timeout to restart.
      // Crucially, we do NOT call ble_hid_advertise() here to avoid tight loops
      // with persistent phone connection attempts.
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Device disconnected, reason=%d. Handle=%d", event->disconnect.reason, event->disconnect.conn.conn_handle);
    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        if (s_conn_handles[i] == event->disconnect.conn.conn_handle) {
            ESP_LOGI(TAG, "Connection for profile %d cleared.", i);
            s_conn_handles[i] = BLE_HS_CONN_HANDLE_NONE;
            esp_event_post(BLE_EVENTS, BLE_EVENT_PROFILE_DISCONNECTED, &i, sizeof(int), 0);
        }
    }
    // Resume advertising (Background, Pairing, or Reconnection)
    // Note: NimBLE adds BLE_HS_ERR_HCI_BASE (512) to HCI error codes.
    int reason = event->disconnect.reason;
    if (reason >= BLE_HS_ERR_HCI_BASE) {
        reason -= BLE_HS_ERR_HCI_BASE;
    }

    if (reason == BLE_ERR_REM_USER_CONN_TERM || reason == BLE_ERR_CONN_TERM_LOCAL) {
        if (s_pairing_profile != -1) {
            ESP_LOGI(TAG, "Manual disconnect during pairing. Restarting pairing advertisement immediately.");
            ble_hid_advertise();
        } else {
            ESP_LOGI(TAG, "Manual disconnect detected (normalized reason %d). Starting 10s cooldown.", reason);
            esp_timer_start_once(s_adv_cooldown_timer, 10000000); // 10 seconds
        }
    } else {
        ble_hid_advertise();
    }
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "Advertising complete event (timeout/stopped). Reason=%d", event->adv_complete.reason);
    // Note: We now rely more on the master s_pairing_timeout_timer for pairing state,
    // but we still clear flags here for safety.
    s_directed_profile = -1;
    // Fallback to background advertising if routing is still active and we didn't just stop it manually
    if (ble_hid_is_routing_active() && event->adv_complete.reason == BLE_HS_ETIMEOUT) {
        ESP_LOGI(TAG, "Advertising timed out. Restarting in background mode.");
        ble_hid_advertise();
    }
    break;

  case BLE_GAP_EVENT_ENC_CHANGE:
    // Gives us information when encryption and pairing process is complete
    if (event->enc_change.status == 0) {
      ESP_LOGI(TAG, "Connection successfully encrypted (pairing complete)");

      // If we were in pairing mode, fire event so cfg_ble saves credentials.
      if (s_pairing_profile >= 0 && s_pairing_profile < CFG_BLE_MAX_PROFILES) {
          ESP_LOGI(TAG, "Pairing complete for profile %d. Firing event.", s_pairing_profile);

          ble_pairing_result_t result = {
              .profile_idx = s_pairing_profile,
              .addr_type   = s_pending_addr.type,
          };
          memcpy(result.addr, s_pending_addr.val, 6);
          esp_event_post(BLE_EVENTS, BLE_EVENT_PAIRING_COMPLETE, &result, sizeof(result), 0);

          ESP_LOGI(TAG, "Pairing success. Clearing s_pairing_profile.");
          s_pairing_profile = -1;
          esp_timer_stop(s_pairing_timeout_timer);
      }
    } else {
      ESP_LOGE(TAG, "Encryption failed, status=%d", event->enc_change.status);
      ESP_LOGI(TAG, "Pairing failed. Clearing s_pairing_profile.");
      if (s_pairing_profile != -1) {
          int failed_profile = s_pairing_profile;
          s_pairing_profile = -1;
          esp_timer_stop(s_pairing_timeout_timer);
          esp_event_post(BLE_EVENTS, BLE_EVENT_PAIRING_FAILED, &failed_profile, sizeof(int), 0);
      }
    }
    break;

  case BLE_GAP_EVENT_REPEAT_PAIRING:
    ESP_LOGI(TAG, "Repeat Pairing Event");
    // Delete the old bond and allow the new one
    // Needed if the phone deleted its bond and is trying to re-pair
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
      ble_store_util_delete_peer(&desc.peer_id_addr);
    }
    return BLE_GAP_REPEAT_PAIRING_RETRY;

  case BLE_GAP_EVENT_PASSKEY_ACTION:
    ESP_LOGI(TAG, "Passkey action event");
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG,
             "Subscribe event: conn_handle=%d, attr_handle=%d, "
             "cur_notify=%d, cur_indicate=%d",
             event->subscribe.conn_handle, event->subscribe.attr_handle,
             event->subscribe.cur_notify, event->subscribe.cur_indicate);
    // When Android subscribes to notifications, push the battery level
    if (event->subscribe.cur_notify == 1) {
      int bat_rc =
          ble_hid_notify_battery_level(event->subscribe.conn_handle, 69);
      ESP_LOGI(TAG, "Sent battery notification on subscribe, rc=%d", bat_rc);
    }
    break;

  case BLE_GAP_EVENT_NOTIFY_TX:
    if (event->notify_tx.status == 0) {
      ESP_LOGD(TAG, "Notification sent OK, handle=%d",
               event->notify_tx.attr_handle);
    } else {
      ESP_LOGE(TAG, "Notification FAILED, handle=%d, status=%d",
               event->notify_tx.attr_handle, event->notify_tx.status);
    }
    break;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "MTU update event; conn_handle=%d mtu=%d",
             event->mtu.conn_handle, event->mtu.value);
    break;

  case BLE_GAP_EVENT_CONN_UPDATE:
    ESP_LOGI(TAG, "Connection parameters updated, status=%d",
             event->conn_update.status);
    return 0; // Accept

  default:
    break;
  }

  return 0;
}

static void bleprph_on_reset(int reason) {
  ESP_LOGE(TAG, "BLE stack reset, reason: %d", reason);
  for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
      s_conn_handles[i] = BLE_HS_CONN_HANDLE_NONE;
  }
}

static void bleprph_on_sync(void) {
  int rc;

  ESP_LOGW(TAG, "bleprph_on_sync called");

  // Make sure we have a valid address
  rc = ble_hs_util_ensure_addr(0); // 0 = prefer public, fallback to random
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
    return;
  }

  // Stack is ready — start advertising if there's a reason to
  ESP_LOGI(TAG, "BLE stack synced.");
  ble_hid_advertise();
}

/* ========================================================================= */
/* Core Implementation                                                       */
/* ========================================================================= */

static void ble_hid_advertise(void) {
  // Stop the cooldown timer if it's running, as we're starting advertising now
  esp_timer_stop(s_adv_cooldown_timer);

  // Respect the routing toggle
  if (!ble_hid_is_routing_active()) {
    ESP_LOGI(TAG, "BLE Routing disabled. Ensuring advertising is stopped.");
    ble_gap_adv_stop();
    return;
  }

  const cfg_ble_state_t *st = cfg_ble_get_state();
  int active_profile = st->selected_profile;
  if (s_pairing_profile != -1) active_profile = s_pairing_profile;
  else if (s_directed_profile != -1) active_profile = s_directed_profile;

  // Guard against out-of-range profile (e.g. uninitialised or corrupt NVS state)
  if (active_profile < 0 || active_profile >= CFG_BLE_MAX_PROFILES) {
    ESP_LOGW(TAG, "No valid active profile (%d). Stopping advertising.", active_profile);
    ble_gap_adv_stop();
    return;
  }

  // We only advertise if:
  // 1. Explicitly pairing (s_pairing_profile != -1)
  // 2. Explicitly reconnecting (s_directed_profile != -1)
  // 3. Or the selected profile is NOT connected and is valid (Background Passive Mode)
  bool is_connected = (s_conn_handles[active_profile] != BLE_HS_CONN_HANDLE_NONE);
  bool is_valid = st->profiles[active_profile].is_valid;
  bool is_explicit = (s_pairing_profile != -1 || s_directed_profile != -1);

  if (!is_explicit && (is_connected || !is_valid)) {
    ESP_LOGI(TAG, "Profile %d connected or invalid. No reason to advertise.", active_profile);
    ble_gap_adv_stop();
    return;
  }

  struct ble_gap_adv_params adv_params = {0};
  struct ble_hs_adv_fields fields = {0};
  int rc;

  // Stop any existing advertising first
  ble_gap_adv_stop();

  // Generate a Static Random Address derived from the public MAC + profile ID
  uint8_t base_mac[6] = {0};
  ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, base_mac, NULL);

  uint8_t rand_addr[6];
  memcpy(rand_addr, base_mac, 6);
  rand_addr[5] |= 0xC0; // Set highest 2 bits of MSB for Static Random Address type

  // Rotate LSB based on profile ID AND nonce to change identity on re-pair
  rand_addr[0] = (rand_addr[0] + active_profile + st->profiles[active_profile].addr_nonce) & 0xFF;

  rc = ble_hs_id_set_rnd(rand_addr);
  if (rc != 0) {
      ESP_LOGE(TAG, "Failed to set random address: %d", rc);
  }

  // --- Advertising data ---

  // Appearance: HID keyboard
  fields.appearance = BLE_APPEARANCE_HID_KEYBOARD;
  fields.appearance_is_present = 1;

  // Device name
  const char *name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)name;
  fields.name_len = (uint8_t)strlen(name);
  fields.name_is_complete = 1;

  // Advertise the HID service UUID
  static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
  fields.uuids16 = &hid_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  // Discoverability: GEN_DISC only if PAIRING or RECONNECTING (Discoverable)
  // BACKGROUND mode is NON-DISC (only connectable by those who know us)
  if (s_pairing_profile != -1 || s_directed_profile != -1) {
      fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
      adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  } else {
      fields.flags = BLE_HS_ADV_F_BREDR_UNSUP; // No DISC flag = Non-Discoverable
      adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;
  }

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
    return;
  }

  // --- Advertising parameters ---
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Always connectable (Undirected)

  // Intervals: Fast for pairing/reconnect, slow for background
  if (s_pairing_profile != -1 || s_directed_profile != -1) {
      adv_params.itvl_min = 32;   // 20ms
      adv_params.itvl_max = 48;   // 30ms
  } else {
      adv_params.itvl_min = 1280; // 800ms
      adv_params.itvl_max = 1600; // 1000ms
  }

  // Duration: Use master timer for pairing, stack timer for reconnection
  int32_t duration_ms = BLE_HS_FOREVER;
  if (s_pairing_profile != -1) {
      duration_ms = 60000; // Match master timer
  } else if (s_directed_profile != -1) {
      duration_ms = 15000;
  }

  ESP_LOGI(TAG, "Starting gap adv: mode=%s, duration=%ld ms, profile=%d, state=%s",
           adv_params.disc_mode == BLE_GAP_DISC_MODE_GEN ? "GEN_DISC" : "NON_DISC",
           (long)duration_ms, active_profile,
           (s_pairing_profile != -1) ? "PAIRING" : (s_directed_profile != -1 ? "RECONNECTING" : "BACKGROUND"));

  rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, duration_ms, &adv_params, ble_hid_gap_event, (void*)(intptr_t)(active_profile + 1));
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    return;
  }
}

/* NimBLE runs its own task — this is its entry point */
static void ble_host_task(void *param) {
  // blocks until nimble_port_stop()
  nimble_port_run();
  nimble_port_freertos_deinit();
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void ble_hid_init(void) {
  esp_err_t ret;

  for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
      s_conn_handles[i] = BLE_HS_CONN_HANDLE_NONE;
  }

  // 1. Init the NimBLE transport (HCI over VHCI for integrated controller)
  ret = nimble_port_init();
  assert(ret == ESP_OK);

  // 2. Register stack callbacks
  ble_hs_cfg.sync_cb = bleprph_on_sync;   // called when stack is ready
  ble_hs_cfg.reset_cb = bleprph_on_reset; // called on unrecoverable error

  // 3. Security / bonding config
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; // "Just Works" pairing
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_mitm = 0; // "Just Works" without MITM protection
  ble_hs_cfg.sm_sc = 1;   // Secure Connections (BLE 4.2+)

  // 4. Initialize bond storage (NVS)
  ble_hs_cfg.store_read_cb = ble_store_config_read;
  ble_hs_cfg.store_write_cb = ble_store_config_write;

  // 5. Register GATT services
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_hid_svc_register();

  // 6. Set device name and appearance (GAP)
  ret = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
  assert(ret == 0);

  ret = ble_svc_gap_device_appearance_set(BLE_APPEARANCE_HID_KEYBOARD);
  assert(ret == 0);

  // 7. Spin up the NimBLE FreeRTOS task
  nimble_port_freertos_init(ble_host_task);

  // 8. Initialize advertising timers
  const esp_timer_create_args_t cooldown_timer_args = {
      .callback = ble_hid_adv_timer_cb,
      .name = "adv_cooldown"
  };
  esp_timer_create(&cooldown_timer_args, &s_adv_cooldown_timer);

  const esp_timer_create_args_t pairing_timer_args = {
      .callback = ble_hid_pairing_timeout_cb,
      .name = "pairing_timeout"
  };
  esp_timer_create(&pairing_timer_args, &s_pairing_timeout_timer);

  ESP_LOGI(TAG, "BLE HID initialization complete");
}

static void ble_hid_pairing_timeout_cb(void *arg) {
    if (s_pairing_profile != -1) {
        ESP_LOGW(TAG, "ABSOLUTE Pairing timeout for profile %d. Stopping pairing mode.", s_pairing_profile);
        int timed_out_profile = s_pairing_profile;
        s_pairing_profile = -1;
        esp_event_post(BLE_EVENTS, BLE_EVENT_PAIRING_TIMEOUT, &timed_out_profile, sizeof(int), 0);
        ble_hid_advertise(); // Restart in background or stop
    }
}

static void ble_hid_adv_timer_cb(void *arg) {
    ESP_LOGI(TAG, "Cooldown expired. Resuming background advertising.");
    ble_hid_advertise();
}

static uint16_t get_active_conn_handle(void) {
  const cfg_ble_state_t *st = cfg_ble_get_state();
  int sel = st->selected_profile;
  if (sel >= 0 && sel < CFG_BLE_MAX_PROFILES) {
      return s_conn_handles[sel];
  }
  return BLE_HS_CONN_HANDLE_NONE;
}

bool ble_hid_is_connected(void) {
  return (get_active_conn_handle() != BLE_HS_CONN_HANDLE_NONE);
}

esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, size_t len) {
  uint16_t handle = get_active_conn_handle();
  if (handle == BLE_HS_CONN_HANDLE_NONE) {
    return ESP_ERR_INVALID_STATE;
  }

  const cfg_ble_state_t *st = cfg_ble_get_state();
  if (!st->ble_routing_enabled) {
      return ESP_OK; // Silently drop, acting as "OFF"
  }

  int rc = ble_hid_tx_keyboard_report(handle, report, len);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed characteristic tx: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t ble_hid_send_consumer_report(uint16_t media_keycode) {
  uint16_t handle = get_active_conn_handle();
  if (handle == BLE_HS_CONN_HANDLE_NONE) {
    return ESP_ERR_INVALID_STATE;
  }

  int rc = ble_hid_tx_consumer_report(handle, media_keycode);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed consumer tx: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

void ble_hid_profile_pair(uint8_t profile_id) {
    ESP_LOGI(TAG, "[BLE API] Handling HOLD (Pair) for profile %d", profile_id);

    // Erase old credentials if valid and mark profile as invalid
    const cfg_ble_state_t *st = cfg_ble_get_state();
    bool was_valid = st->profiles[profile_id].is_valid;

    cfg_ble_state_t new_state = *st;
    new_state.profiles[profile_id].is_valid = false;
    // Rotate the address nonce so we appear as a new device to the phone
    new_state.profiles[profile_id].addr_nonce++;
    cfg_ble_save_state(&new_state);

    if (was_valid) {
        ESP_LOGI(TAG, "[BLE API] Erasing old credentials for profile %d", profile_id);
        ble_addr_t old_addr;
        old_addr.type = st->profiles[profile_id].addr_type;
        memcpy(old_addr.val, st->profiles[profile_id].val, 6);
        int rc = ble_store_util_delete_peer(&old_addr);
        if (rc != 0) {
            ESP_LOGW(TAG, "[BLE API] ble_store_util_delete_peer failed: %d", rc);
        }
    }

    ESP_LOGI(TAG, "[BLE API] Setting s_pairing_profile = %d", profile_id);
    s_pairing_profile = profile_id;
    s_directed_profile = -1;
    int pairing_profile_int = (int)profile_id;
    esp_event_post(BLE_EVENTS, BLE_EVENT_PAIRING_STARTED, &pairing_profile_int, sizeof(int), 0);

    // Start absolute pairing timeout timer
    esp_timer_stop(s_pairing_timeout_timer);
    esp_timer_start_once(s_pairing_timeout_timer, 60000000); // 60 seconds

    if (s_conn_handles[profile_id] != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "[BLE API] Profile %d already connected (handle %d). Terminating for pairing.", profile_id, s_conn_handles[profile_id]);
        ble_gap_terminate(s_conn_handles[profile_id], BLE_ERR_REM_USER_CONN_TERM);
        // The GAP disconnect event will start the advertising.
    } else {
        ESP_LOGI(TAG, "[BLE API] Not currently connected on profile %d. Starting general advertisement for pairing.", profile_id);
        ble_hid_advertise(); // General advertise
    }
}

void ble_hid_profile_connect_and_select(uint8_t profile_id) {
    ESP_LOGI(TAG, "[BLE API] Handling SINGLE TAP (Select/Connect) for profile %d", profile_id);
    const cfg_ble_state_t *st = cfg_ble_get_state();

    if (st->profiles[profile_id].is_valid) {
        cfg_ble_state_t new_state = *st;
        new_state.selected_profile = profile_id;
        cfg_ble_save_state(&new_state);

        if (s_conn_handles[profile_id] != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "[BLE API] Profile %d is already connected. Routing is now switched.", profile_id);
            // No need to terminate anything else. They can stay connected.
        } else {
            ESP_LOGI(TAG, "[BLE API] Profile %d not currently connected. Starting reconnection advertisement.", profile_id);
            if (s_pairing_profile != -1) {
                ESP_LOGI(TAG, "[BLE API] Clearing s_pairing_profile due to manual select.");
                s_pairing_profile = -1;
            }
            s_directed_profile = profile_id;
            ble_hid_advertise();
        }
    } else {
        ESP_LOGW(TAG, "[BLE API] Profile %d is not configured (unpaired). Ignoring selection request.", profile_id);
    }
}

void ble_hid_profile_toggle_connection(uint8_t profile_id) {
    ESP_LOGI(TAG, "[BLE API] Handling DOUBLE TAP (Toggle Connect) for profile %d", profile_id);
    if (s_conn_handles[profile_id] != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "[BLE API] Profile %d connected. Terminating connection (Toggle OFF).", profile_id);
        ble_gap_terminate(s_conn_handles[profile_id], BLE_ERR_REM_USER_CONN_TERM);
    } else {
        const cfg_ble_state_t *st = cfg_ble_get_state();
        if (st->profiles[profile_id].is_valid) {
            ESP_LOGI(TAG, "[BLE API] Not connected. Attempting reconnection advertising for profile %d.", profile_id);
            if (s_pairing_profile != -1) {
                ESP_LOGI(TAG, "[BLE API] Clearing s_pairing_profile due to manual toggle.");
                s_pairing_profile = -1;
            }
            s_directed_profile = profile_id;
            ble_hid_advertise();
        } else {
            ESP_LOGW(TAG, "[BLE API] Profile %d is not configured (invalid).", profile_id);
        }
    }
}

void ble_hid_set_routing_active(bool active) {
    bool was_active = ble_hid_is_routing_active();

    cfg_ble_state_t new_state = *cfg_ble_get_state();
    new_state.ble_routing_enabled = active;
    cfg_ble_save_state(&new_state);
    esp_event_post(BLE_EVENTS, BLE_EVENT_ROUTING_CHANGED, &active, sizeof(bool), 0);

    if (active) {
        if (!was_active) {
            ESP_LOGI(TAG, "BLE Routing enabled. Triggering snappy reconnection.");
            // Set directed profile to currently selected for a fast reconnection attempt
            s_directed_profile = new_state.selected_profile;
            ble_hid_advertise();
        }
    } else {
        ESP_LOGI(TAG, "BLE Routing disabled. Terminating all connections and stopping advertising.");
        for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
            if (s_conn_handles[i] != BLE_HS_CONN_HANDLE_NONE) {
                ble_gap_terminate(s_conn_handles[i], BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        s_pairing_profile = -1;
        s_directed_profile = -1;
        ble_hid_advertise(); // Will stop advertising due to routing-disabled check
    }
}

bool ble_hid_is_routing_active(void) {
    return cfg_ble_get_state()->ble_routing_enabled;
}

uint16_t ble_hid_get_connected_profiles_bitmap(void) {
    uint16_t bitmap = 0;
    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        if (s_conn_handles[i] != BLE_HS_CONN_HANDLE_NONE) {
            bitmap |= (1 << i);
        }
    }
    return bitmap;
}

int ble_hid_get_pairing_profile(void) {
    return s_pairing_profile;
}
