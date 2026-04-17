#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "cfgmod.h"

/**
 * @brief Sequence-number allocator callback.
 *
 * Callers must provide a thread-safe function that returns the next outgoing
 * sequence number (typically next_seq() from splitmod.c, protected by a
 * critical section).  Passing raw &s_tx_seq and doing (*tx_seq)++ inside this
 * module would bypass that critical section on a dual-core ESP32-S3.
 */
typedef uint64_t (*split_seq_alloc_fn_t)(void);

/* =========================================================================
 * Config kinds that are synced over split link
 *
 * Each entry is a (kind, key) pair that corresponds directly to a
 * cfgmod_read_storage / cfgmod_write_storage call.
 * Macros and custom keys use their own indexed storage and are excluded.
 * ========================================================================= */

typedef struct {
    cfgmod_kind_t kind;
    const char   *key;
} split_sync_entry_t;

extern const split_sync_entry_t SPLIT_SYNC_ENTRIES[];
extern const size_t             SPLIT_SYNC_ENTRY_COUNT;

/* =========================================================================
 * MASTER side — transmit config to slave
 * ========================================================================= */

/**
 * @brief Push all syncable config entries to the slave (called on connect).
 *        Sends CONFIG_SYNC fragments for every entry in SPLIT_SYNC_ENTRIES.
 */
esp_err_t split_config_sync_push_all(const uint8_t *peer_mac, split_seq_alloc_fn_t get_seq);

/**
 * @brief Push a single (kind, key) entry to the slave.
 *        Called by the CONFIG_EVENT_KIND_UPDATED handler for incremental sync.
 */
esp_err_t split_config_sync_push(const uint8_t *peer_mac, split_seq_alloc_fn_t get_seq,
                                  cfgmod_kind_t kind, const char *key);

/**
 * @brief Handle an incoming CONFIG_SYNC_ACK from the slave.
 */
void split_config_sync_on_ack(const uint8_t *payload, size_t len);

/* =========================================================================
 * SLAVE side — receive config from master
 * ========================================================================= */

/**
 * @brief Handle an incoming CONFIG_SYNC fragment from the master.
 *        Reassembles fragments and applies the blob to local NVS on completion.
 *
 * @param out_reverse_ble_sync  Set to true when a stale ble_cfg write is rejected
 *                              and the caller should push its own (newer) ble_cfg +
 *                              bond data back to the sender.  May be NULL.
 */
esp_err_t split_config_sync_on_fragment(const uint8_t *src_mac,
                                         const uint8_t *payload, size_t len,
                                         const uint8_t *reply_mac,
                                         split_seq_alloc_fn_t get_seq,
                                         bool *out_reverse_ble_sync);

void split_config_sync_reset(void);

/**
 * @brief Process any deferred reassembly work (NVS writes or timeouts).
 *        Should be called periodically from split_task.
 */
void split_config_sync_process_deferred(void);
