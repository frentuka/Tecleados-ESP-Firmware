#include "split_transport.h"
#include "split_protocol.h"
#include "split_crypto.h"
#include "split_session.h"

#include <string.h>


#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#define TAG "SPLIT_TR"

/* =========================================================================
 * Internal State
 * ========================================================================= */

#define MAX_PROTOCOLS 4

typedef struct {
    uint8_t                   proto_id;
    split_transport_recv_cb_t cb;
} proto_handler_t;

static proto_handler_t         s_handlers[MAX_PROTOCOLS];
static uint8_t                 s_handler_count = 0;
static split_transport_send_cb_t s_send_cb     = NULL;
static bool                    s_initialized   = false;

// Session key for AES-128-CCM.  Zeroed = plaintext mode (during pairing).
static uint8_t                 s_session_key[SPLIT_CRYPTO_KEY_SIZE] = {0};
static bool                    s_session_key_set                    = false;

// Handshake key (usually paired key) used for recovery/negotiation
static uint8_t                 s_handshake_key[SPLIT_CRYPTO_KEY_SIZE] = {0};
static bool                    s_handshake_key_set                   = false;

static TickType_t              s_last_tx_time                       = 0;



/* =========================================================================
 * ESP-NOW Callbacks (called from WiFi task context)
 * ========================================================================= */

static void on_espnow_recv(const esp_now_recv_info_t *info,
                           const uint8_t *data, int len)
{
    if (!data || len < (int)SPLIT_FRAME_OVERHEAD || (size_t)len > SPLIT_ESP_NOW_MAX) {
        return;
    }

    // Work on a mutable copy: decryption operates in-place, and data is const.
    uint8_t frame_buf[SPLIT_ESP_NOW_MAX];
    memcpy(frame_buf, data, (size_t)len);

    split_frame_header_t header;
    size_t payload_len = 0;

    if (!split_protocol_parse_frame(frame_buf, (size_t)len, &header, NULL, &payload_len)) {
        ESP_LOGD(TAG, "dropped invalid frame from " MACSTR, MAC2STR(info->src_addr));
        return;
    }

    uint8_t       *payload = frame_buf + SPLIT_FRAME_HEADER_SIZE;
    const uint8_t *mic     = frame_buf + SPLIT_FRAME_HEADER_SIZE + payload_len;

    // Reconstruct 48-bit sequence from header (little-endian)
    uint64_t full_seq = 0;
    for (int i = 0; i < 6; i++) {
        full_seq |= ((uint64_t)header.seq[i] << (i * 8));
    }

    // DISCOVERY and PAIRING messages are always plaintext (unencrypted) as they
    // are used to bootstrap the session. We skip decryption for these types.
    bool type_is_plaintext = (header.type == SPLIT_MSG_DISCOVERY || 
                              header.type == SPLIT_MSG_PAIR_REQUEST || 
                              header.type == SPLIT_MSG_PAIR_RESPONSE);

    if (!type_is_plaintext) {
        esp_err_t crypt_ret = ESP_FAIL;

        // 1. Try primary session key if active (TSK)
        if (s_session_key_set) {
            crypt_ret = split_crypto_decrypt(s_session_key, full_seq,
                                             frame_buf, SPLIT_FRAME_HEADER_SIZE,
                                             payload, payload_len, mic);
        }

        // 2. Try handshake key if primary failed (or wasn't set) and message type allows it
        if (crypt_ret != ESP_OK && s_handshake_key_set) {
            bool type_allows_handshake = (header.type == SPLIT_MSG_ROLE_NEGOTIATE || 
                                          header.type == SPLIT_MSG_DISCOVERY);
            
            if (type_allows_handshake) {
                crypt_ret = split_crypto_decrypt(s_handshake_key, full_seq,
                                                 frame_buf, SPLIT_FRAME_HEADER_SIZE,
                                                 payload, payload_len, mic);
            }
        }

        if (crypt_ret != ESP_OK) {
            // Only increment failure counts if we're not in the initial grace period.
            if (!split_session_is_grace_period()) {
                split_session_inc_auth_failure();
            }
            ESP_LOGD(TAG, "decrypt failed (type=%02X seq=%llu key_set=%d) from " MACSTR,
                     header.type, (unsigned long long)full_seq, s_session_key_set, MAC2STR(info->src_addr));
            return;
        }
    }



    // Dispatch to registered protocol handler.
    // MIC has already been verified above (or the frame is legitimately plaintext);
    // pass the in-frame bytes to satisfy the callback signature.
    for (int i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].proto_id == header.proto) {
            s_handlers[i].cb(info->src_addr, header.type, full_seq,
                             payload, payload_len, mic);
            return;
        }
    }

    ESP_LOGD(TAG, "no handler for proto 0x%02X", header.proto);
}

