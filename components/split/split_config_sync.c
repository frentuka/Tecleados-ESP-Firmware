#include "split_config_sync.h"
#include "split_protocol.h"
#include "split_transport.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cfg_storage_keys.h"
#include "cfg_layouts.h"
#include "cfg_ble.h"
#include "cfg_macros.h"
#include "cfg_custom_keys.h"
#include "cfg_combos.h"
#include "split_session.h"
#include "host/ble_store.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

// Mirror of the bond sync binary format defined in cfg_ble.c — used to
// count PEER_SEC records in an incoming blob WITHOUT applying it first.
struct bond_sync_header {
    uint8_t  version;
    uint8_t  num_sections;
    uint16_t sync_version;
} __attribute__((packed));

struct bond_sync_section {
    uint8_t  type;
    uint16_t record_count;
    uint16_t record_size;
} __attribute__((packed));

#define SPLIT_CFG_SEND_RETRIES   3    // Max attempts per fragment
#define SPLIT_CFG_RETRY_DELAY_MS 30   // Delay between retries (and after success, to pace the burst)

#define TAG "SPLIT_CFG"

/* Pending-push descriptor — tracks which (kind, key) the master is currently
 * waiting for an ACK on. The ACK handler validates this before giving the
 * semaphore so that stale or spurious ACKs from a previous session do not
 * unblock the wrong push (Finding #2). */
typedef struct {
    bool     active;
    uint8_t  kind;
    char     key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
} pending_push_t;

static SemaphoreHandle_t s_tx_ack_sem   = NULL;
static pending_push_t    s_pending_push = {0};

static void ensure_tx_sem_init(void)
{
    if (s_tx_ack_sem == NULL) {
        s_tx_ack_sem = xSemaphoreCreateBinary();
    }
}

/* =========================================================================
 * Sync table — (kind, NVS key) pairs pushed to slave on connect / config update
 * ========================================================================= */

const split_sync_entry_t SPLIT_SYNC_ENTRIES[] = {
    { CFGMOD_KIND_LAYOUT,   "lay_idx"       },
    { CFGMOD_KIND_SYSTEM,   "sys"           },
    { CFGMOD_KIND_PHYSICAL, CFG_ST_PHYSICAL_LAYOUT },
    { CFGMOD_KIND_MACRO,    "mac_idx"       },
    { CFGMOD_KIND_CKEY,     "ck_idx"        },
    { CFGMOD_KIND_COMBO,    "cmb_idx"       },
    { CFGMOD_KIND_CONNECTION, "ble_cfg"     },
    { CFGMOD_KIND_BLE_BOND, "all"           },
};


const size_t SPLIT_SYNC_ENTRY_COUNT =
    sizeof(SPLIT_SYNC_ENTRIES) / sizeof(SPLIT_SYNC_ENTRIES[0]);

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

// Send one fragment with retry. Paces the send with a short delay on success
// to avoid flooding the ESP-NOW TX queue when pushing many fragments.
static esp_err_t send_fragment_with_retry(const uint8_t *peer_mac, split_seq_alloc_fn_t get_seq,
                                           const uint8_t *frame, size_t frame_len,
                                           uint8_t idx, uint8_t total)
{
    for (int attempt = 0; attempt < SPLIT_CFG_SEND_RETRIES; attempt++) {
        esp_err_t ret = split_transport_send(peer_mac, SPLIT_PROTO_SPLIT,
                                             SPLIT_MSG_CONFIG_SYNC, get_seq(),
                                             frame, frame_len);
        if (ret == ESP_OK) {
            // Space out fragments to avoid filling the MAC TX queue and causing
            // peak current spikes when pushing 20+ fragments back-to-back.
            vTaskDelay(pdMS_TO_TICKS(SPLIT_CFG_RETRY_DELAY_MS));
            return ESP_OK;
        }
        ESP_LOGW(TAG, "fragment %u/%u attempt %d/%d: %s",
                 idx + 1, total, attempt + 1, SPLIT_CFG_SEND_RETRIES, esp_err_to_name(ret));
        if (attempt + 1 < SPLIT_CFG_SEND_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(SPLIT_CFG_RETRY_DELAY_MS));
        }
    }
    ESP_LOGW(TAG, "fragment %u/%u failed after %d attempts", idx + 1, total, SPLIT_CFG_SEND_RETRIES);
    return ESP_FAIL;
}

