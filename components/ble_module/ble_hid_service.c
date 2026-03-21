/**
 * BLE HID Keyboard Service (HOGP-compliant)
 *
 * Implements the minimum required GATT services for a BLE HID keyboard:
 *   - Device Information Service (DIS) with PnP ID
 *   - Battery Service (BAS) with Battery Level
 *   - HID Service with Report Map, HID Info, Protocol Mode,
 *     HID Control Point, and Input Report
 *
 * Key design decisions:
 *   - Report IDs used (1 = keyboard, 2 = consumer) — required for multi-report maps
 *   - Protocol Mode uses READ | WRITE_NO_RSP (HOGP requirement)
 *   - NimBLE auto-creates CCCDs for NOTIFY characteristics
 *   - Uses ENC (not AUTHEN) so service discovery works before pairing
 */

#include "ble_hid_service.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "kb_state.h"

static const char *TAG = "ble_hid_svc";

/* ========================================================================= */
/* UUID Definitions                                                          */
/* ========================================================================= */

/* HID Service */
#define UUID_HID_SVC            0x1812
#define UUID_HID_INFORMATION    0x2A4A
#define UUID_HID_REPORT_MAP     0x2A4B
#define UUID_HID_CONTROL_POINT  0x2A4C
#define UUID_HID_REPORT         0x2A4D
#define UUID_HID_PROTOCOL_MODE  0x2A4E
#define UUID_HID_BOOT_KEYB_IN   0x2A22
#define UUID_HID_BOOT_KEYB_OUT  0x2A32

/* Battery Service */
#define UUID_BAS_SVC            0x180F
#define UUID_BAS_BATTERY_LEVEL  0x2A19

/* Device Information Service */
#define UUID_DIS_SVC            0x180A
#define UUID_DIS_PNP_ID         0x2A50

/* Report Reference Descriptor */
#define UUID_REPORT_REFERENCE   0x2908

/* Report IDs */
#define BLE_REPORT_ID_KEYBOARD  1
#define BLE_REPORT_ID_CONSUMER  2

/* ========================================================================= */
/* Macros                                                                    */
/* ========================================================================= */

/* Encodes Report ID and Type into a void* arg for hid_report_ref_access_cb.
 * High byte = Report ID, low byte = Report Type (1=Input, 2=Output, 3=Feature). */
#define REPORT_REF_ARG(id, type) ((void *)(uintptr_t)(((id) << 8) | (type)))

/* ========================================================================= */
/* HID Data                                                                  */
/* ========================================================================= */

/* HID Information: v1.11, no country code, normally connectable */
static const uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x03};

/**
 * HID Report Map — 6KRO Keyboard + Consumer Control
 *
 * Report ID 1 — Keyboard (8 bytes):
 *   Byte 0:   Modifier bitmap (Ctrl/Shift/Alt/GUI × Left/Right)
 *   Byte 1:   Reserved (always 0)
 *   Bytes 2–7: Up to 6 simultaneous key codes (6KRO)
 *
 * Report ID 2 — Consumer Control (2 bytes):
 *   16-bit HID Consumer Control usage ID (0 = release)
 */
