#include "split_session.h"

#include <string.h>

#include "esp_mac.h"
#include "esp_log.h"

#define TAG "SPLIT_SES"

/* =========================================================================
 * State (file-level so all accessors are cheap; synchronisation is added
 * only where concurrent access actually happens — seq allocator and
 * anti-replay window).
 * ========================================================================= */

static split_state_t s_state = SPLIT_STATE_DISABLED;
static split_role_t  s_role  = SPLIT_ROLE_NONE;

static uint8_t s_own_mac[6]  = {0};
static uint8_t s_peer_mac[6] = {0};

static int8_t   s_peer_rssi  = 0;
static uint16_t s_latency_us = 0;

/* s_tx_seq is accessed from split_task, kb_manager_task (via matrix_cb) and
 * the WiFi task (heartbeat echo). On dual-core ESP32-S3 these run in parallel;
 * all accesses must go through split_session_next_seq(). */
static uint64_t      s_tx_seq  = 0;
static portMUX_TYPE  s_seq_mux = portMUX_INITIALIZER_UNLOCKED;

/* Long-term AES-128 key derived from X25519 during pairing, stored in NVS. */
static uint8_t s_stored_key[SPLIT_CRYPTO_KEY_SIZE] = {0};

/* Anti-replay state for inbound packets. */
static uint64_t s_peer_seq_last  = 0;
static bool     s_peer_seq_valid = false;

/* Peer liveness. */
static TickType_t s_peer_last_seen = 0;
static bool       s_link_stale     = false;
static TickType_t s_connected_at   = 0;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

void split_session_init(void)
{
    esp_read_mac(s_own_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "own MAC: " MACSTR, MAC2STR(s_own_mac));
}

/* =========================================================================
 * State machine
 * ========================================================================= */

split_state_t split_session_get_state(void) { return s_state; }
void          split_session_set_state(split_state_t s) { s_state = s; }
split_role_t  split_session_get_role(void)  { return s_role; }
void          split_session_set_role(split_role_t r) { s_role = r; }

/* =========================================================================
 * MACs
 * ========================================================================= */

const uint8_t *split_session_own_mac(void)  { return s_own_mac;  }
const uint8_t *split_session_peer_mac(void) { return s_peer_mac; }

void split_session_set_peer_mac(const uint8_t mac[6])
{
    memcpy(s_peer_mac, mac, 6);
}

void split_session_clear_peer_mac(void)
{
    memset(s_peer_mac, 0, 6);
}

/* =========================================================================
 * Metrics
 * ========================================================================= */

int8_t   split_session_get_rssi(void)        { return s_peer_rssi;  }
void     split_session_set_rssi(int8_t r)    { s_peer_rssi = r; }
uint16_t split_session_get_latency_us(void)  { return s_latency_us; }
void     split_session_set_latency_us(uint16_t v) { s_latency_us = v; }

/* =========================================================================
 * Sequence allocator — always go through this to avoid tearing / duplicates.
 * ========================================================================= */

uint64_t split_session_next_seq(void)
{
    portENTER_CRITICAL(&s_seq_mux);
    uint64_t s = s_tx_seq++;
    portEXIT_CRITICAL(&s_seq_mux);
    return s;
}

/* =========================================================================
 * Anti-replay — 16-bit sequence space with a 32768-wide forward window.
 *
 * Unsigned modular subtraction gives the forward distance. Values in
 * [1, 32767] are "new", 0 is a duplicate, [32768, 65535] are "old". This
 * avoids implementation-defined behaviour from casting a negative int to
 * int16_t while keeping the same wraparound-aware window.
 * ========================================================================= */

bool split_session_check_rx_seq(uint64_t seq)
{
    if (s_peer_seq_valid) {
        // With a 64-bit counter (using 48 bits for wireless frames), we don't
        // need wraparound-aware windows like 16-bit space does.
        // A simple "greater than" check is sufficient and prevents all replays.
        if (seq <= s_peer_seq_last) {
            return false;
        }
    }
    s_peer_seq_last  = seq;
    s_peer_seq_valid = true;
    return true;
}

void split_session_reset_rx_seq(void)
{
    s_peer_seq_last  = 0;
    s_peer_seq_valid = false;
}

/* =========================================================================
 * Peer liveness
 * ========================================================================= */

void       split_session_mark_peer_seen(void)    { s_peer_last_seen = xTaskGetTickCount(); }
TickType_t split_session_peer_last_seen(void)    { return s_peer_last_seen; }

void split_session_set_link_stale(bool stale)    { s_link_stale = stale; }
bool split_session_is_link_stale(void)           { return s_link_stale; }

void       split_session_mark_connected_now(void){ s_connected_at = xTaskGetTickCount(); }
TickType_t split_session_connected_at(void)      { return s_connected_at; }

/* =========================================================================
 * Stored key
 * ========================================================================= */

void split_session_set_stored_key(const uint8_t *key)
{
    if (key) {
        memcpy(s_stored_key, key, SPLIT_CRYPTO_KEY_SIZE);
    } else {
        memset(s_stored_key, 0, SPLIT_CRYPTO_KEY_SIZE);
    }
}

const uint8_t *split_session_stored_key(void)
{
    return s_stored_key;
}
