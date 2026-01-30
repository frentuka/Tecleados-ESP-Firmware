#include "usb_callbacks.h"
#include "usb_descriptors.h"
#include "usb_crc.h"

#include "cfgmod.h"
#include "basic_utils.h"

#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "tinyusb.h"

#define TAG "usb_callbacks"

// ======== Packet processing queues ========
#define PROCESS_QUEUE_LENGTH 4
static QueueHandle_t rx_processing_queue = NULL;
static QueueHandle_t tx_processing_queue = NULL;

// ============ RX Buffer ============

#define RX_TIMEOUT_MS 1000 // max timeout for incoming sequential packets
#define MAX_RX_BUF_SIZE 21500 // 21.5KB -> 43(payload size) * 100(polling rate) * 5(seconds)
#define MAX_RX_BUF_SIZE_IN_PAYLOADS MAX_RX_BUF_SIZE / MAX_PAYLOAD_LENGTH // 500 packets long

static uint8_t rx_buf[MAX_RX_BUF_SIZE] = {0};
static uint16_t rx_buf_len = 0;
static uint64_t rx_last_packet_timestamp_us = 0;      // last received packet timestamp
static uint16_t rx_last_packet_remaining_packets = 0;

// ============ TX Buffer ============

#define TX_TIMEOUT_MS 1000
#define MAX_TX_BUF_SIZE 21500
#define MAX_TX_BUF_SIZE_IN_PAYLOADS MAX_TX_BUF_SIZE / MAX_PAYLOAD_LENGTH

static uint8_t tx_buf[MAX_TX_BUF_SIZE] = {0};
static uint16_t tx_buf_len = 0;
static uint16_t tx_buf_idx = 0;
static uint16_t tx_buf_last_packet_sent_idx = 0;
static uint64_t tx_last_packet_timestamp_us = 0; // last sent packet timestamp
static bool tx_awaiting_response = false;

// ============ Function declaration ============

static void erase_rx_buffer();
static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len);
static bool process_rx_buffer();

static void process_rx_request(const usb_packet_msg_t msg);
static void process_tx_response(const usb_packet_msg_t msg);

static bool build_send_single_msg_packet(uint8_t flag, uint16_t rem, uint8_t payload_len, uint8_t *payload);
static bool send_single_packet(uint8_t *packet, uint16_t packet_len);
static bool send_payload(const uint8_t *payload, uint16_t payload_len);

// ============ Callbacks ============

// Updated HID callbacks to route by interface
uint16_t usbmod_tud_hid_get_report_cb(uint8_t instance,
                              uint8_t report_id,
                              hid_report_type_t report_type,
                              uint8_t *buffer,
                              uint16_t reqlen)
{
    (void) report_type;
    (void) report_id;
    
    // Nothing is done here since all requests are interrupt based
    return 0;
}

void usbmod_tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void) report_type;

    // to-do: keyboard state listeners
    
    if (instance != ITF_NUM_HID_COMM) {
        return;
    }

    // Skip the report ID byte - buffer[0] is the report ID, actual data starts at buffer[1]
    // bufsize includes the report ID, so actual payload is (bufsize - 1)
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
        build_send_single_msg_packet(PAYLOAD_FLAG_NAK, msg.remaining_packets, 0, NULL);
        return;
    }

    // message payload is correct and can be acknowledged
    build_send_single_msg_packet(PAYLOAD_FLAG_ACK, msg.remaining_packets, 0, NULL);

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

static void process_incoming_packet(usb_packet_msg_t msg)
{
    ESP_LOGI(TAG, "Received payload. len: %u, flags: %u, remaining: %u", msg.payload_len, msg.flags, msg.remaining_packets);
    print_bytes_as_chars(TAG, msg.payload, msg.payload_len);

    // Verify payload's length
    if (msg.payload_len == 0) {
        ESP_LOGE(TAG, "Received payload_len == 0");
        return;
    }

    bool is_rx = msg.flags & PAYLOAD_FLAG_FIRST || msg.flags & PAYLOAD_FLAG_MID || msg.flags & PAYLOAD_FLAG_LAST;
    bool is_tx = !is_rx && (msg.flags & PAYLOAD_FLAG_ACK || msg.flags & PAYLOAD_FLAG_NAK || msg.flags & PAYLOAD_FLAG_OK || msg.flags & PAYLOAD_FLAG_ERR || msg.flags & PAYLOAD_FLAG_ABORT);
    
    if (is_rx) {
        process_rx_request(msg);
        return;
    }

    if (is_tx) {
        process_tx_response(msg);
        return;
    }
}

static void process_outgoing_packet(usb_packet_msg_t msg)
{

}

// HID report descriptor callback - return correct descriptor per interface
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
    if (instance == ITF_NUM_HID_KBD) {
        return desc_hid_report_kbd;
    } else if (instance == ITF_NUM_HID_COMM) {
        return desc_hid_report_comm;
    }
    return NULL;
}

// ======== Flag processing ========