static void on_espnow_send(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    if (s_send_cb && tx_info) {
        s_send_cb(tx_info->des_addr, status);
    }
}

/* =========================================================================
 * WiFi Init (station mode, no connection — just for ESP-NOW)
 * ========================================================================= */

static esp_err_t wifi_init_for_espnow(void)
{
    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    // Event loop may already exist (event_bus_init creates it)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set channel (default 1). ESP-NOW needs WiFi started first.
    ret = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "set channel failed: %s", esp_err_to_name(ret));
    }

    // Disable modem power save. The default WIFI_PS_MIN_MODEM can add up to one
    // DTIM interval (~100 ms) of receive latency, which is unacceptable for a
    // keyboard. WIFI_PS_NONE keeps the radio fully awake at the cost of ~20 mA
    // extra draw — acceptable since the keyboard is always in active use.
    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_ps(NONE) failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "WiFi STA initialized for ESP-NOW (modem power save disabled)");
    return ESP_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

esp_err_t split_transport_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = wifi_init_for_espnow();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_recv_cb(on_espnow_recv);
    if (ret != ESP_OK) goto fail;

    ret = esp_now_register_send_cb(on_espnow_send);
    if (ret != ESP_OK) goto fail;

    // Add broadcast peer so we can send discovery beacons
    const uint8_t broadcast[] = SPLIT_BROADCAST_MAC;
    ret = split_transport_add_peer(broadcast, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "broadcast peer add failed (may already exist)");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW transport initialized");
    return ESP_OK;

fail:
    esp_now_deinit();
    return ret;
}

esp_err_t split_transport_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();

    s_handler_count = 0;
    s_send_cb = NULL;
    s_initialized = false;
    memset(s_session_key, 0, SPLIT_CRYPTO_KEY_SIZE);
    s_session_key_set = false;

    ESP_LOGI(TAG, "ESP-NOW transport deinitialized");
    return ESP_OK;
}

esp_err_t split_transport_register_protocol(uint8_t proto_id,
                                            split_transport_recv_cb_t cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    if (s_handler_count >= MAX_PROTOCOLS) return ESP_ERR_NO_MEM;

    // Check for duplicate
    for (int i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].proto_id == proto_id) {
            s_handlers[i].cb = cb;
            return ESP_OK;
        }
    }

    s_handlers[s_handler_count].proto_id = proto_id;
    s_handlers[s_handler_count].cb = cb;
    s_handler_count++;

    ESP_LOGD(TAG, "registered handler for proto 0x%02X", proto_id);
    return ESP_OK;
}

void split_transport_set_send_cb(split_transport_send_cb_t cb)
{
    s_send_cb = cb;
}

void split_transport_set_session_key(const uint8_t *key)
{
    if (key) {
        memcpy(s_session_key, key, SPLIT_CRYPTO_KEY_SIZE);
        s_session_key_set = true;
        ESP_LOGD(TAG, "session key set — encryption enabled");
    } else {
        memset(s_session_key, 0, SPLIT_CRYPTO_KEY_SIZE);
        s_session_key_set = false;
        ESP_LOGD(TAG, "session key cleared — plaintext mode");
    }
}

