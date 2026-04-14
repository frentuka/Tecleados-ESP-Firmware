#include "blemod.h"
#include "battery.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_pvcy.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include "ble_hid_service.h"
#include "cfg_ble.h"
#include "cfg_system.h"
#include "event_bus.h"

static const char *TAG = "ble_hid_mod";

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define BLE_APPEARANCE_HID_KEYBOARD 0x03C1
#define BLE_DEVICE_NAME             "Tecleados MK1"

extern void ble_store_config_init(void);

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

static uint16_t s_conn_handles[CFG_BLE_MAX_PROFILES];

// Marker for a profile that is connected on the other half of the split keyboard.
// Distinct from BLE_HS_CONN_HANDLE_NONE (0xFFFF) to avoid reconnection loops.
#define BLE_CONN_HANDLE_REMOTE 0xFFFE

// Currently "advertising for pair" target profile. -1 if not pairing.
static int s_pairing_profile = -1;
// Currently directed advertising target. -1 if not doing directed.
static int s_directed_profile = -1;
// How many times we have restarted directed (reconnect) advertising after a
// timeout without getting a connection.  Reset on connect; capped at
// BLE_RECONNECT_MAX_RETRIES after which we fall to background mode.
static int s_reconnect_retries = 0;
#define BLE_RECONNECT_MAX_RETRIES 8  // 8 × 15 s = 2 min of active GEN_DISC
// Tracks whether the most recently STARTED ADV was directed (not undirected).
// Used in ADV_COMPLETE to distinguish a directed 1.28-s hardware timeout
// (reason=0) from a connection-made or explicit-stop completion (also reason=0).
static bool s_directed_adv_active = false;

// Cooldown timer to prevent instant reconnection loops after manual disconnect
static esp_timer_handle_t s_adv_cooldown_timer;
static void ble_hid_adv_timer_cb(void *arg);

// Suspended state flag (used by Split Keyboard slave to stop BLE without updating NVS config)
static bool s_is_suspended = false;

// Full NimBLE reinit support: semaphore signalled by ble_host_task just before
// it calls vTaskDelete so the deinit path knows the task has exited.
static SemaphoreHandle_t s_host_stopped_sem = NULL;
// Guard: only register the CONFIG_EVENTS handler once across reinits.
static bool s_config_evt_registered = false;

// Directed profile to restore in bleprph_on_sync after a Master-side reinit.
// Set before ble_hid_deinit(); read and cleared in bleprph_on_sync.
static int s_post_reinit_directed_profile = -1;

// When true, the next ble_hid_set_suspended(false) skips directed ADV and
// goes straight to undirected GEN_DISC.  Set by ble_hid_skip_directed_adv(),
// consumed and cleared by ble_hid_set_suspended().
static bool s_skip_directed_on_resume = false;

// Phase tracking for seamless multidevice reconnection
typedef enum {
    RECONN_PHASE_IDLE = 0,
    RECONN_PHASE_SELECTED,  // Try selected up to 5 times
    RECONN_PHASE_FOREVER,   // Cycle infinitely until ANY connection
    RECONN_PHASE_FINITE     // Cycle finite (5x per profile) until queue empty
} reconn_phase_t;

static reconn_phase_t s_reconn_phase = RECONN_PHASE_IDLE;
static uint16_t s_reconnect_pending_bitmap = 0;
static uint16_t s_slave_mirror_conn_bitmap = 0; // Latch: last known connected profiles on Master
static uint8_t s_reconnect_cycles[CFG_BLE_MAX_PROFILES] = {0};
static int s_reconnect_last_profile = -1;
static int s_selected_retries_left = 0;

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
static void ble_hid_start_next_reconnect(void);
static void ble_hid_on_ble_config_updated(void *arg, esp_event_base_t base,
                                           int32_t event_id, void *data);
static void ble_hid_on_split_status_updated(void *arg, esp_event_base_t base,
                                             int32_t event_id, void *data);
static void ble_hid_dump_bonds(void);

/* ========================================================================= */
/* Callbacks and Event Handlers                                              */
/* ========================================================================= */