/* =========================================================================
 * MASTER — send fragments for one (kind, key) blob
 * ========================================================================= */

esp_err_t split_config_sync_push(const uint8_t *peer_mac, split_seq_alloc_fn_t get_seq,
                                  cfgmod_kind_t kind, const char *key)
{
    if (!peer_mac || !get_seq || !key) return ESP_ERR_INVALID_ARG;

    // ---- F8: Two-pass PSRAM allocation ----------------------------------------
    // Allocate exactly what we need rather than the theoretical 57 KB maximum.
    // cfgmod_read_storage rejects NULL buffers, so we use a 1-byte probe buffer:
    // NVS returns ESP_ERR_NVS_INVALID_LENGTH and writes the real size into blob_len.
    size_t blob_len = 1;
    uint8_t probe_byte;
    esp_err_t ret = cfgmod_read_storage(kind, key, &probe_byte, &blob_len);

    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "read_storage(%u, %s): not found — skipping", kind, key);
        return ESP_OK;
    }
    // When the provided buffer is too small, nvs_get_blob sets *inout_len to the
    // real blob size and returns a non-zero error (platform-specific "invalid length").
    // We treat any non-ESP_OK, non-ESP_ERR_NOT_FOUND result as "size updated in blob_len".
    // blob_len now holds the actual required size regardless of which error was returned.
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        // blob_len was updated by cfgmod_read_storage / nvs_get_blob to the true size.
        // Proceed with allocation.
        if (blob_len <= 1) {
            // Unexpected: NVS reported an error but didn't update blob_len.
            ESP_LOGW(TAG, "read_storage probe(%u, %s): unexpected error %s with blob_len=0", kind, key, esp_err_to_name(ret));
            return ESP_OK;
        }
    }

    if (blob_len == 0) return ESP_OK;

    uint8_t *blob = heap_caps_malloc(blob_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) return ESP_ERR_NO_MEM;

    ret = cfgmod_read_storage(kind, key, blob, &blob_len);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "read_storage(%u, %s): %s — skipping", kind, key, esp_err_to_name(ret));
        } else {
            ESP_LOGD(TAG, "read_storage(%u, %s): not found — skipping", kind, key);
        }
        free(blob);
        return ESP_OK;
    }
    if (blob_len == 0) { free(blob); return ESP_OK; }
    // ---------------------------------------------------------------------------


    uint8_t total_fragments = (uint8_t)((blob_len + SPLIT_CONFIG_SYNC_DATA_MAX - 1)
                                         / SPLIT_CONFIG_SYNC_DATA_MAX);

    ESP_LOGD(TAG, "pushing kind=%u key=%s blob_len=%u fragments=%u",
             kind, key, (unsigned)blob_len, total_fragments);

    // ---- F2: Register the pending push so on_ack can validate before giving sem
    ensure_tx_sem_init();
    xSemaphoreTake(s_tx_ack_sem, 0); // Clear any stale ACK BEFORE sending fragments

    s_pending_push.active = true;
    s_pending_push.kind   = (uint8_t)kind;
    memset(s_pending_push.key, 0, sizeof(s_pending_push.key));
    strncpy(s_pending_push.key, key, SPLIT_CONFIG_SYNC_KEY_LEN);

    uint8_t frame[SPLIT_CONFIG_SYNC_HDR_SIZE + SPLIT_CONFIG_SYNC_DATA_MAX];

    for (uint8_t idx = 0; idx < total_fragments; idx++) {
        size_t offset    = (size_t)idx * SPLIT_CONFIG_SYNC_DATA_MAX;
        size_t chunk_len = blob_len - offset;
        if (chunk_len > SPLIT_CONFIG_SYNC_DATA_MAX) chunk_len = SPLIT_CONFIG_SYNC_DATA_MAX;

        split_config_sync_payload_t *hdr = (split_config_sync_payload_t *)frame;
        hdr->kind           = (uint8_t)kind;
        hdr->fragment_idx   = idx;
        hdr->fragment_total = total_fragments;
        memset(hdr->key, 0, SPLIT_CONFIG_SYNC_KEY_LEN);
        strncpy((char *)hdr->key, key, SPLIT_CONFIG_SYNC_KEY_LEN - 1);
        memcpy(frame + SPLIT_CONFIG_SYNC_HDR_SIZE, blob + offset, chunk_len);

        ret = send_fragment_with_retry(peer_mac, get_seq,
                                       frame, SPLIT_CONFIG_SYNC_HDR_SIZE + chunk_len,
                                       idx, total_fragments);
        if (ret != ESP_OK) {
            s_pending_push.active = false;
            free(blob);
            return ret;
        }
    }

    free(blob);

    if (xSemaphoreTake(s_tx_ack_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "timeout waiting for ACK: kind=%u key=%s", kind, key);
        s_pending_push.active = false;
        return ESP_ERR_TIMEOUT;
    }

    s_pending_push.active = false;
    return ESP_OK;
}


