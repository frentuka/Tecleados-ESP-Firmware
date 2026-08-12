#include "usb_callbacks.h"
#include "usb_descriptors.h"
#include "kb_state.h"
#include "esp_log.h"
#include "comm_transport.h"
#include <stddef.h>

#define TAG "USB Callbacks"

uint16_t usbmod_tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t *buffer, uint16_t reqlen) {
  (void)report_type;
  (void)report_id;
  return 0;
}

void usbmod_tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t const *buffer, uint16_t bufsize) {
  // Keyboard state listener (LEDs)
  if (instance == ITF_NUM_HID_KBD && report_type == HID_REPORT_TYPE_OUTPUT) {
    uint8_t led_status = 0;
    if (report_id != 0 && bufsize >= 2) {
      led_status = buffer[1];
    } else if (report_id == 0 && bufsize >= 1) {
      led_status = buffer[0];
    } else if (bufsize >= 1) {
      led_status = buffer[bufsize > 1 ? 1 : 0];
    }
    kb_state_update_leds(led_status);
    return;
  }

  if (instance != ITF_NUM_HID_COMM) {
    return;
  }

  // Skip the report ID byte for COMM interface
  uint16_t payload_len = bufsize > 0 ? bufsize - 1 : 0;
  uint8_t const *payload = buffer + 1;

  if (payload_len == 0) return;

  // The TinyUSB-specific packet ingestion acts purely as a transport adapter.
  // It passes the packet to the comm_module.
  comm_transport_receive_packet(COMM_TRANSPORT_USB, payload, payload_len);
}

// HID report descriptor callback - return correct descriptor per interface
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  if (instance == ITF_NUM_HID_KBD) {
    return desc_hid_report_kbd;
  } else if (instance == ITF_NUM_HID_COMM) {
    return desc_hid_report_comm;
  }
  return NULL;
}