static void ble_hid_start_next_reconnect(void) {
    if (s_reconn_phase == RECONN_PHASE_IDLE || s_reconnect_pending_bitmap == 0) {
        s_directed_profile = -1;
        s_reconn_phase = RECONN_PHASE_IDLE;
        ble_hid_advertise(); // Fall back to background advertising
        return;
    }

    // Pick next profile using round-robin
    int picked = -1;
    for (int i = 1; i <= CFG_BLE_MAX_PROFILES; i++) {
        int candidate = (s_reconnect_last_profile + i) % CFG_BLE_MAX_PROFILES;
        if (s_reconnect_pending_bitmap & (1 << candidate)) {
            picked = candidate;
            break;
        }
    }

    if (picked != -1) {
        s_directed_profile = picked;
        s_reconnect_last_profile = picked;
        ESP_LOGI(TAG, "Starting sequential reconnect for profile %d (Phase %d)", picked, s_reconn_phase);
        ble_hid_advertise();
    } else {
        s_directed_profile = -1;
        s_reconn_phase = RECONN_PHASE_IDLE;
        ble_hid_advertise();
    }
}

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
            s_reconnect_retries = 0; // successful connection — reset retry counter
            ESP_LOGI(TAG, "Mapped connection %d to profile %d (via arg)", event->connect.conn_handle, profile);
            esp_event_post(BLE_EVENTS, BLE_EVENT_PROFILE_CONNECTED, &profile, sizeof(int), 0);
        } else {
            ESP_LOGW(TAG, "Could not map connection to profile!");
        }

        // Identity resolution is deferred until encryption/pairing is complete.
        s_directed_profile = -1;
      }

      int rc = ble_gap_security_initiate(event->connect.conn_handle);
      ESP_LOGI(TAG, "Security initiation requested: %d", rc);
    } else {
      ESP_LOGI(TAG, "Connection failed (status %d)", event->connect.status);
      int sel = s_directed_profile;
      s_directed_profile = -1;

      if (s_reconn_phase != RECONN_PHASE_IDLE) {
          if (s_reconn_phase == RECONN_PHASE_SELECTED) {
              s_selected_retries_left--;
              if (s_selected_retries_left <= 0) {
                  ESP_LOGI(TAG, "Selected Profile failed. Moving to FOREVER Phase.");
                  if (sel >= 0 && sel < CFG_BLE_MAX_PROFILES && cfg_ble_get_state()->profiles[sel].is_valid) {
                      s_reconnect_pending_bitmap |= (1 << sel);
                  }
                  s_reconn_phase = RECONN_PHASE_FOREVER;
              }
          }
          ble_hid_start_next_reconnect();
      }
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
            s_reconn_phase = RECONN_PHASE_IDLE;
            s_reconnect_pending_bitmap = 0;
            esp_timer_start_once(s_adv_cooldown_timer, 10000000); // 10 seconds
        }
    } else {
        if (s_reconn_phase != RECONN_PHASE_IDLE) {
            ble_hid_start_next_reconnect();
        } else {
            ble_hid_advertise();
        }
    }
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "Advertising complete event (timeout/stopped). Reason=%d", event->adv_complete.reason);
    {
        // HIGH-DUTY directed advertising (1.28 s BLE controller window) completes
        // with reason=0 (BLE_ERR_SUCCESS/stopped), NOT reason=13 (BLE_HS_ETIMEOUT).
        // LOW-DUTY directed and undirected GEN_DISC timeouts produce reason=13.
        // We must handle both so that Android gets retried after the 1.28s window.
        //
        // Guard: only treat reason=0 as a retry trigger if WE started directed ADV
        // (s_directed_adv_active). When reason=0 comes from:
        //   a) a real connection → BLE_GAP_EVENT_CONNECT already fired, clears
        //      s_directed_profile, so was_reconnecting=false → no retry.
        //   b) explicit ble_gap_adv_stop() from ble_hid_advertise() switching modes
        //      → s_directed_adv_active=false was set in the undirected branch before
        //      the event fires → condition fails → no retry.
        bool was_directed_timeout = (event->adv_complete.reason == 0 && s_directed_adv_active);
        s_directed_adv_active = false; // consume the flag

        if ((event->adv_complete.reason == BLE_HS_ETIMEOUT || was_directed_timeout)
            && ble_hid_is_routing_active()) {

            int sel = s_directed_profile;
            bool is_pairing = (s_pairing_profile != -1);

            if (sel != -1 && !is_pairing) {
                if (s_reconn_phase == RECONN_PHASE_SELECTED) {
                    s_reconnect_cycles[sel]++;
                    s_selected_retries_left--;

                    if (s_selected_retries_left > 0) {
                        // INTERLEAVED PRIORITY: If we have other profiles waiting in the bitmap,
                        // we alternate between the Selected profile and the next 'Other' profile.
                        // This keeps the Selected profile at 50% priority while preventing
                        // the others from being starved (dropped).
                        if (s_selected_retries_left % 2 == 0 && s_reconnect_pending_bitmap != 0) {
                            ESP_LOGI(TAG, "Interleaving: giving a turn to secondary profiles (Selected %d retries left).", s_selected_retries_left);
                            s_directed_profile = -1; // Force start_next_reconnect to pick from bitmap
                            ble_hid_start_next_reconnect();
                        } else {
                            ESP_LOGI(TAG, "Selected Profile %d timed out. Retrying (%d left).", sel, s_selected_retries_left);
                            ble_hid_advertise();
                        }
                    } else {
                        ESP_LOGI(TAG, "Selected Profile %d exhausted. Moving to FOREVER Phase.", sel);
                        if (cfg_ble_get_state()->profiles[sel].is_valid) {
                            s_reconnect_pending_bitmap |= (1 << sel);
                        }
                        s_reconn_phase = RECONN_PHASE_FOREVER;
                        s_directed_profile = -1;
                        ble_hid_start_next_reconnect();
                        return 0; // break out
                    }
                } 
                else if (s_reconn_phase == RECONN_PHASE_FOREVER) {
                    s_directed_profile = -1;
                    ble_hid_start_next_reconnect();
                    return 0;
                }
                else if (s_reconn_phase == RECONN_PHASE_FINITE) {
                    s_reconnect_cycles[sel]++;
                    if (s_reconnect_cycles[sel] >= 5) {
                        ESP_LOGI(TAG, "Finite reconnect exhausted for profile %d.", sel);
                        s_reconnect_pending_bitmap &= ~(1 << sel);
                        if (s_conn_handles[sel] == BLE_CONN_HANDLE_REMOTE) {
                            s_conn_handles[sel] = BLE_HS_CONN_HANDLE_NONE;
                        }
                    }
                    s_directed_profile = -1;
                    ble_hid_start_next_reconnect();
                    return 0;
                }
            } else {
                 s_directed_profile = -1;
                 s_reconn_phase = RECONN_PHASE_IDLE;
            }
        } else {
             s_directed_profile = -1;
             s_reconn_phase = RECONN_PHASE_IDLE;
        }
        ble_hid_advertise();
    } // end ADV_COMPLETE block
    break;

  case BLE_GAP_EVENT_ENC_CHANGE:
    // Gives us information when encryption and pairing process is complete
    if (event->enc_change.status == 0) {
      ESP_LOGI(TAG, "Connection successfully encrypted (pairing complete)");

      // Re-query connection to get the resolved Identity Address (Post-IRK exchange)
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
          s_pending_addr = desc.peer_id_addr;
          ESP_LOGI(TAG, "Identity resolved: %02X:%02X:%02X:%02X:%02X:%02X",
                   s_pending_addr.val[0], s_pending_addr.val[1], s_pending_addr.val[2],
                   s_pending_addr.val[3], s_pending_addr.val[4], s_pending_addr.val[5]);
      }

      // If we were in pairing mode, fire event so cfg_ble saves credentials.
      if (s_pairing_profile >= 0 && s_pairing_profile < CFG_BLE_MAX_PROFILES) {
          ESP_LOGI(TAG, "Pairing complete for profile %d. Firing event.", s_pairing_profile);

          cfg_ble_state_t st_unsync = *cfg_ble_get_state();
          st_unsync.has_unsynced_updates = 1;
          cfg_ble_save_state(&st_unsync);

          ble_pairing_result_t result = {
              .profile_idx = s_pairing_profile,
              .addr_type   = s_pending_addr.type,
          };
          memcpy(result.addr, s_pending_addr.val, 6);
          esp_event_post(BLE_EVENTS, BLE_EVENT_PAIRING_COMPLETE, &result, sizeof(result), 0);

          ESP_LOGI(TAG, "Pairing success. Clearing s_pairing_profile.");
          s_pairing_profile = -1;
          esp_timer_stop(s_pairing_timeout_timer);
      } else {
          // Self-healing: reconnect on an existing bond (s_pairing_profile == -1).
          // If the connected peer's identity address matches a profile whose
          // is_valid is FALSE, that profile was invalidated (e.g. by a HOLD erase
          // that propagated to both halves before the bond sync guard could protect
          // it). A successful ENC_CHANGE proves the bond is alive — restore is_valid
          // by posting a synthetic PAIRING_COMPLETE so cfg_ble saves + syncs it.
          const cfg_ble_state_t *st = cfg_ble_get_state();
          for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
              if (!st->profiles[i].is_valid &&
                  memcmp(st->profiles[i].val, s_pending_addr.val, 6) == 0) {
                  ESP_LOGW(TAG, "Bond self-heal: profile %d matches reconnecting peer "
                           "%02X:%02X:%02X:%02X:%02X:%02X but is_valid=false — restoring.",
                           i, s_pending_addr.val[0], s_pending_addr.val[1],
                           s_pending_addr.val[2], s_pending_addr.val[3],
                           s_pending_addr.val[4], s_pending_addr.val[5]);
                  ble_pairing_result_t heal = {
                      .profile_idx = i,
                      .addr_type   = s_pending_addr.type,
                  };
                  memcpy(heal.addr, s_pending_addr.val, 6);
                  esp_event_post(BLE_EVENTS, BLE_EVENT_PAIRING_COMPLETE, &heal, sizeof(heal), 0);
                  break; // only fix the first matching profile
              }
          }
      }

      // If we are in a reconnect phase, we got a connection!
      if (s_reconn_phase == RECONN_PHASE_SELECTED || s_reconn_phase == RECONN_PHASE_FOREVER) {
          s_reconn_phase = RECONN_PHASE_FINITE;
          memset(s_reconnect_cycles, 0, sizeof(s_reconnect_cycles));
          ESP_LOGI(TAG, "First connection established. Transitioning to FINITE Phase.");
      }

      int connected_profile = -1;
      for (int i=0; i<CFG_BLE_MAX_PROFILES; i++) {
          if (s_conn_handles[i] == event->enc_change.conn_handle) {
              connected_profile = i;
              break;
          }
      }
      if (connected_profile != -1 && s_reconn_phase == RECONN_PHASE_FINITE) {
          s_reconnect_pending_bitmap &= ~(1 << connected_profile);
      }

      if (s_reconn_phase == RECONN_PHASE_FINITE && s_reconnect_pending_bitmap != 0) {
          ESP_LOGI(TAG, "Encryption complete, starting next reconnect from queue.");
          ble_hid_start_next_reconnect();
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
          ble_hid_notify_battery_level(event->subscribe.conn_handle, battery_get_level_pct());
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

  // Apply any bonds that arrived while we were syncing or suspended.
  // This is critical for Android devices which require the Resolving List
  // to be populated before they can resolve the keyboard's identity.
  extern void cfg_ble_apply_deferred_bonds(void);
  cfg_ble_apply_deferred_bonds();

  if (s_is_suspended) {
      ESP_LOGI(TAG, "BLE stack synced (suspended — not advertising).");
      return;
  }

  // Restore the directed profile saved before a Master-side reinit so that
  // ble_hid_advertise() targets the newly-synced host (not NON_DISC fallback).
  if (s_post_reinit_directed_profile >= 0) {
      s_directed_profile             = s_post_reinit_directed_profile;
      s_post_reinit_directed_profile = -1;
      // Reset retry counter so directed advertising starts fresh after every
      // master activation — regardless of what retry state was left over from
      // a previous incomplete reconnect cycle on the old master.
      s_reconnect_retries = 0;
      ESP_LOGI(TAG, "BLE stack synced (post-reinit): directed adv → profile %d.",
               s_directed_profile);
  } else {
      ESP_LOGI(TAG, "BLE stack synced.");
  }
  ble_hid_advertise();
}