esp_err_t split_config_sync_push_kind(const uint8_t *peer_mac, split_seq_alloc_fn_t get_seq,
                                       cfgmod_kind_t kind)
{
    esp_err_t last_ret = ESP_OK;

    for (size_t i = 0; i < SPLIT_SYNC_ENTRY_COUNT; i++) {
        if (SPLIT_SYNC_ENTRIES[i].kind != kind) continue;

        const char *key = SPLIT_SYNC_ENTRIES[i].key;
        esp_err_t ret = split_config_sync_push(peer_mac, get_seq, kind, key);
        if (ret != ESP_OK) last_ret = ret;

        // Special handling for dynamic sub-keys (Layouts, Macros, Custom Keys)
        if (kind == CFGMOD_KIND_LAYOUT && strcmp(key, "lay_idx") == 0) {
            cfg_layout_index_t lidx = {0};
            size_t lidx_len = sizeof(lidx);
            if (cfgmod_read_storage(kind, key, &lidx, &lidx_len) == ESP_OK) {
                for (uint16_t j = 0; j < CFG_LAYOUT_MAX_COUNT; j++) {
                    if (lidx.active_mask & (1u << j)) {
                        char sub_key[16];
                        snprintf(sub_key, sizeof(sub_key), "ly_%u", j);
                        split_config_sync_push(peer_mac, get_seq, kind, sub_key);
                    }
                }
            }
        } else if (kind == CFGMOD_KIND_MACRO && strcmp(key, "mac_idx") == 0) {
            cfg_macro_index_t midx = {0};
            size_t midx_len = sizeof(midx);
            if (cfgmod_read_storage(kind, key, &midx, &midx_len) == ESP_OK) {
                for (uint16_t j = 0; j < 64; j++) {
                    if (midx.active_mask & (1ULL << j)) {
                        char sub_key[16];
                        snprintf(sub_key, sizeof(sub_key), "mac_%u", j);
                        split_config_sync_push(peer_mac, get_seq, kind, sub_key);
                    }
                }
            }
        } else if (kind == CFGMOD_KIND_CKEY && strcmp(key, "ck_idx") == 0) {
            cfg_ckey_index_t cidx = {0};
            size_t cidx_len = sizeof(cidx);
            if (cfgmod_read_storage(kind, key, &cidx, &cidx_len) == ESP_OK) {
                for (uint16_t j = 0; j < 64; j++) {
                    if (cidx.mask[j / 8] & (1u << (j % 8))) {
                        char sub_key[12];
                        snprintf(sub_key, sizeof(sub_key), "ck_%u", j);
                        split_config_sync_push(peer_mac, get_seq, kind, sub_key);
                    }
                }
            }
        } else if (kind == CFGMOD_KIND_COMBO && strcmp(key, "cmb_idx") == 0) {
            cfg_combo_index_t cmbidx = {0};
            size_t cmbidx_len = sizeof(cmbidx);
            if (cfgmod_read_storage(kind, key, &cmbidx, &cmbidx_len) == ESP_OK) {
                for (uint16_t j = 0; j < 32; j++) {
                    if (cmbidx.active_mask & (1u << j)) {
                        char sub_key[16];
                        snprintf(sub_key, sizeof(sub_key), "cmb_%u", j);
                        split_config_sync_push(peer_mac, get_seq, kind, sub_key);
                    }
                }
            }
        }
    }
    return last_ret;
}


