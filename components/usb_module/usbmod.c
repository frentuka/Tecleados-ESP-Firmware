#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usb_callbacks.h"
#include "usb_descriptors.h"
#include "usbmod.h"
#include "cfg_system.h"
#include "comm_module.h"
#include "comm_transport.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#define TAG "USBModule"

// ============ USB STATE CALLBACKS ============

static void usb_event_cb(tinyusb_event_t *event, void *arg) {
  switch (event->id) {
  case TINYUSB_EVENT_ATTACHED:
    ESP_LOGI(TAG, "USB mounted (host connected)");
    comm_transport_set_connected(COMM_TRANSPORT_USB, true);
    break;
  case TINYUSB_EVENT_DETACHED:
    ESP_LOGI(TAG, "USB unmounted (host disconnected)");
    comm_transport_set_connected(COMM_TRANSPORT_USB, false);
    break;
  default:
    break;
  }
}

/*
    handle callbacks to usb_callbacks
    (workaround to tinyusb not wanting to link with usb_callbacks)
*/

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  return usbmod_tud_hid_get_report_cb(instance, report_id, report_type, buffer,
                                      reqlen);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  usbmod_tud_hid_set_report_cb(instance, report_id, report_type, buffer,
                               bufsize);
}

// ============ KEYBOARD HID FUNCTIONS ============

bool usb_keyboard_use_boot_protocol(void) {
  return (tud_hid_n_get_protocol(ITF_NUM_HID_KBD) == HID_PROTOCOL_BOOT);
}

bool usb_send_keyboard_6kro(uint8_t modifier, const uint8_t keycodes[6]) {
  return tud_hid_n_keyboard_report(ITF_NUM_HID_KBD, REPORT_ID_KEYBOARD,
                                   modifier, keycodes);
}

bool usb_send_keyboard_nkro(uint8_t modifier, const uint8_t *bitmap,
                             uint16_t len) {
  uint8_t buf[len + 1];
  buf[0] = modifier;
  memcpy(buf + 1, bitmap, len);
  return tud_hid_n_report(ITF_NUM_HID_KBD, REPORT_ID_NKRO, buf, len + 1);
}

bool usb_send_consumer_report(uint16_t keycode) {
  if (tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD)) {
    return tud_hid_n_report(ITF_NUM_HID_KBD, REPORT_ID_CONSUMER, &keycode, 2);
  }
  return false;
}

// Test functions

static void block_until_kb_ready() {
  while (!tud_hid_n_ready(ITF_NUM_HID_KBD)) {
    continue;
  }
}

void usb_send_char(char c) {
  uint8_t const conv_table[128][2] = {HID_ASCII_TO_KEYCODE};
  uint8_t uichar = (uint8_t)c;
  if (uichar >= 128)
    return;

  uint8_t kc = conv_table[uichar][1];
  if (kc == 0)
    return;

  uint8_t mod = conv_table[uichar][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
  uint8_t keys[6] = {kc};

  if (tud_mounted()) {
    tud_hid_n_keyboard_report(ITF_NUM_HID_KBD, REPORT_ID_KEYBOARD, mod, keys);
    block_until_kb_ready();
    uint8_t no_keys[6] = {0};
    tud_hid_n_keyboard_report(ITF_NUM_HID_KBD, REPORT_ID_KEYBOARD, 0, no_keys);
  }
}

void usb_send_keystroke(uint8_t hid_keycode) {
  if (tud_mounted()) {
    uint8_t keycode[6] = {hid_keycode};
    tud_hid_n_keyboard_report(ITF_NUM_HID_KBD, REPORT_ID_KEYBOARD, 0,
                              keycode); // Press
    block_until_kb_ready();
    uint8_t no_keys[6] = {0};
    tud_hid_n_keyboard_report(ITF_NUM_HID_KBD, REPORT_ID_KEYBOARD, 0,
                              no_keys); // Release
    ESP_LOGI(TAG, "Sent keystroke");
  }
}


/*
    Main USB module
*/

// Transport operations for comm_module
static bool usbmod_send_packet(const uint8_t *packet, uint16_t len) {
    if (!tud_mounted() || !tud_hid_n_ready(ITF_NUM_HID_COMM)) {
        return false;
    }
    // Convert generic packet pointer to standard void pointer for tud_hid_n_report
    return tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, len);
}

static bool usbmod_is_ready(void) {
    return tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_COMM);
}

static uint16_t usbmod_get_max_packet_size(void) {
    return COMM_REPORT_SIZE;
}

static const comm_transport_ops_t s_usb_ops = {
    .send_packet = usbmod_send_packet,
    .is_ready = usbmod_is_ready,
    .get_max_packet_size = usbmod_get_max_packet_size
};

// TinyUSB task
void usb_task(void *arg) {
  while (1) {
    tud_task();
    taskYIELD();
  }
}

void usb_init() {
  tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
  tusb_cfg.descriptor.device = &desc_device;
  tusb_cfg.descriptor.full_speed_config = desc_configuration;

  // 32 (name) + 3 (" (") + 16 (variant) + 1 (")") + 1 (NUL) = 53
  static char s_override_product_name[53];
  static char s_override_serial_number[16];

  cfg_system_t sys;
  if (cfg_system_get(&sys) == ESP_OK && sys.device_name[0] != '\0') {
      if (sys.split_variant[0] != '\0') {
          snprintf(s_override_product_name, sizeof(s_override_product_name),
                   "%s (%s)", sys.device_name, sys.split_variant);
      } else {
          strncpy(s_override_product_name, sys.device_name, sizeof(s_override_product_name) - 1);
          s_override_product_name[sizeof(s_override_product_name) - 1] = '\0';
      }
      string_desc_arr[2] = s_override_product_name;
  }

  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
      snprintf(s_override_serial_number, sizeof(s_override_serial_number),
               "%02X%02X%02X%02X%02X%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      string_desc_arr[3] = s_override_serial_number;
  }

  tusb_cfg.descriptor.string = (const char **)string_desc_arr;
  tusb_cfg.descriptor.string_count =
      sizeof(string_desc_arr) / sizeof(string_desc_arr[0]);
  tusb_cfg.event_cb = usb_event_cb;

  ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

  xTaskCreatePinnedToCoreWithCaps(usb_task, "usb_task", 4096, NULL, 5, NULL, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  comm_transport_register(COMM_TRANSPORT_USB, &s_usb_ops);
  comm_transport_set_connected(COMM_TRANSPORT_USB, tud_mounted());

  ESP_LOGI(TAG, "USB initialized !");
}