/* ========================================================================= */
/* Core Implementation                                                       */
/* ========================================================================= */

static void ble_hid_advertise(void) {
  // Breathing room: wait 50ms to let the ESP-NOW split link exchange heartbeats
  // between advertising bursts. Essential for radio stability on ESP32-S3.
  vTaskDelay(pdMS_TO_TICKS(50));

  // Stop the cooldown timer if it's running, as we're starting advertising now
  esp_timer_stop(s_adv_cooldown_timer);

  // Gating: NimBLE host must be synced with the controller before we can
  // set addresses or start advertising. If not synced yet, bleprph_on_sync
  // will call us again once the handshake is complete.
  if (!ble_hs_synced()) {
    ESP_LOGD(TAG, "ble_hid_advertise: host not synced yet, deferring.");
    return;
  }

  // Respect the routing toggle and suspended state
  if (!ble_hid_is_routing_active() || s_is_suspended) {
    ESP_LOGI(TAG, "BLE Routing disabled or suspended. Ensuring advertising is stopped.");
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
  //
  // CRITICAL: A profile marked as BLE_CONN_HANDLE_REMOTE (0xFFFE) is NOT yet
  // locally connected. We MUST continue to advertise until a local handle (< 0xFF00)
  // is assigned by the NimBLE stack.
  bool is_connected = (s_conn_handles[active_profile] < 0xFF00);
  bool is_valid = st->profiles[active_profile].is_valid;
  bool is_explicit = (s_pairing_profile != -1 || s_directed_profile != -1);

  if (!is_explicit && (is_connected || !is_valid)) {
    ESP_LOGI(TAG, "Profile %d connected or invalid. No reason to advertise.", active_profile);
    ble_gap_adv_stop();
    return;
  }

  struct ble_gap_adv_params adv_params = {0};
  int rc;

  // Stop any existing advertising first
  ble_gap_adv_stop();

  // Generate a Static Random Address.
  // If a shared BLE address is configured (for split keyboards so both halves share
  // the same BLE identity), use that as the base. Otherwise derive from public MAC.
  uint8_t rand_addr[6];
  cfg_system_t sys_for_addr;
  bool use_shared = false;
  if (cfg_system_get(&sys_for_addr) == ESP_OK) {
      for (int i = 0; i < 6; i++) {
          if (sys_for_addr.ble_shared_addr[i]) { use_shared = true; break; }
      }
  }
  if (use_shared) {
      const uint8_t *s = sys_for_addr.ble_shared_addr;
      memcpy(rand_addr, s, 6);
      ESP_LOGI(TAG, "ADDR_BASE shared=%02X:%02X:%02X:%02X:%02X:%02X",
               s[0], s[1], s[2], s[3], s[4], s[5]);
  } else {
      uint8_t base_mac[6] = {0};
      ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, base_mac, NULL);
      memcpy(rand_addr, base_mac, 6);
      ESP_LOGW(TAG, "ADDR_BASE unique_public=%02X:%02X:%02X:%02X:%02X:%02X (WARNING: No shared addr!)",
               base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
  }
  rand_addr[5] |= 0xC0; // Set highest 2 bits of MSB for Static Random Address type

  // Rotate LSB based on profile ID AND nonce to change identity on re-pair.
  uint8_t nonce = st->profiles[active_profile].addr_nonce;
  rand_addr[0] = (rand_addr[0] + active_profile + nonce) & 0xFF;

  ESP_LOGI(TAG, "ADDR_FINAL profile=%d nonce=%u addr=%02X:%02X:%02X:%02X:%02X:%02X",
           active_profile, nonce,
           rand_addr[0], rand_addr[1], rand_addr[2], rand_addr[3], rand_addr[4], rand_addr[5]);

  // Log profile dump for diagnostics
  ESP_LOGI(TAG, "Dumping all profiles (active=%d):", active_profile);
  for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
      if (st->profiles[i].is_valid) {
          const uint8_t *pa = st->profiles[i].val;
          ESP_LOGI(TAG, "  [%d] VALID, peer=%02X:%02X:%02X:%02X:%02X:%02X, type=%u, nonce=%u",
                   i, pa[0], pa[1], pa[2], pa[3], pa[4], pa[5],
                   st->profiles[i].addr_type, st->profiles[i].addr_nonce);
      }
  }

  rc = ble_hs_id_set_rnd(rand_addr);
  if (rc != 0) {
      ESP_LOGE(TAG, "Failed to set random address: %d", rc);
  }

  bool is_reconnecting = (s_directed_profile != -1) && (s_pairing_profile == -1);

  // Reconnection advertising strategy:
  //
  // Retry 0  : HIGH duty cycle directed (ADV_DIRECT_IND, 3.75 ms burst, 1.28 s max).
  //            Windows/PC BLE stacks respond within ~50 ms → instant silent handover.
  //            Android does NOT respond to directed ADV: it connects as an INITIATOR
  //            by actively scanning for the keyboard's undirected ADV (not by
  //            passively watching for directed packets aimed at it).
  //
  // Retries 1+: Undirected GEN_DISC (20 ms interval, 15 s window).
  //            Android initiates a connection within ~500 ms of seeing the first
  //            undirected packet. Windows also connects here if it missed the
  //            1.28 s directed window.
  //
  // Total worst-case Android reconnect: 1.28 s + ~0.5 s = ~2 s.
  if (is_reconnecting && is_valid && s_reconnect_cycles[active_profile] == 0 && s_reconn_phase != RECONN_PHASE_FOREVER) {
      adv_params.conn_mode = BLE_GAP_CONN_MODE_DIR;

      // Low duty cycle: Much more cooperative with Wi-Fi/ESP-NOW than high duty.
      // We set a 1.28s duration to match the previous high-duty burst length.
      adv_params.high_duty_cycle = 0;
      adv_params.itvl_min = 0; // Use default
      adv_params.itvl_max = 0;
      int32_t dir_duration = 1280;

      // Use the Identity Address from the bond store as the target.
      ble_addr_t peer_addr = {0};
      struct ble_store_value_sec peer_sec;
      struct ble_store_key_sec key_sec = { .peer_addr = *BLE_ADDR_ANY, .idx = 0 };
      
      bool identity_found = false;
      while (ble_store_read_peer_sec(&key_sec, &peer_sec) == 0) {
          if (memcmp(peer_sec.peer_addr.val, st->profiles[active_profile].val, 6) == 0) {
              peer_addr = peer_sec.peer_addr;
              identity_found = true;
              break;
          }
          key_sec.idx++;
      }

      if (!identity_found) {
          uint8_t target_type = st->profiles[active_profile].addr_type & 0x01;
          peer_addr.type = target_type;
          memcpy(peer_addr.val, st->profiles[active_profile].val, 6);
          ESP_LOGD(TAG, "Directed adv: identity not in bond store, using profile addr");
      }

      ESP_LOGI(TAG, "Starting directed adv → %02X:%02X:%02X:%02X:%02X:%02X "
               "profile=%d HIGH_DUTY(1.28s)",
               peer_addr.val[0], peer_addr.val[1], peer_addr.val[2],
               peer_addr.val[3], peer_addr.val[4], peer_addr.val[5],
               active_profile);

      // -- START directed advertising --
      s_directed_adv_active = true;
      rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, &peer_addr, dir_duration, &adv_params,
                             ble_hid_gap_event, (void*)(intptr_t)(active_profile + 1));
  } else {
      // -- START undirected advertising --
      s_directed_adv_active = false;
      // Undirected advertising: pairing mode, background mode, or directed target
      // not yet known (config sync still in flight after a role swap).
      struct ble_hs_adv_fields fields = {0};

      fields.appearance = BLE_APPEARANCE_HID_KEYBOARD;
      fields.appearance_is_present = 1;

      const char *name = ble_svc_gap_device_name();
      fields.name = (uint8_t *)name;
      fields.name_len = (uint8_t)strlen(name);
      fields.name_is_complete = 1;

      static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
      fields.uuids16 = &hid_uuid;
      fields.num_uuids16 = 1;
      fields.uuids16_is_complete = 1;

      if (s_pairing_profile != -1 || s_directed_profile != -1) {
          fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
          adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
          adv_params.itvl_min = 32;   // 20ms
          adv_params.itvl_max = 48;   // 30ms
      } else {
          fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;
          adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;
          adv_params.itvl_min = 1280; // 800ms
          adv_params.itvl_max = 1600; // 1000ms
      }
      adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;

      int32_t duration_ms = BLE_HS_FOREVER;
      if (s_pairing_profile != -1) {
          duration_ms = 60000;
      } else if (s_directed_profile != -1) {
          duration_ms = 1300;
      }

      rc = ble_gap_adv_set_fields(&fields);
      if (rc != 0) {
          ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
          return;
      }

      ESP_LOGI(TAG, "Starting undirected adv: mode=%s duration=%ld ms profile=%d state=%s",
               adv_params.disc_mode == BLE_GAP_DISC_MODE_GEN ? "GEN_DISC" : "NON_DISC",
               (long)duration_ms, active_profile,
               s_pairing_profile != -1 ? "PAIRING" : (s_directed_profile != -1 ? "RECONNECTING(no peer)" : "BACKGROUND"));

      rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, duration_ms, &adv_params,
                             ble_hid_gap_event, (void*)(intptr_t)(active_profile + 1));
  }

  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
  }
}