esp_err_t split_config_sync_push_all(const uint8_t *peer_mac, split_seq_alloc_fn_t get_seq)
{
    ESP_LOGI(TAG, "pushing main config kinds to slave");

    // Use a bool[256] instead of a uint32_t bitmask: shifting by kind >= 32 is
    // undefined behaviour in C, and cfgmod_kind_t is not bounded to < 32.
    bool pushed[256] = {false};
    for (size_t i = 0; i < SPLIT_SYNC_ENTRY_COUNT; i++) {
        cfgmod_kind_t kind = SPLIT_SYNC_ENTRIES[i].kind;
        if (pushed[(uint8_t)kind]) continue;

        split_config_sync_push_kind(peer_mac, get_seq, kind);
        pushed[(uint8_t)kind] = true;
    }

    return ESP_OK;
}


void split_config_sync_on_ack(const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_config_sync_ack_payload_t)) return;
    const split_config_sync_ack_payload_t *ack = (const split_config_sync_ack_payload_t *)payload;

    char key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    memcpy(key, ack->key, SPLIT_CONFIG_SYNC_KEY_LEN);
    key[SPLIT_CONFIG_SYNC_KEY_LEN] = '\0';

    if (ack->status == 0) {
        ESP_LOGD(TAG, "slave ACK'd kind=%u key=%s", ack->kind, key);
        if (ack->kind == (uint8_t)CFGMOD_KIND_CONNECTION && strncmp(key, "ble_cfg", 7) == 0) {
            cfg_ble_clear_unsynced();
        }
    } else {
        ESP_LOGW(TAG, "slave NAK'd kind=%u key=%s status=%u", ack->kind, key, ack->status);
    }

    // ---- F2: Only unblock the ACK semaphore if this ACK matches the pending push.
    // Stale or spurious ACKs from a previous session or a mismatched key must
    // NOT unblock a different in-progress push, as that would cause the master
    // to believe a config was delivered when it wasn't.
    if (s_tx_ack_sem && s_pending_push.active &&
        s_pending_push.kind == ack->kind &&
        strncmp(s_pending_push.key, key, SPLIT_CONFIG_SYNC_KEY_LEN) == 0) {
        xSemaphoreGive(s_tx_ack_sem);
    } else if (s_tx_ack_sem && !s_pending_push.active) {
        // No push in progress but semaphore exists — harmless, ignore.
        ESP_LOGD(TAG, "ACK received with no push in progress (kind=%u key=%s) — ignored",
                 ack->kind, key);
    } else {
        ESP_LOGW(TAG, "ACK kind/key mismatch: expected kind=%u key=%s, got kind=%u key=%s — ignored",
                 s_pending_push.kind, s_pending_push.key, ack->kind, key);
    }
}


/* =========================================================================
 * SLAVE — reassembly
 * ========================================================================= */

// Maximum fragments we can track in the bitmap. Support up to 255 to match
// the protocol's fragment_total field.
#define SPLIT_CFG_MAX_FRAGMENTS 255