static void process_rx_request(const usb_packet_msg_t msg)
{
    bool result = false;

    if (msg.flags & PAYLOAD_FLAG_FIRST) {
        ESP_LOGI(TAG, "Identified FIRST flag");

        erase_rx_buffer();
        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);

        if (!result) {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
        }
    }

    if (msg.flags & PAYLOAD_FLAG_MID) {
        ESP_LOGI(TAG, "Identified MID flag");

        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);

        if (!result) {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
        }
    }

    if (msg.flags & PAYLOAD_FLAG_LAST) {
        ESP_LOGI(TAG, "Identified LAST flag");

        result = append_payload_to_rx_buffer(msg.payload, msg.payload_len);
        if (result) {
            result = process_rx_buffer();
            if (!result) {
                ESP_LOGE(TAG, "Error when processing rx buffer. Responding with ERR.");
                build_send_single_msg_packet(PAYLOAD_FLAG_ERR, msg.remaining_packets, 0, NULL);
            }
        } else {
            ESP_LOGE(TAG, "Error when appending to rx buffer. Aborting.");
            build_send_single_msg_packet(PAYLOAD_FLAG_ABORT, msg.remaining_packets, 0, NULL);
        }
    }

    if (result)
        rx_last_packet_timestamp_us = esp_timer_get_time();
    
    return;
}

static void process_tx_response(const usb_packet_msg_t msg)
{
    // Check unexpected response
    if (!tx_awaiting_response) {
        ESP_LOGE(TAG, "Received unexpected TX response. Ignoring.");
        return;
    }

    if (msg.flags & PAYLOAD_FLAG_ACK) {
        
    }

    if (msg.flags & PAYLOAD_FLAG_NAK) {
        
    }

//

    if (msg.flags & PAYLOAD_FLAG_OK) {
        
    }

    if (msg.flags & PAYLOAD_FLAG_ERR) {
        
    }

//

    if (msg.flags & PAYLOAD_FLAG_ABORT) {
        
    }
}

// ============ RX Stuff ============

// Erase rx buffer in order to receive new data
static void erase_rx_buffer()
{
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_buf_len = 0;
    rx_last_packet_timestamp_us = 0;
    ESP_LOGI(TAG, "RX buffer erased");
}

// Will store data in rx buffer appending it next to data already stored
static bool append_payload_to_rx_buffer(const uint8_t *data, uint8_t data_len)
{
    if (rx_buf_len + data_len > MAX_RX_BUF_SIZE) {
        ESP_LOGE(TAG, "RX BUF Error: trying to append packets bigger than available space (trying: %lu, max av: %lu)", data_len + rx_buf_len, MAX_RX_BUF_SIZE);
        return false;
    }

    memcpy(rx_buf + rx_buf_len, data, data_len);
    rx_buf_len+= data_len;

    ESP_LOGI(TAG, "Appended data to RX buf. Data length: %d. Buffer length: %d sizeof(rx_buf): %ull", data_len, rx_buf_len, sizeof(rx_buf));

    return true;
}

// Execute payload inside rx_buffer. Will erase rx_buffer afterwards, regardless of the result.
static bool process_rx_buffer()
{
    ESP_LOGI(TAG, "Processing RX buffer. rx_buf_len: %u", rx_buf_len);
    print_bytes_as_chars(TAG, rx_buf, rx_buf_len);

    return true;
}

// ============ TX Stuff ============

// Erase tx buffer in order to receive new data
static void erase_tx_buffer()
{
    memset(tx_buf, 0, sizeof(tx_buf));
    tx_buf_len = 0;
    tx_buf_idx = 0;
    tx_buf_last_packet_sent_idx = 0;
    tx_last_packet_timestamp_us = 0;
    ESP_LOGI(TAG, "TX buffer erased");
}

// Will store data in rx buffer appending it next to data already stored
static bool append_payload_to_tx_buffer(const uint8_t *data, uint8_t data_len)
{
    if (tx_buf_len + data_len > MAX_TX_BUF_SIZE) {
        ESP_LOGE(TAG, "TX BUF Error: trying to append packets bigger than available space (trying: %lu, max av: %lu)", data_len + tx_buf_len, MAX_TX_BUF_SIZE);
        return false;
    }

    memcpy(tx_buf + tx_buf_len, data, data_len);
    tx_buf_len+= data_len;

    ESP_LOGI(TAG, "Appended data to TX buf. Data length: %d. Buffer length: %d sizeof(tx_buf): %ull", data_len, tx_buf_len, sizeof(tx_buf));

    return true;
}

static bool build_send_single_msg_packet(uint8_t flag, uint16_t rem, uint8_t payload_len, uint8_t *payload)
{
    uint8_t packet[COMM_REPORT_SIZE] = {0};
    packet[0] = flag;
    packet[1] = rem & 0xFF;
    packet[2] = (rem >> 8) & 0xFF;
    packet[3] = payload_len;
    memcpy(packet + 4, payload, payload_len);

    return send_single_packet(packet, COMM_REPORT_SIZE);
}