static const uint8_t hid_report_map[] = {
    // --- KEYBOARD REPORT ---
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x85, BLE_REPORT_ID_KEYBOARD, // Report ID (1)

    // --- Modifier keys (1 byte = 8 bits) ---
    0x05, 0x07, //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0, //   Usage Minimum (Left Control)
    0x29, 0xE7, //   Usage Maximum (Right GUI)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x02, //   Input (Data, Variable, Absolute)

    // --- Reserved byte (1 byte) ---
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8)
    0x81, 0x03, //   Input (Constant, Variable, Absolute)

    // --- LED output report (5 bits + 3 padding) ---
    0x95, 0x05, //   Report Count (5)
    0x75, 0x01, //   Report Size (1)
    0x05, 0x08, //   Usage Page (LEDs)
    0x19, 0x01, //   Usage Minimum (Num Lock)
    0x29, 0x05, //   Usage Maximum (Kana)
    0x91, 0x02, //   Output (Data, Variable, Absolute)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x03, //   Report Size (3)
    0x91, 0x03, //   Output (Constant, Variable, Absolute)

    // --- Key array (6 bytes) ---
    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x65, //   Logical Maximum (101)
    0x05, 0x07, //   Usage Page (Keyboard/Keypad)
    0x19, 0x00, //   Usage Minimum (0)
    0x29, 0x65, //   Usage Maximum (101)
    0x81, 0x00, //   Input (Data, Array, Absolute)
    0xC0,       // End Collection

    // --- CONSUMER CONTROL (MEDIA) REPORT ---
    0x05, 0x0C, // Usage Page (Consumer)
    0x09, 0x01, // Usage (Consumer Control)
    0xA1, 0x01, // Collection (Application)
    0x85, BLE_REPORT_ID_CONSUMER, // Report ID (2)

    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x03, //   Logical Maximum (0x03FF = 1023)
    0x19, 0x00,       //   Usage Minimum (0)
    0x2A, 0xFF, 0x03, //   Usage Maximum (1023)

    0x75, 0x10, //   Report Size (16 bits)
    0x95, 0x01, //   Report Count (1)
    0x81, 0x00, //   Input (Data, Array, Absolute)
    0xC0        // End Collection
};

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

static uint8_t  s_hid_protocol_mode = 1; // 1 = Report Protocol (default)
static uint8_t  s_hid_control_point = 0;
static uint8_t  s_hid_input_report[8] = {0}; // 8 bytes
static uint16_t s_hid_consumer_report = 0;   // 2 bytes

/* Attribute handles (set by NimBLE during registration) */
static uint16_t s_hid_input_report_handle;
static uint16_t s_hid_output_report_handle;
static uint16_t s_hid_consumer_report_handle;
static uint16_t s_hid_boot_in_handle;
static uint16_t s_hid_boot_out_handle;
static uint16_t s_bas_battery_level_handle;

/* ========================================================================= */
/* Callback Forward Declarations                                             */
/* ========================================================================= */

static int hid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int dis_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int bas_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_report_ref_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg);