typedef struct {
    bool      active;
    uint8_t   kind;
    char      key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    uint8_t   total;
    uint8_t   received_map[32]; // bitmask for 256 bits (up to 255 fragments)
    uint8_t  *buf;
    size_t    buf_len;       // allocated size
    size_t    data_len;      // total expected data length
    
    // Background deferral fields
    bool       write_pending;
    uint64_t   session_id;   // Monotonic session counter to prevent race in process_deferred
    bool       reverse_sync_pending;
    uint8_t    reply_mac[6];
    TickType_t last_updated_at;
} reassembly_t;

static reassembly_t s_rx = {0};
static portMUX_TYPE s_rx_mux = portMUX_INITIALIZER_UNLOCKED;

#define SPLIT_CFG_REASSEMBLY_TIMEOUT_TICKS  pdMS_TO_TICKS(2000)

void split_config_sync_reset(void)
{
    portENTER_CRITICAL(&s_rx_mux);
    if (s_rx.write_pending) {
        // Buffer is owned by the background process_deferred task.
        // DO NOT free it or zero out the state yet; process_deferred
        // will call reset() again when it's done.
        portEXIT_CRITICAL(&s_rx_mux);
        return;
    }
    if (s_rx.buf) {
        free(s_rx.buf);
        s_rx.buf = NULL;
    }
    memset(&s_rx, 0, sizeof(s_rx));
    portEXIT_CRITICAL(&s_rx_mux);

    // ---- F2: Destroy and re-create the ACK semaphore so stale signals from a
    // previous session cannot leak into the next one. 
    // Calling vSemaphoreDelete with a task potentially blocked on the semaphore 
    // would assert causing a crash. Thus we Give the semaphore so the waiter 
    // unblocks and naturally aborts when it sees s_pending_push is inactive.
    s_pending_push.active = false;
    if (s_tx_ack_sem) {
        xSemaphoreGive(s_tx_ack_sem);
    }
}