void split_transport_set_handshake_key(const uint8_t *key)
{
    if (key) {
        memcpy(s_handshake_key, key, SPLIT_CRYPTO_KEY_SIZE);
        s_handshake_key_set = true;
        ESP_LOGD(TAG, "handshake key registered");
    } else {
        memset(s_handshake_key, 0, SPLIT_CRYPTO_KEY_SIZE);
        s_handshake_key_set = false;
    }
}


esp_err_t split_transport_send(const uint8_t *dst_mac,
                               uint8_t proto_id, uint8_t type, uint64_t seq,
                               const uint8_t *payload, size_t payload_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (payload_len > SPLIT_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;

    uint8_t frame_buf[SPLIT_ESP_NOW_MAX];
    size_t frame_len = split_protocol_build_frame(frame_buf, sizeof(frame_buf),
                                                   proto_id, type, seq,
                                                   payload, payload_len);
    if (frame_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_session_key_set || s_handshake_key_set) {
        // Negotiation and discovery always use the handshake key (paired key)
        // to ensure we can always talk to a peer regardless of transient state.
        bool use_handshake = (proto_id == SPLIT_PROTO_SPLIT && 
                             (type == SPLIT_MSG_ROLE_NEGOTIATE || type == SPLIT_MSG_DISCOVERY));
        
        // DISCOVERY and PAIRING messages are sent as plaintext (unencrypted) during initial handshake.
        bool type_is_plaintext = (type == SPLIT_MSG_DISCOVERY || 
                                  type == SPLIT_MSG_PAIR_REQUEST || 
                                  type == SPLIT_MSG_PAIR_RESPONSE);

        if (type_is_plaintext) {
            // Send as plaintext (unencrypted)
        } else {
            const uint8_t *key = (use_handshake && s_handshake_key_set) ? s_handshake_key : s_session_key;

            // Encrypt payload in-place; fill the MIC trailer that build_frame zeroed.
            uint8_t *enc_payload = frame_buf + SPLIT_FRAME_HEADER_SIZE;
            uint8_t *mic_out     = frame_buf + SPLIT_FRAME_HEADER_SIZE + payload_len;
            esp_err_t crypt_ret  = split_crypto_encrypt(key, seq,
                                                        frame_buf, SPLIT_FRAME_HEADER_SIZE,
                                                        enc_payload, payload_len,
                                                        mic_out);
            if (crypt_ret != ESP_OK) {
                ESP_LOGE(TAG, "encrypt failed: %s (type=%02X)", esp_err_to_name(crypt_ret), type);
                return crypt_ret;
            }
        }
    }



    esp_err_t ret = esp_now_send(dst_mac, frame_buf, frame_len);
    if (ret == ESP_OK) {
        // Broadcasts (discovery) do not count as active peer interaction
        static const uint8_t bcast[] = SPLIT_BROADCAST_MAC;
        if (memcmp(dst_mac, bcast, 6) != 0) {
            s_last_tx_time = xTaskGetTickCount();
        }
    }
    return ret;
}

esp_err_t split_transport_add_peer(const uint8_t *mac, uint8_t channel)
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }

    esp_now_peer_info_t peer = {
        .channel = channel,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,  // We handle encryption at the protocol layer
    };
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);

    return esp_now_add_peer(&peer);
}

esp_err_t split_transport_remove_peer(const uint8_t *mac)
{
    if (!esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    return esp_now_del_peer(mac);
}

bool split_transport_peer_exists(const uint8_t *mac)
{
    return esp_now_is_peer_exist(mac);
}

esp_err_t split_transport_set_channel(uint8_t channel)
{
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

uint8_t split_transport_get_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t secondary;
    esp_wifi_get_channel(&primary, &secondary);
    return primary;
}

TickType_t split_transport_get_last_tx_time(void)
{
    return s_last_tx_time;
}