/* ========================================================================= */
/* GATT Service Table                                                        */
/* ========================================================================= */

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* ---- Device Information Service ---- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_DIS_SVC),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    /* PnP ID */
                    .uuid = BLE_UUID16_DECLARE(UUID_DIS_PNP_ID),
                    .access_cb = dis_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                },
                {0},
            },
    },

    /* ---- Battery Service ---- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_BAS_SVC),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    /* Battery Level — READ + NOTIFY
                     * NimBLE auto-creates the CCCD (0x2902) descriptor
                     * when NOTIFY flag is set. Do NOT add it manually. */
                    .uuid = BLE_UUID16_DECLARE(UUID_BAS_BATTERY_LEVEL),
                    .access_cb = bas_access_cb,
                    .val_handle = &s_bas_battery_level_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_NOTIFY,
                },
                {0},
            },
    },

    /* ---- HID Service ---- */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_HID_SVC),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    /* HID Information */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_INFORMATION),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                    .arg = (void *)(uintptr_t)UUID_HID_INFORMATION,
                },
                {
                    /* Report Map */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT_MAP),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                    .arg = (void *)(uintptr_t)UUID_HID_REPORT_MAP,
                },
                {
                    /* Protocol Mode — HOGP mandates READ | WRITE_NO_RSP */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_PROTOCOL_MODE),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .arg = (void *)(uintptr_t)UUID_HID_PROTOCOL_MODE,
                },
                {
                    /* Boot Keyboard Input Report — READ + NOTIFY */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_BOOT_KEYB_IN),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_hid_boot_in_handle,
                    .arg = (void *)(uintptr_t)UUID_HID_BOOT_KEYB_IN,
                },
                {
                    /* Boot Keyboard Output Report — READ + WRITE + WRITE_NO_RSP */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_BOOT_KEYB_OUT),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &s_hid_boot_out_handle,
                    .arg = (void *)(uintptr_t)UUID_HID_BOOT_KEYB_OUT,
                },
                {
                    /* HID Control Point */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_CONTROL_POINT),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .arg = (void *)(uintptr_t)UUID_HID_CONTROL_POINT,
                },
                {
                    /* Input Report (Keyboard) — READ + NOTIFY
                     * NimBLE auto-creates the CCCD (0x2902).
                     * We manually add the Report Reference (0x2908). */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_hid_input_report_handle,
                    .arg = (void *)(uintptr_t)UUID_HID_REPORT,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            {
                                /* Report Reference: Report ID=1, Type=Input */
                                .uuid = BLE_UUID16_DECLARE(UUID_REPORT_REFERENCE),
                                .access_cb = hid_report_ref_access_cb,
                                .att_flags = BLE_ATT_F_READ,
                                .arg = REPORT_REF_ARG(BLE_REPORT_ID_KEYBOARD, 1),
                            },
                            {0},
                        },
                },
                {
                    /* Input Report (Consumer) — READ + NOTIFY */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_hid_consumer_report_handle,
                    .arg = (void *)(uintptr_t)UUID_HID_REPORT,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            {
                                /* Report Reference: Report ID=2, Type=Input */
                                .uuid = BLE_UUID16_DECLARE(UUID_REPORT_REFERENCE),
                                .access_cb = hid_report_ref_access_cb,
                                .att_flags = BLE_ATT_F_READ,
                                .arg = REPORT_REF_ARG(BLE_REPORT_ID_CONSUMER, 1),
                            },
                            {0},
                        },
                },
                {
                    /* Output Report — READ + WRITE + WRITE_NO_RSP */
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_REPORT),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &s_hid_output_report_handle,
                    .arg = (void *)(uintptr_t)UUID_HID_REPORT,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            {
                                /* Report Reference: Report ID=1, Type=Output */
                                .uuid = BLE_UUID16_DECLARE(UUID_REPORT_REFERENCE),
                                .access_cb = hid_report_ref_access_cb,
                                .att_flags = BLE_ATT_F_READ,
                                .arg = REPORT_REF_ARG(BLE_REPORT_ID_KEYBOARD, 2),
                            },
                            {0},
                        },
                },
                {0},
            },
    },

    {0}, /* End of services */
};

/* ========================================================================= */
/* Callbacks                                                                 */
/* ========================================================================= */