esp_err_t split_config_sync_on_fragment(const uint8_t *src_mac,
                                         const uint8_t *payload, size_t len,
                                         const uint8_t *reply_mac,
                                         split_seq_alloc_fn_t get_seq,
                                         bool *out_reverse_ble_sync)
{
    (void)src_mac;
    if (len < SPLIT_CONFIG_SYNC_HDR_SIZE) return ESP_ERR_INVALID_SIZE;

    const split_config_sync_payload_t *frag = (const split_config_sync_payload_t *)payload;
    size_t data_len = len - SPLIT_CONFIG_SYNC_HDR_SIZE;

    char key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    memcpy(key, frag->key, SPLIT_CONFIG_SYNC_KEY_LEN);
    key[SPLIT_CONFIG_SYNC_KEY_LEN] = '\0';

    if (frag->fragment_total == 0) {
        ESP_LOGW(TAG, "invalid fragment total 0");
        return ESP_ERR_INVALID_SIZE;
    }

    // New transfer or different (kind, key) → reset and start fresh.
    if (!s_rx.active || s_rx.kind != frag->kind ||
        strncmp(s_rx.key, key, SPLIT_CONFIG_SYNC_KEY_LEN) != 0 ||
        s_rx.total != frag->fragment_total) {
        
        // INTERLOCK: If a background write is still pending for the previous blob,
        // we cannot start a new reassembly because the s_rx state is occupied.
        if (s_rx.write_pending) {
            ESP_LOGW(TAG, "rx fragment kind=%u key=%s rejected: previous write still pending", frag->kind, key);
            return ESP_ERR_INVALID_STATE;
        }

        split_config_sync_reset();

        s_rx.active     = true;
        s_rx.kind       = frag->kind;
        s_rx.total      = frag->fragment_total;
        s_rx.session_id = esp_timer_get_time();
        memset(s_rx.received_map, 0, sizeof(s_rx.received_map));
        memcpy(s_rx.key, key, sizeof(s_rx.key));

        s_rx.buf_len = (size_t)frag->fragment_total * SPLIT_CONFIG_SYNC_DATA_MAX;
        s_rx.buf     = heap_caps_malloc(s_rx.buf_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_rx.buf) {
            split_config_sync_reset();
            return ESP_ERR_NO_MEM;
        }
        s_rx.data_len = 0;
    }

    s_rx.last_updated_at = xTaskGetTickCount();

    if (frag->fragment_idx >= frag->fragment_total) {
        ESP_LOGW(TAG, "bad fragment idx %u / total %u", frag->fragment_idx, frag->fragment_total);
        return ESP_ERR_INVALID_ARG;
    }

    size_t offset = (size_t)frag->fragment_idx * SPLIT_CONFIG_SYNC_DATA_MAX;
    if (offset + data_len > s_rx.buf_len) {
        ESP_LOGW(TAG, "fragment overflows reassembly buffer");
        split_config_sync_reset();
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t byte_idx = frag->fragment_idx / 8;
    uint8_t bit_idx  = frag->fragment_idx % 8;

    if (!(s_rx.received_map[byte_idx] & (1 << bit_idx))) {
        s_rx.received_map[byte_idx] |= (1 << bit_idx);
        memcpy(s_rx.buf + offset, payload + SPLIT_CONFIG_SYNC_HDR_SIZE, data_len);
    }

    // ---- F4: Accumulate data_len from every fragment so out-of-order delivery
    // is handled correctly. Using max() instead of only updating on the last
    // fragment-by-index means data_len is always correct regardless of arrival
    // order. (Previously only fragment_idx==total-1 updated data_len, which
    // left data_len==0 if that particular fragment was delayed.)
    size_t end = offset + data_len;
    if (end > s_rx.data_len) {
        s_rx.data_len = end;
    }

    ESP_LOGD(TAG, "rx fragment %u/%u kind=%u key=%s",
             frag->fragment_idx + 1, frag->fragment_total, frag->kind, key);

    // Check completion
    bool complete = true;
    for (uint16_t i = 0; i < s_rx.total; i++) {
        if (!(s_rx.received_map[i / 8] & (1 << (i % 8)))) {
            complete = false;
            break;
        }
    }

    if (!complete || s_rx.data_len == 0) {
        return ESP_OK; // Still waiting for more fragments.
    }

    // For ble_cfg: compare nonce sums to detect a stale push from the peer.
    // If our own nonce sum is greater, our data is newer — reject the overwrite
    // and signal the caller to push our ble_cfg + bond back to the sender.
    if ((cfgmod_kind_t)s_rx.kind == CFGMOD_KIND_CONNECTION &&
        strncmp(s_rx.key, "ble_cfg", SPLIT_CONFIG_SYNC_KEY_LEN) == 0 &&
        s_rx.data_len == sizeof(cfg_ble_state_t)) {

        const cfg_ble_state_t *recv_st = (const cfg_ble_state_t *)s_rx.buf;
        uint32_t recv_sum = recv_st->sync_version;
        uint32_t own_sum = cfg_ble_get_state()->sync_version;

        if (splitmod_get_role() == SPLIT_ROLE_SLAVE) {
            ESP_LOGI(TAG, "ble_cfg: SLAVE trust mode — accepting Master config (ver %u)", recv_sum);
        } else {
            // I am Master. I own the BLE config. I ignore any BLE config from a Slave.
            if (own_sum != recv_sum) {
                ESP_LOGW(TAG, "ble_cfg: Master-Slave sync mismatch (%u != %u). Triggering reverse corrective sync.", 
                         own_sum, recv_sum);
                if (out_reverse_ble_sync) *out_reverse_ble_sync = true;
            }
            
            split_config_sync_ack_payload_t ack = { .kind = s_rx.kind, .status = 1 };
            memcpy(ack.key, s_rx.key, SPLIT_CONFIG_SYNC_KEY_LEN);
            split_transport_send(src_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC_ACK,
                                 get_seq(), (const uint8_t *)&ack, sizeof(ack));
                                 
            split_config_sync_reset();
            return ESP_OK; // Consume but do not apply
        }
    }

    // For bond sync: protect against a newly-promoted master (that has no peer bonds
    // because it was previously a slave) wiping the other half's complete bond store.
    // Count PEER_SEC records in the incoming blob and compare to our local count.
    // If we have MORE peer bonds locally, request a reverse sync instead of accepting.
    if ((cfgmod_kind_t)s_rx.kind == CFGMOD_KIND_BLE_BOND &&
        strncmp(s_rx.key, "all", SPLIT_CONFIG_SYNC_KEY_LEN) == 0 &&
        s_rx.data_len >= sizeof(struct bond_sync_header)) {

        // Count peer records in incoming blob by scanning sections
        const struct bond_sync_header *bh = (const struct bond_sync_header *)s_rx.buf;
        int incoming_peer_count = 0;
        const uint8_t *bp = s_rx.buf + sizeof(*bh);
        size_t brem = s_rx.data_len - sizeof(*bh);
        for (int si = 0; si < bh->num_sections && brem >= sizeof(struct bond_sync_section); si++) {
            const struct bond_sync_section *sec = (const struct bond_sync_section *)bp;
            if (sec->type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
                incoming_peer_count = (int)sec->record_count;
            }
            size_t sec_sz = sizeof(*sec) + (size_t)sec->record_count * sec->record_size;
            if (brem < sec_sz) break;
            bp   += sec_sz;
            brem -= sec_sz;
        }

        // Count our local peer bonds based on persistent profile validity.
        // We do NOT use ble_store_util_count() here because it is unreliable
        // when the BLE radio is suspended or still syncing (which is exactly
        // when this sync happens).
        int local_peer_count = 0;
        // RULE: Master ignores Slave's bond data. Master is the source of truth.
        if (splitmod_get_role() == SPLIT_ROLE_MASTER) {
            ESP_LOGW(TAG, "Bond Sync Guard: Master ignores Slave's bond data. Requesting corrective reverse sync.");
            if (out_reverse_ble_sync) *out_reverse_ble_sync = true;
            
            split_config_sync_ack_payload_t ack = { .kind = s_rx.kind, .status = 1 };
            memcpy(ack.key, s_rx.key, SPLIT_CONFIG_SYNC_KEY_LEN);
            split_transport_send(src_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC_ACK,
                                 get_seq(), (const uint8_t *)&ack, sizeof(ack));
                                 
            split_config_sync_reset();
            return ESP_OK;
        }

        const cfg_ble_state_t *st = cfg_ble_get_state();
        for (int i = 0; i < CFG_BLE_MAX_PROFILES; i++) {
            if (st->profiles[i].is_valid) local_peer_count++;
        }

        ESP_LOGI(TAG, "Bond sync guard: incoming_peers=%d local_peers=%d",
                 incoming_peer_count, local_peer_count);

        if (local_peer_count > incoming_peer_count) {
            // We are richer in bonds — reject the incoming empty sync and
            // request a reverse sync so the master gets our complete bond set.
            ESP_LOGW(TAG, "Bond sync: local has more peers (%d > %d) — "
                     "rejecting and requesting reverse sync",
                     local_peer_count, incoming_peer_count);
            if (out_reverse_ble_sync) *out_reverse_ble_sync = true;
            
            split_config_sync_ack_payload_t ack = { .kind = s_rx.kind, .status = 1 };
            memcpy(ack.key, s_rx.key, SPLIT_CONFIG_SYNC_KEY_LEN);
            split_transport_send(src_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC_ACK,
                                 get_seq(), (const uint8_t *)&ack, sizeof(ack));
                                 
            split_config_sync_reset();
            return ESP_OK;
        }
    }

    // All fragments received — mark as pending for background write.
    // We defer the NVS write to the split_task to avoid blocking the WiFi task.
    portENTER_CRITICAL(&s_rx_mux);
    if (s_rx.active) {
        s_rx.write_pending = true;
        if (reply_mac) memcpy(s_rx.reply_mac, reply_mac, 6);
        if (out_reverse_ble_sync && *out_reverse_ble_sync) {
            s_rx.reverse_sync_pending = true;
        }
    }
    portEXIT_CRITICAL(&s_rx_mux);

    return ESP_OK;
}


void split_config_sync_process_deferred(void)
{
    // ---- F15: Note on blocking behaviour ----------------------------------------
    // This function is called from split_task's tick_connected() loop. When a
    // write is pending and a corrective reverse-sync is needed, it calls
    // split_config_sync_push() which can block for up to 5 s per key waiting
    // for the ACK semaphore. During that time the split_task tick stalls,
    // meaning no heartbeats are sent. This is by design (the task owns slow I/O)
    // and is acceptable because corrective reverse-syncs are rare events.
    // --------------------------------------------------------------------------
    portENTER_CRITICAL(&s_rx_mux);
    if (!s_rx.active) {
        portEXIT_CRITICAL(&s_rx_mux);
        return;
    }


    TickType_t now = xTaskGetTickCount();
    if (now - s_rx.last_updated_at > SPLIT_CFG_REASSEMBLY_TIMEOUT_TICKS && !s_rx.write_pending) {
        portEXIT_CRITICAL(&s_rx_mux);
        ESP_LOGW(TAG, "reassembly timeout for kind=%u key=%s — resetting", s_rx.kind, s_rx.key);
        split_config_sync_reset();
        return;
    }

    if (!s_rx.write_pending) {
        portEXIT_CRITICAL(&s_rx_mux);
        return;
    }

    // We have a write pending. Capture state and release mux to perform slow I/O.
    uint8_t   kind     = s_rx.kind;
    char      key[SPLIT_CONFIG_SYNC_KEY_LEN + 1];
    memcpy(key, s_rx.key, sizeof(key));
    uint8_t  *buf      = s_rx.buf;
    size_t    data_len = s_rx.data_len;
    uint8_t   dst_mac[6];
    memcpy(dst_mac, s_rx.reply_mac, 6);
    bool      rev_sync = s_rx.reverse_sync_pending;
    uint64_t  session_id = s_rx.session_id;
    
    // NULL out the buffer in state so reset doesn't double-free it while we work.
    s_rx.buf = NULL;
    s_rx.write_pending = false; 
    portEXIT_CRITICAL(&s_rx_mux);

    ESP_LOGD(TAG, "background: applying sync kind=%u key=%s (%u bytes)", kind, key, (unsigned)data_len);
    esp_err_t ret = cfgmod_write_storage((cfgmod_kind_t)kind, key, buf, data_len);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "config sync applied kind=%u key=%s", kind, key);
        if (rev_sync) {
            ESP_LOGI(TAG, "triggering corrective reverse sync for kind=%u key=%s", kind, key);
            split_config_sync_push(dst_mac, split_session_next_seq, (cfgmod_kind_t)kind, key);
            if (kind == CFGMOD_KIND_CONNECTION) {
                split_config_sync_push(dst_mac, split_session_next_seq, CFGMOD_KIND_BLE_BOND, "all");
            }
        }
    } else {
        ESP_LOGW(TAG, "background: NVS write failed for kind=%u key=%s: %s", kind, key, esp_err_to_name(ret));
    }

    // Send status ACK back to master
    split_config_sync_ack_payload_t ack = {
        .kind   = kind,
        .status = (ret == ESP_OK) ? 0 : 1,
    };
    memcpy(ack.key, key, SPLIT_CONFIG_SYNC_KEY_LEN);
    split_transport_send(dst_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_CONFIG_SYNC_ACK,
                         split_session_next_seq(), (const uint8_t *)&ack, sizeof(ack));

    // 2. Clear state ONLY IF the session ID hasn't changed.
    portENTER_CRITICAL(&s_rx_mux);
    if (s_rx.session_id == session_id) {
        split_config_sync_reset();
    }
    portEXIT_CRITICAL(&s_rx_mux);

    free(buf);
}