/* NimBLE runs its own task — this is its entry point */
static void ble_host_task(void *param) {
  // blocks until nimble_port_stop()
  nimble_port_run();
  // Signal the deinit path that the host event loop has exited so it is safe
  // to call nimble_port_deinit() and start a fresh nimble_port_init().
  if (s_host_stopped_sem) xSemaphoreGive(s_host_stopped_sem);
  // Delete ourselves directly instead of calling nimble_port_freertos_deinit().
  // nimble_port_freertos_deinit() calls nimble_port_stop() internally in some
  // ESP-IDF versions, which would post a spurious stop-event to the new NimBLE
  // instance's event queue (started by nimble_port_init() on the other task),
  // causing the new nimble_host task to exit its run-loop immediately and panic.
  // vTaskDelete(NULL) only touches FreeRTOS kernel structures — it is safe to
  // call while nimble_port_deinit()/init() runs concurrently on the split task.
  vTaskDelete(NULL);
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void ble_hid_init(void) {
  esp_err_t ret;

  // Create the host-stopped semaphore once — survives across reinits.
  if (s_host_stopped_sem == NULL) {
      s_host_stopped_sem = xSemaphoreCreateBinary();
      if (s_host_stopped_sem == NULL) {
          ESP_LOGE(TAG, "Failed to create stopped semaphore");
          return;
      }
  }

  // Reset per-connection state; pairing/directed profiles are set by the
  // caller before ble_hid_init() so that bleprph_on_sync → ble_hid_advertise
  // uses the right values without a race.
  for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
      s_conn_handles[i] = BLE_HS_CONN_HANDLE_NONE;
  }
  s_pairing_profile = -1;
  s_reconnect_retries = 0;

  // 1. Init the NimBLE transport (HCI over VHCI for integrated controller)
  ret = nimble_port_init();
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
      return;
  }

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
  ble_store_config_init(); // CRITICAL: Enables loading/saving bonds to NVS
  ble_hs_cfg.store_read_cb = ble_store_config_read;
  ble_hs_cfg.store_write_cb = ble_store_config_write;

  // Final identity check
  ble_hid_dump_bonds();

  // 5. Register GATT services
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_hid_svc_register();

  // 6. Set device name and appearance (GAP)
  // Priority: ble_shared_name (split identity) > device_name > compile-time fallback.
  cfg_system_t sys;
  const char *dev_name = BLE_DEVICE_NAME; // fallback
  if (cfg_system_get(&sys) == ESP_OK) {
      if (sys.ble_shared_name[0] != '\0') {
          dev_name = sys.ble_shared_name;
      } else if (sys.device_name[0] != '\0') {
          dev_name = sys.device_name;
      }
  }
  ret = ble_svc_gap_device_name_set(dev_name);
  assert(ret == 0);

  ret = ble_svc_gap_device_appearance_set(BLE_APPEARANCE_HID_KEYBOARD);
  assert(ret == 0);

  // 7. Spin up the NimBLE FreeRTOS task
  nimble_port_freertos_init(ble_host_task);

  // 8. Create advertising timers once — they survive across reinits.
  if (s_adv_cooldown_timer == NULL) {
      const esp_timer_create_args_t cooldown_timer_args = {
          .callback = ble_hid_adv_timer_cb,
          .name = "adv_cooldown"
      };
      esp_timer_create(&cooldown_timer_args, &s_adv_cooldown_timer);
  }
  if (s_pairing_timeout_timer == NULL) {
      const esp_timer_create_args_t pairing_timer_args = {
          .callback = ble_hid_pairing_timeout_cb,
          .name = "pairing_timeout"
      };
      esp_timer_create(&pairing_timer_args, &s_pairing_timeout_timer);
  }

  // 9. Listen for BLE connection config updates so that if advertising started
  // in undirected mode (profile not yet valid — config sync race on role swap),
  // it upgrades to directed advertising as soon as the profile data arrives.
  // CONFIG_EVENT_KIND_UPDATED payload kind=2 == CFGMOD_KIND_CONNECTION.
  // Only register once — the handler is not tied to the NimBLE task lifecycle.
  if (!s_config_evt_registered) {
      esp_event_handler_register(CONFIG_EVENTS, CONFIG_EVENT_KIND_UPDATED,
                                  ble_hid_on_ble_config_updated, NULL);
      esp_event_handler_register(SPLIT_EVENTS, SPLIT_EVENT_BLE_STATUS_UPDATED,
                                  ble_hid_on_split_status_updated, NULL);
      s_config_evt_registered = true;
  }

  ESP_LOGI(TAG, "BLE HID initialization complete");
}