static int dis_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
  /* PnP ID: Bluetooth SIG vendor source (0x01), Vendor ESPRESSIF (0x02E5),
   * Product=0x0121, Version=1.0 */
  static const uint8_t pnp_id[7] = {0x01, 0xE5, 0x02, 0x21, 0x01, 0x01, 0x01};
  int rc = os_mbuf_append(ctxt->om, pnp_id, sizeof(pnp_id));
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int bas_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
  uint8_t battery_level = 69;
  int rc = os_mbuf_append(ctxt->om, &battery_level, sizeof(battery_level));
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int hid_report_ref_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg) {
  uint16_t arg_val = (uint16_t)(uintptr_t)arg;
  uint8_t report_id   = (arg_val >> 8) & 0xFF;
  uint8_t report_type = arg_val & 0xFF;

  uint8_t report_ref[2] = {report_id, report_type};
  int rc = os_mbuf_append(ctxt->om, report_ref, sizeof(report_ref));
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int hid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
  uint16_t uuid = (uint16_t)(uintptr_t)arg;
  int rc;

  switch (uuid) {

  case UUID_HID_INFORMATION:
    rc = os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

  case UUID_HID_REPORT_MAP:
    rc = os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

  case UUID_HID_PROTOCOL_MODE:
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      rc = os_mbuf_append(ctxt->om, &s_hid_protocol_mode,
                          sizeof(s_hid_protocol_mode));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      if (ctxt->om->om_len == 1) {
        s_hid_protocol_mode = ctxt->om->om_data[0];
        ESP_LOGI(TAG, "Host set Protocol Mode to %d", s_hid_protocol_mode);
        return 0;
      }
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    break;

  case UUID_HID_CONTROL_POINT:
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      if (ctxt->om->om_len == 1) {
        s_hid_control_point = ctxt->om->om_data[0];
        ESP_LOGI(TAG, "HID Control Point: %d", s_hid_control_point);
        return 0;
      }
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    break;

  case UUID_HID_REPORT:
    if (attr_handle == s_hid_input_report_handle) {
      if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        rc = os_mbuf_append(ctxt->om, s_hid_input_report,
                            sizeof(s_hid_input_report));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      }
    } else if (attr_handle == s_hid_consumer_report_handle) {
      if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        rc = os_mbuf_append(ctxt->om, &s_hid_consumer_report,
                            sizeof(s_hid_consumer_report));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      }
    } else if (attr_handle == s_hid_output_report_handle) {
      if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t out_report = kb_state_get_leds();
        rc = os_mbuf_append(ctxt->om, &out_report, 1);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len == 1) {
          kb_state_update_leds(ctxt->om->om_data[0]);
          return 0;
        }
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
    }
    break;

  case UUID_HID_BOOT_KEYB_IN:
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      rc = os_mbuf_append(ctxt->om, s_hid_input_report, sizeof(s_hid_input_report));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    break;

  case UUID_HID_BOOT_KEYB_OUT:
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      uint8_t out_report = kb_state_get_leds();
      rc = os_mbuf_append(ctxt->om, &out_report, 1);
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      if (ctxt->om->om_len == 1) {
        kb_state_update_leds(ctxt->om->om_data[0]);
        return 0;
      }
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    break;

  default:
    break;
  }

  return BLE_ATT_ERR_UNLIKELY;
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void ble_hid_svc_register(void) {
  int rc;

  rc = ble_gatts_count_cfg(gatt_svr_svcs);
  assert(rc == 0);

  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  assert(rc == 0);

  ESP_LOGI(TAG, "HID GATT services registered");
}

int ble_hid_tx_keyboard_report(uint16_t conn_handle, const uint8_t *report,
                               size_t len) {
  if (len != 8) {
    ESP_LOGE(TAG, "Invalid report len=%d (expected 8)", (int)len);
    return BLE_HS_EINVAL;
  }

  /* Update local state (so read-back returns current value) */
  memcpy(s_hid_input_report, report, 8);

  struct os_mbuf *om = ble_hs_mbuf_from_flat(s_hid_input_report, 8);
  if (!om) {
    return BLE_HS_ENOMEM;
  }

  /* Boot protocol mandates an 8-byte report without Report ID */
  if (s_hid_protocol_mode == 0) {
    return ble_gatts_notify_custom(conn_handle, s_hid_boot_in_handle, om);
  } else {
    return ble_gatts_notify_custom(conn_handle, s_hid_input_report_handle, om);
  }
}

int ble_hid_tx_consumer_report(uint16_t conn_handle, uint16_t media_keycode) {
  s_hid_consumer_report = media_keycode;

  struct os_mbuf *om = ble_hs_mbuf_from_flat(&s_hid_consumer_report, 2);
  if (!om) {
    return BLE_HS_ENOMEM;
  }

  return ble_gatts_notify_custom(conn_handle, s_hid_consumer_report_handle, om);
}

int ble_hid_notify_battery_level(uint16_t conn_handle, uint8_t level) {
  struct os_mbuf *om = ble_hs_mbuf_from_flat(&level, 1);
  if (!om) {
    return BLE_HS_ENOMEM;
  }
  return ble_gatts_notify_custom(conn_handle, s_bas_battery_level_handle, om);
}
