#include "blemod.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include <assert.h>

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_hid_service.h"

#define TAG "ble_hid_mod"

/* ========================================================================= */
/* Globals                                                                   */
/* ========================================================================= */

static uint16_t g_ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;

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
      g_ble_conn_handle = event->connect.conn_handle;

      int rc = ble_gap_security_initiate(event->connect.conn_handle);
      ESP_LOGI(TAG, "Security initiation requested: %d", rc);
    } else {
      ESP_LOGI(TAG, "Connection failed, restarting advertising");
      ble_hid_advertise();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Device disconnected, reason=%d", event->disconnect.reason);
    g_ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_hid_advertise(); // restart so it's discoverable again
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "Advertising complete");
    ble_hid_advertise(); // restart if it timed out
    break;

  case BLE_GAP_EVENT_ENC_CHANGE:
    // Gives us information when encryption and pairing process is complete
    if (event->enc_change.status == 0) {
      ESP_LOGI(TAG, "Connection successfully encrypted (pairing complete)");
    } else {
      ESP_LOGE(TAG, "Encryption failed, status=%d", event->enc_change.status);
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
  g_ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  // can attempt to re-advertise here or set a flag if needed
}

static void bleprph_on_sync(void) {
  int rc;

  // Make sure we have a valid address
  rc = ble_hs_util_ensure_addr(0); // 0 = prefer public, fallback to random
  assert(rc == 0);

  // Stack is ready — start advertising
  ESP_LOGI(TAG, "BLE stack synced, ready to advertise");
  ble_hid_advertise();
}

/* ========================================================================= */
/* Core Implementation                                                       */
/* ========================================================================= */

static void ble_hid_advertise(void) {
  struct ble_gap_adv_params adv_params = {0};
  struct ble_hs_adv_fields fields = {0};
  int rc;

  // --- Advertising data (what's broadcast) ---
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  // Appearance: HID keyboard (0x03C1)
  fields.appearance = 0x03C1;
  fields.appearance_is_present = 1;

  // Device name
  const char *name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)name;
  fields.name_len = (uint8_t)strlen(name);
  fields.name_is_complete = 1;

  // Advertise the HID service UUID so hosts know what we are
  static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
  fields.uuids16 = &hid_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  assert(rc == 0);

  // --- Advertising parameters ---
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // connectable
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // general discoverable

  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, // or BLE_OWN_ADDR_RANDOM
                         NULL,                // no direct peer
                         BLE_HS_FOREVER,      // advertise indefinitely
                         &adv_params,
                         ble_hid_gap_event, // your GAP event callback
                         NULL);
  assert(rc == 0);
  ESP_LOGI(TAG, "Advertising started");
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

  // 4. Register GATT services (you'll add your HID service here later)
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_hid_svc_register();

  // 5. Set device name and appearance (GAP)
  ret = ble_svc_gap_device_name_set("Tecleados MK1");
  assert(ret == 0);

  ret = ble_svc_gap_device_appearance_set(0x03C1); // 0x03C1 = HID Keyboard
  assert(ret == 0);

  // 6. Spin up the NimBLE FreeRTOS task
  nimble_port_freertos_init(ble_host_task);

  ESP_LOGI(TAG, "BLE HID initialization complete");
}

bool ble_hid_is_connected(void) {
  return (g_ble_conn_handle != BLE_HS_CONN_HANDLE_NONE);
}

esp_err_t ble_hid_send_keyboard_report(const uint8_t *report, size_t len) {
  if (!ble_hid_is_connected()) {
    return ESP_ERR_INVALID_STATE;
  }

  int rc = ble_hid_tx_keyboard_report(g_ble_conn_handle, report, len);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed characteristic tx: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}