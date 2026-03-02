#include "usb_callbacks.h"
#include "usb_callbacks_rx.h"
#include "usb_callbacks_tx.h"

#include "usb_crc.h"
#include "usb_defs.h"
#include "usb_descriptors.h"
#include "usb_send.h"


#include "basic_utils.h"



#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "tinyusb.h"

#define TAG "USB Callbacks"

// ============ Packet processing queues ============

#define PROCESS_QUEUE_LENGTH 4
static QueueHandle_t rx_processing_queue = NULL;
static QueueHandle_t tx_processing_queue = NULL;

// ============ Callbacks ============

#include "kb_state.h"

// Updated HID callbacks to route by interface
uint16_t usbmod_tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t *buffer, uint16_t reqlen) {
  (void)report_type;
  (void)report_id;

  // Nothing is done here since all requests are interrupt based
  return 0;
}

void usbmod_tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t const *buffer, uint16_t bufsize) {
  // Keyboard state listener (LEDs)
  if (instance == ITF_NUM_HID_KBD && report_type == HID_REPORT_TYPE_OUTPUT) {
    uint8_t led_status = 0;
    if (report_id != 0 && bufsize >= 2) {
      // Buffer includes report ID if report_id != 0
      led_status = buffer[1];
    } else if (report_id == 0 && bufsize >= 1) {
      led_status = buffer[0];
    } else if (bufsize >= 1) {
      // Fallback
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

  ESP_LOGI(TAG, "HID Comms RX: %d bytes (payload only)", payload_len);

  // Validate payload availability (only full sized messages are allowed)
  if (payload_len == 0 || payload_len > sizeof(usb_packet_msg_t)) {
    ESP_LOGE(TAG, "Invalid payload length: %d", payload_len);
    return;
  }

  // create msg
  usb_packet_msg_t msg = {0};
  memcpy(&msg, payload, payload_len);

  // Validate payload's CRT
  if (!usb_crc_verify_packet(payload)) {
    ESP_LOGE(TAG, "Unable to validate payload. Responding with NAK.");
    build_send_single_msg_packet(PAYLOAD_FLAG_NAK, msg.remaining_packets, 0,
                                 NULL);
    return;
  }

  // message payload is correct and can be acknowledged
  build_send_single_msg_packet(PAYLOAD_FLAG_ACK, msg.remaining_packets, 0,
                               NULL);

  // check process queue initialization
  if (rx_processing_queue == NULL) {
    ESP_LOGE(TAG, "Process queue not initialized");
    return;
  }

  // send packet to be processed on another thread
  if (xQueueSend(rx_processing_queue, &msg, 0) != pdTRUE) {
    ESP_LOGE(TAG, "Process queue full, dropping packet");
  }
}

static void process_incoming_packet(usb_packet_msg_t msg) {
  ESP_LOGI(TAG, "Received payload. len: %u, flags: %u, remaining: %u",
           msg.payload_len, msg.flags, msg.remaining_packets);
  print_bytes_as_chars(TAG, msg.payload, msg.payload_len);

  bool is_rx = msg.flags & PAYLOAD_FLAG_FIRST || msg.flags & PAYLOAD_FLAG_MID ||
               msg.flags & PAYLOAD_FLAG_LAST;
  bool is_tx =
      !is_rx && (msg.flags & PAYLOAD_FLAG_ACK || msg.flags & PAYLOAD_FLAG_NAK ||
                 msg.flags & PAYLOAD_FLAG_OK || msg.flags & PAYLOAD_FLAG_ERR ||
                 msg.flags & PAYLOAD_FLAG_ABORT);

  // Verify payload's length
  if (is_rx && msg.payload_len == 0) {
    ESP_LOGE(TAG, "Received RX payload_len == 0");
    return;
  }

  if (is_rx) {
    ESP_LOGI(TAG, "process_incoming_packet: Processing RX-wise packet");
    process_rx_request(msg);
    return;
  }

  if (is_tx) {
    ESP_LOGI(TAG, "process_incoming_packet: Queuing TX-wise packet");
    if (tx_processing_queue == NULL) {
      ESP_LOGE(TAG, "tx_processing_queue not initialized");
      return;
    }
    if (xQueueSend(tx_processing_queue, &msg, 0) != pdTRUE) {
      ESP_LOGE(TAG, "TX Process queue full, dropping packet");
    }
    return;
  }
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

// ============ init ============

static void rx_processing_task(void *pvParameters) {
  usb_packet_msg_t msg;
  while (1) {
    // check for incoming packets
    if (xQueueReceive(rx_processing_queue, &msg, portMAX_DELAY) == pdTRUE) {
      process_incoming_packet(msg);
    }
  }
}

static void tx_processing_task(void *pvParameters) {
  usb_packet_msg_t msg;
  while (1) {
    // check for incoming packets
    if (xQueueReceive(tx_processing_queue, &msg, portMAX_DELAY) == pdTRUE) {
      process_tx_response(msg);
    }
  }
}

static void timeouts_task(void *pvParameters) {
  while (1) {
    uint64_t now_us = esp_timer_get_time();
    // check for packet timeout (rx_buf)
    uint64_t rx_timestamp_elapsed = now_us - rx_get_last_packet_timestamp_us();
    if (rx_get_last_packet_timestamp_us() &&
        rx_timestamp_elapsed > RX_TIMEOUT_MS * 1000) {
      erase_rx_buffer();
    }

    // check for packet timeout (rx_buf)
    uint64_t tx_timestamp_elapsed = now_us - tx_get_last_packet_timestamp_us();
    if (tx_get_last_packet_timestamp_us() &&
        tx_timestamp_elapsed > TX_TIMEOUT_MS * 1000) {
      erase_tx_buffer();
    }

    // Throttle loop to max 50hz
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static usb_data_callback_t s_module_callbacks[USB_MODULE_COUNT] = {0};

void register_callback(usb_msg_module_t callback_module,
                       usb_data_callback_t callback) {
  if (callback_module >= USB_MODULE_COUNT) {
    ESP_LOGE(TAG, "Failed to register callback: module > USB_MODULE_COUNT");
    return;
  }

  s_module_callbacks[callback_module] = callback;
}

bool execute_callback(usb_msg_module_t callback_module, uint8_t const *data,
                      uint16_t data_len) {
  if (callback_module >= USB_MODULE_COUNT) {
    ESP_LOGE(TAG, "Failed to execute callback: module > USB_MODULE_COUNT");
    return false;
  }

  if (!s_module_callbacks[callback_module]) {
    ESP_LOGE(TAG, "Failed to execute callback: callback not registered");
    return false;
  }

  return s_module_callbacks[callback_module]((uint8_t *)data, data_len);
}

void usb_callbacks_init(void) {
  rx_processing_queue =
      xQueueCreate(PROCESS_QUEUE_LENGTH, sizeof(usb_packet_msg_t));
  tx_processing_queue =
      xQueueCreate(PROCESS_QUEUE_LENGTH, sizeof(usb_packet_msg_t));

  if (rx_processing_queue == NULL || tx_processing_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create process queue");
    return;
  }

  // 32KB stack sizes (64KB total)
  xTaskCreate(rx_processing_task, "usb_rx_processing_task", 8192, NULL, 5,
              NULL);
  xTaskCreate(tx_processing_task, "usb_tx_processing_task", 8192, NULL, 5,
              NULL);
  xTaskCreate(timeouts_task, "usb_cb_timeouts_task", 3072, NULL, 5, NULL);
}