// Fired by cfgmod whenever BLE connection config (profiles, routing) changes.
static void ble_hid_on_ble_config_updated(void *arg, esp_event_base_t base,
                                           int32_t event_id, void *data)
{
    const config_update_event_t *ev = (const config_update_event_t *)data;
    if (ev->kind != 2u) return; // 2 == CFGMOD_KIND_CONNECTION

    // If we have a pending directed reconnect that started in undirected mode
    // because the profile was not yet valid, restart now that profile data
    // has arrived (config sync just completed).
    if (s_directed_profile == -1 || s_is_suspended || !ble_hid_is_routing_active()) return;
    if (s_conn_handles[s_directed_profile] != BLE_HS_CONN_HANDLE_NONE) return; // already connected

    const cfg_ble_state_t *st = cfg_ble_get_state();
    if (st->profiles[s_directed_profile].is_valid && !ble_gap_adv_active()) {
        // Profile just became valid and advertising isn't running — or we can
        // always restart: ble_hid_advertise stops and restarts cleanly.
        ESP_LOGI(TAG, "Profile %d now valid after config sync — upgrading to directed adv",
                 s_directed_profile);
        ble_hid_advertise();
    }
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
  if (!st->ble_routing_enabled || s_is_suspended) {
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

  const cfg_ble_state_t *st = cfg_ble_get_state();
  if (!st->ble_routing_enabled || s_is_suspended) {
      return ESP_OK; // Silently drop, acting as "OFF"
  }

  int rc = ble_hid_tx_consumer_report(handle, media_keycode);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed consumer tx: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

void ble_hid_profile_pair(uint8_t profile_id) {
    s_reconnect_pending_bitmap = 0;
    s_reconn_phase = RECONN_PHASE_SELECTED;
    s_selected_retries_left = 5;
    memset(s_reconnect_cycles, 0, sizeof(s_reconnect_cycles));
    s_reconnect_last_profile = -1;
    ESP_LOGI(TAG, "[BLE API] Handling HOLD (Pair) for profile %d", profile_id);

    // Erase old credentials if valid and mark profile as invalid
    const cfg_ble_state_t *st = cfg_ble_get_state();
    bool was_valid = st->profiles[profile_id].is_valid;

    cfg_ble_state_t new_state = *st;
    new_state.profiles[profile_id].is_valid = false;
    // Rotate the address nonce so we appear as a new device to the phone
    new_state.profiles[profile_id].addr_nonce++;
    new_state.has_unsynced_updates = 1; // Mark as dirty since we erased and rotated
    new_state.sync_version++; // Update version to push changes to split counterpart
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
    s_reconnect_pending_bitmap = 0;
    s_reconn_phase = RECONN_PHASE_SELECTED;
    s_selected_retries_left = 5;
    memset(s_reconnect_cycles, 0, sizeof(s_reconnect_cycles));
    s_reconnect_last_profile = -1;
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
    s_reconnect_pending_bitmap = 0;
    s_reconn_phase = RECONN_PHASE_SELECTED;
    s_selected_retries_left = 5;
    memset(s_reconnect_cycles, 0, sizeof(s_reconnect_cycles));
    s_reconnect_last_profile = -1;
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

/*
 * Completely tear down the NimBLE host stack.
 * Blocks until the NimBLE task has exited so callers can immediately call
 * ble_hid_init() afterwards for a clean reinit.
 */
static void ble_hid_deinit(void) {
    ESP_LOGI(TAG, "BLE deinit: stopping NimBLE host stack.");

    // Stop timers so their callbacks don't fire during teardown.
    if (s_adv_cooldown_timer)   esp_timer_stop(s_adv_cooldown_timer);
    if (s_pairing_timeout_timer) esp_timer_stop(s_pairing_timeout_timer);

    // Stop advertising (no-op if nothing is running).
    ble_gap_adv_stop();

    // Terminate any active BLE connections gracefully.
    for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
        if (s_conn_handles[i] != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conn_handles[i], 0x15);
            s_conn_handles[i] = BLE_HS_CONN_HANDLE_NONE;
        }
    }

    // Signal the NimBLE run loop to exit, then wait for ble_host_task to
    // give s_host_stopped_sem (just before it calls vTaskDelete).
    nimble_port_stop();
    if (s_host_stopped_sem) {
        xSemaphoreTake(s_host_stopped_sem, pdMS_TO_TICKS(3000));
    }

    // Workaround for ESP-IDF core panic:
    // NimBLE uses `esp_timer` callbacks that might fire exactly as the host task is tearing down.
    // By pausing for 50ms here, we allow any such `esp_timer` callbacks (like NPL link expiration 
    // or pairing timeouts) to cleanly route their events onto the RTOS event queue *before* 
    // `nimble_port_deinit()` fatally destroys the underlying RTOS queues.
    vTaskDelay(pdMS_TO_TICKS(50));

    // Now safe to release NimBLE's resources.
    nimble_port_deinit();

    // Reset volatile state so the next ble_hid_init() starts clean.
    s_pairing_profile   = -1;
    s_directed_profile  = -1;
    s_reconnect_retries = 0;
    ESP_LOGI(TAG, "BLE deinit complete.");
}

void ble_hid_set_suspended(bool suspended) {
    if (s_is_suspended == suspended) return;
    s_is_suspended = suspended;

    if (suspended) {
        ESP_LOGI(TAG, "BLE operations suspended. Terminating connections and stopping advertising.");
        for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
            if (s_conn_handles[i] != BLE_HS_CONN_HANDLE_NONE) {
                // 0x15 = "Remote Device Terminated Due to Power Off" (valid per BLE spec).
                // This causes the host to schedule a reconnect when the device comes back,
                // unlike 0x13 (Remote User Terminated) which the host treats as intentional
                // and won't reconnect from.  During a split role swap the other half
                // immediately starts advertising with the same address, so the host
                // should reconnect there within seconds.
                ble_gap_terminate(s_conn_handles[i], 0x15);
            }
        }
        ble_hid_advertise(); // Will stop advertising due to s_is_suspended check
    } else {
        // The NimBLE stack is already running and the resolving list was pre-warmed
        // by ble_hid_reinit_bonds() when the bond sync arrived while we were a slave.
        // All we need to do is reload the config and start advertising immediately.
        ESP_LOGI(TAG, "BLE operations resumed. Starting directed advertising (stack pre-warmed).");
        cfg_ble_reload();

        if (!ble_hid_is_routing_active()) {
            ESP_LOGI(TAG, "BLE routing disabled — not starting advertising.");
            return;
        }

        s_reconnect_pending_bitmap = 0;
        int sel = (int)cfg_ble_get_state()->selected_profile;
        
        // Priority 1: Use the latched bitmap from when we were a slave.
        // This survives the old Master's shutdown sequence (which might have wiped s_conn_handles).
        if (s_slave_mirror_conn_bitmap != 0) {
            s_reconnect_pending_bitmap = s_slave_mirror_conn_bitmap;
            ESP_LOGI(TAG, "Resuming as MASTER. Using LATCHED bitmap from Slave period: 0x%02X", 
                     (unsigned int)s_reconnect_pending_bitmap);
        } else {
            // Fallback: Check handles directly (unlikely to have many if shutdown wipe occurred).
            for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
                if (s_conn_handles[i] == BLE_CONN_HANDLE_REMOTE && cfg_ble_get_state()->profiles[i].is_valid) {
                    s_reconnect_pending_bitmap |= (1 << i);
                }
            }
        }

        s_slave_mirror_conn_bitmap = 0; // Clear latch after promotion.

        memset(s_reconnect_cycles, 0, sizeof(s_reconnect_cycles));
        // Start the round-robin one step before 'sel' so that start_next_reconnect() 
        // picks 'sel' first (the currently active profile).
        s_reconnect_last_profile = (sel + CFG_BLE_MAX_PROFILES - 1) % CFG_BLE_MAX_PROFILES;

        if (s_reconnect_pending_bitmap != 0) {
            ESP_LOGI(TAG, "Resuming as MASTER with multiple connected profiles. Phases → FINITE (bitmap=0x%02X)", 
                     (unsigned int)s_reconnect_pending_bitmap);
            s_reconn_phase = RECONN_PHASE_FINITE;
            ble_hid_start_next_reconnect();
        } else if (sel >= 0 && sel < CFG_BLE_MAX_PROFILES) {
            ESP_LOGI(TAG, "Resuming as MASTER. Standard Selected reconnection for profile %d.", sel);
            s_directed_profile = sel;
            s_reconn_phase = RECONN_PHASE_SELECTED;
            s_selected_retries_left = 6;
            s_reconnect_cycles[sel] = s_skip_directed_on_resume ? 1 : 0;
            s_skip_directed_on_resume = false;
            ble_hid_advertise();
        } else {
            s_reconn_phase = RECONN_PHASE_FOREVER;
            ble_hid_start_next_reconnect();
        }
    }
}

