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
static TickType_t s_grace_until    = 0;
static uint32_t   s_last_peer_salt = 0;

static uint32_t   s_auth_failures  = 0;
static uint32_t   s_local_salt     = 0;
static TickType_t s_last_salt_gen  = 0;


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
/* NOTE (F7 \u2014 sequential-write race): Xtensa 32-bit aligned byte writes are
 * individually atomic, but callers that write state and role back-to-back
 * (e.g. set_state(CONNECTED) then set_role(MASTER)) expose a brief window
 * where another core can observe CONNECTED + ROLE_NONE. All readers that
 * act on both values together (gate_incoming_frame, heartbeat handler) are
 * in the WiFi task, which runs concurrently with split_task on the second
 * core. The window is a single instruction and only leads to a no-op extra
 * heartbeat echo at worst; full mutex protection is not warranted for this
 * use case. If the pattern expands into more complex state logic, revisit. */

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

void split_session_reset_tx_seq(void)
{
    portENTER_CRITICAL(&s_seq_mux);
    s_tx_seq = 0;
    portEXIT_CRITICAL(&s_seq_mux);
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

void split_session_set_grace(uint32_t ms)  { s_grace_until = xTaskGetTickCount() + pdMS_TO_TICKS(ms); }
bool split_session_is_grace_period(void)   { return (xTaskGetTickCount() < s_grace_until); }

void     split_session_set_last_peer_salt(uint32_t salt) { s_last_peer_salt = salt; }
uint32_t split_session_get_last_peer_salt(void)          { return s_last_peer_salt; }

void     split_session_inc_auth_failure(void)   { s_auth_failures++; }

uint32_t split_session_get_auth_failures(void)  { return s_auth_failures; }
void     split_session_reset_auth_failures(void) { s_auth_failures = 0; }

/* =========================================================================
 * Transient session salts
 * ========================================================================= */
void split_session_set_local_salt(uint32_t salt)
{
    TickType_t now = xTaskGetTickCount();
    // Salt Stickiness: Don't rotate salt more than once every 10 seconds to
    // prevent handshake race conditions where one side switches while packets
    // with the old salt are still in flight.
    if (s_local_salt != 0 && (now - s_last_salt_gen) < pdMS_TO_TICKS(10000)) {
        return;
    }
    s_local_salt    = salt;
    s_last_salt_gen = now;
}

void split_session_force_local_salt(uint32_t salt)
{
    // Bypass the stickiness guard — used only at pairing completion where a
    // fresh, authoritative salt is always required regardless of elapsed time.
    s_local_salt    = salt;
    s_last_salt_gen = xTaskGetTickCount();
}

uint32_t split_session_get_local_salt(void) { return s_local_salt; }

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
