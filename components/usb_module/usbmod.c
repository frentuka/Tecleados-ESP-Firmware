#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


#include "usb_callbacks.h"
#include "usb_callbacks_rx.h"
#include "usb_callbacks_tx.h"
#include "usb_descriptors.h"
#include "usbmod.h"




#include "esp_err.h"
#include "esp_log.h"


#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#define TAG "USBModule"

// ============ USB STATE CALLBACKS ============

static void usb_event_cb(tinyusb_event_t *event, void *arg) {
  switch (event->id) {
  case TINYUSB_EVENT_ATTACHED:
    ESP_LOGI(TAG, "USB mounted (host connected)");
    break;
  case TINYUSB_EVENT_DETACHED:
    ESP_LOGI(TAG, "USB unmounted (host disconnected)");
    erase_tx_buffer();
    erase_rx_buffer();
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

// ======== Callbacks ========

void usbmod_register_callback(usb_msg_module_t callback_module,
                               usb_data_callback_t callback) {
  register_callback(callback_module, callback);
}

bool usbmod_execute_callback(usb_msg_module_t callback_module, uint8_t const *data,
                             uint16_t data_len) {
  return execute_callback(callback_module, data, data_len);
}

/*
    Main USB module
*/

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
  tusb_cfg.descriptor.string = (const char **)string_desc_arr;
  tusb_cfg.descriptor.string_count =
      sizeof(string_desc_arr) / sizeof(string_desc_arr[0]);
  tusb_cfg.event_cb = usb_event_cb;

  ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

  xTaskCreatePinnedToCore(usb_task, "usb_task", 4096, NULL, 5, NULL, 1);

  usb_callbacks_init();

  ESP_LOGI(TAG, "USB initialized !");
}