void ble_hid_reinit_bonds(void) {
    // A full deinit+init forces ble_store_config_init() to run again, which reads
    // all bond data from NVS and naturally re-warms the BLE controller's hardware resolving list.
    if (!s_is_suspended) {
        // Reinit is safe only if there are no live connections to drop.
        bool any_connected = false;
        for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
            if (s_conn_handles[i] != BLE_HS_CONN_HANDLE_NONE) {
                any_connected = true;
                break;
            }
        }
        if (any_connected) {
            ESP_LOGW(TAG, "ble_hid_reinit_bonds: active connections — skipping.");
            return;
        }
        const cfg_ble_state_t *st = cfg_ble_get_state();
        s_post_reinit_directed_profile = (int)st->selected_profile;
        s_reconnect_retries            = 0;
        ESP_LOGI(TAG, "ble_hid_reinit_bonds: reiniting NimBLE while master.");
        ble_hid_deinit();
        ble_hid_init();
        return;
    }
    ESP_LOGI(TAG, "ble_hid_reinit_bonds: reiniting NimBLE to warm controller resolving list.");
    ble_hid_deinit();
    ble_hid_init();
}

void ble_hid_skip_directed_adv(void) {
    s_skip_directed_on_resume = true;
}

bool ble_hid_is_suspended(void) {
    return s_is_suspended;
}