#define PACKET_SEND_MAX_ATTEMPTS 3
#define PACKET_SEND_ATTEMPT_DELAY_MS 10

// will send a packet of length COMM_REPORT_SIZE where last byte will be CRC
static bool send_single_packet(uint8_t *packet, uint16_t packet_len)
{
    usb_crc_prepare_packet(packet);
    bool result = tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, packet_len);

    for (uint8_t i = 1; !result && 1 < PACKET_SEND_MAX_ATTEMPTS; i++) {
        ESP_LOGE(TAG, "Failed report send attempt...");
        vTaskDelay(pdMS_TO_TICKS(PACKET_SEND_ATTEMPT_DELAY_MS));
        result = tud_hid_n_report(ITF_NUM_HID_COMM, REPORT_ID_COMM, packet, packet_len);
    }

    if (result) ESP_LOGI(TAG, "Report sent successfully.");
    else        ESP_LOGE(TAG, "Unable to send usb report.");

    return result;
}

// Will extract the next payload available and increment buffer's idx.
// If payload extraction is valid, msg.flags will be 0xFF. Otherwise, 0x00
static usb_packet_msg_t tx_buf_extract_next_msg()
{
    usb_packet_msg_t msg = {0}; // msg.flags == 0x00 -> error
    if (!tx_buf_len) {
        ESP_LOGE(TAG, "Couldn't extract next msg from tx_buf. len == 0");
        return msg;
    }

    if (tx_buf_idx >= tx_buf_len) {
        ESP_LOGE(TAG, "Couldn't extract next msg from tx_buf. idx >= len");
        return msg;
    }

    // extraction should succeed

    uint16_t stripped_payload_len = tx_buf_len - tx_buf_idx;
    stripped_payload_len = stripped_payload_len > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : stripped_payload_len;

    // increment idx
    tx_buf_last_packet_sent_idx = tx_buf_idx;
    tx_buf_idx+= stripped_payload_len;

    // calculate bytes left to calculate amount of remaining packets
    uint16_t bytes_left = tx_buf_len - stripped_payload_len;
    
    // set all values
    msg.flags = 0xFF; // msg.flags == 0xFF -> success
    msg.remaining_packets = (bytes_left + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
    memcpy(msg.payload, tx_buf + tx_buf_idx, stripped_payload_len);
    msg.payload_len = stripped_payload_len;

    // append crc data
    usb_crc_prepare_packet((uint8_t*) &msg);

    // buffer emptying should be managed in tx_processing_queue

     return msg;
}

// Send full payload via tx
static bool send_payload(const uint8_t *payload, uint16_t payload_len)
{
    // Check for buffer content
    if (tx_buf_len) {
        ESP_LOGE(TAG, "Can't send payload: dirty buffer");
        return false;
    }

    // Check if any response is being awaited
    if (tx_awaiting_response) {
        ESP_LOGE(TAG, "Can't send payload: awaiting for response");
        return false;
    }

    // fill buffer
    memcpy(tx_buf, payload, payload_len);
    tx_buf_len = payload_len;

    // Extract first message
    usb_packet_msg_t first_msg = tx_buf_extract_next_msg();

    if (first_msg.flags != 0xFF) {
        ESP_LOGE(TAG, "TX Buffer first message extraction failed. Aborting.");
        erase_tx_buffer();
        return false;
    }

    // Send first payload message. The others (if any) should be sent over
    if (!send_single_packet(&first_msg, COMM_REPORT_SIZE)) {
        ESP_LOGE(TAG, "TX Process queue full, dropping packet.");
        return false;
    }

    return true;
}

// ============ init ============

static void processing_task(void *pvParameters) {
    usb_packet_msg_t msg;
    while (1) {
        // check for incoming packets
        if (xQueueReceive(rx_processing_queue, &msg, portMAX_DELAY) == pdTRUE) {
            process_incoming_packet(msg);
        }

        if (xQueueReceive(tx_processing_queue, &msg, portMAX_DELAY) == pdTRUE) {
            process_outgoing_packet(msg);
        }

        // check for packet timeout (rx_buf)
        uint64_t rx_timestamp_elapsed = esp_timer_get_time() - rx_last_packet_timestamp_us;
        if (rx_last_packet_timestamp_us && rx_timestamp_elapsed > RX_TIMEOUT_MS * 1000) {
            erase_rx_buffer();
        }

        // check for packet timeout (rx_buf)
        uint64_t tx_timestamp_elapsed = esp_timer_get_time() - tx_last_packet_timestamp_us;
        if (tx_last_packet_timestamp_us && tx_timestamp_elapsed > TX_TIMEOUT_MS * 1000) {
            erase_tx_buffer();
        }
    }
}

void usb_callbacks_init(void) {
    rx_processing_queue = xQueueCreate(PROCESS_QUEUE_LENGTH, sizeof(usb_packet_msg_t));
    if (rx_processing_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create process queue");
        return;
    }

    // 32KB stack size
    xTaskCreate(processing_task, "usb_processing_task", 8192, NULL, 5, NULL);
}