// Fired on SLAVE whenever the master pushes a status update (heartbeat/BLE event).
static void ble_hid_on_split_status_updated(void *arg, esp_event_base_t base,
                                             int32_t event_id, void *data) {
    (void)arg; (void)base; (void)event_id;
#if CONFIG_SPLIT_SUPPORT
    const split_ble_status_t *ev = (const split_ble_status_t *)data;
    (void)ev;
#endif

    // We only mirror the master's state if we are currently operating as a slave.
    // If we are a standalone device or the current master, we own our own state.
#if CONFIG_SPLIT_SUPPORT
    if (splitmod_is_enabled() && splitmod_get_role() == SPLIT_ROLE_SLAVE) {
        // Sync selected profile so background advertising targets the same slot.
        // We only save to NVS if it actually changed to avoid excessive wear.
        const cfg_ble_state_t *st = cfg_ble_get_state();
        if (st->selected_profile != ev->selected_profile) {
            cfg_ble_state_t new_st = *st;
            new_st.selected_profile = ev->selected_profile;
            cfg_ble_save_state(&new_st);
            ESP_LOGI(TAG, "Slave sync: saved Master's profile %d to NVS", (int)new_st.selected_profile);
        } else {
            // Update in-memory only if no change needed in NVS.
            cfg_ble_set_selected_profile(ev->selected_profile);
        }
        
        // Sync connection bitmap...
        // SLAVE LATCH: We always add new connections to our mirror, but we are 
        // extremely cautious about clearing them, to avoid the "Master Shutdown Wipe" 
        // during role swaps.
        s_slave_mirror_conn_bitmap |= ev->connected_bitmap;

        for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
            bool is_conn_on_master = (ev->connected_bitmap & (1 << i)) != 0;
            if (is_conn_on_master) {
                if (s_conn_handles[i] == BLE_HS_CONN_HANDLE_NONE) {
                    s_conn_handles[i] = BLE_CONN_HANDLE_REMOTE; 
                }
            } else {
                // Regular sync clearing (e.g. user manually disconnected on Master).
                // We keep the latch (s_slave_mirror_conn_bitmap) intact just in case
                // this is actually a role-swap shutdown.
                s_conn_handles[i] = BLE_HS_CONN_HANDLE_NONE; 
            }
        }
        
        // If advertising is running, restart it to apply the new profile/connection state.
        if (ble_gap_adv_active()) {
            ble_hid_advertise();
        }
    }
#endif
}

static void ble_hid_dump_bonds(void) {
    // Note: Diagnostics can be expanded here if needed to track LTK count.
    ESP_LOGI(TAG, "Bond store persistence is active.");
}

void ble_hid_seed_handover_state(uint16_t bitmap, int8_t selected_profile) {
    if (selected_profile >= 0 && selected_profile < CFG_BLE_MAX_PROFILES) {
        ESP_LOGI(TAG, "Handover: syncing selected profile to %d", (int)selected_profile);
        cfg_ble_set_selected_profile((uint8_t)selected_profile);
    }

    if (bitmap != 0) {
        ESP_LOGI(TAG, "Handover: seeding restoration latch with bitmap 0x%02X", (unsigned int)bitmap);
        s_slave_mirror_conn_bitmap |= bitmap;
    }
}
