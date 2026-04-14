#include "split_role.h"
#include "split_protocol.h"
#include "cfgmod.h"

#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"

#define TAG "SPLIT_ROLE"

#define NVS_LAST_ROLE_KEY "last_role"

/* =========================================================================
 * NVS persistence
 * ========================================================================= */

void split_role_save_last(split_role_t role)
{
    if (role != SPLIT_ROLE_MASTER && role != SPLIT_ROLE_SLAVE) return;
    uint8_t val = (uint8_t)role;
    esp_err_t ret = cfgmod_write_storage(CFGMOD_KIND_SPLIT, NVS_LAST_ROLE_KEY,
                                          &val, sizeof(val));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to save last_role: %s", esp_err_to_name(ret));
    }
}

split_role_t split_role_load_last(void)
{
    uint8_t val = 0;
    size_t len = sizeof(val);
    esp_err_t ret = cfgmod_read_storage(CFGMOD_KIND_SPLIT, NVS_LAST_ROLE_KEY,
                                         &val, &len);
    if (ret != ESP_OK || (val != SPLIT_ROLE_MASTER && val != SPLIT_ROLE_SLAVE)) {
        return SPLIT_ROLE_NONE;
    }
    return (split_role_t)val;
}

/* =========================================================================
 * Role decision
 * ========================================================================= */

static split_role_t flip_role(split_role_t current)
{
    return (current == SPLIT_ROLE_MASTER) ? SPLIT_ROLE_SLAVE : SPLIT_ROLE_MASTER;
}

split_role_t split_role_decide(const uint8_t own_mac[6],
                                const uint8_t peer_mac[6],
                                uint8_t own_usb_connected,
                                uint16_t own_ble_connected_bitmap,
                                uint8_t own_has_unsynced_ble,
                                split_role_t own_last_role,
                                uint8_t peer_usb_connected,
                                uint16_t peer_ble_connected_bitmap,
                                uint8_t peer_has_unsynced_ble,
                                split_role_t peer_last_role)
{
    // Every priority uses paired symmetric checks:
    //   if (own_X && !peer_X) → MASTER
    //   if (!own_X && peer_X) → SLAVE
    // This guarantees that when one side returns at a given priority, the other
    // side ALSO returns at that same priority with the complementary role.
    // A side that falls through to the next priority always has the same
    // input values as its peer at that priority (both equal), so they
    // both fall through together until one priority breaks the tie.

    // Priority 1: Unsynced BLE data.
    // A half with a fresh bond not yet shared with the peer MUST be master
    // so the sync happens before a role swap could orphan the credential.
    if ( own_has_unsynced_ble && !peer_has_unsynced_ble) return SPLIT_ROLE_MASTER;
    if (!own_has_unsynced_ble &&  peer_has_unsynced_ble) return SPLIT_ROLE_SLAVE;

    // Priority 2: USB host connection.
    // A half physically connected to a USB host must be master — it is already
    // producing HID reports via USB and the host expects them from that device.
    if ( own_usb_connected && !peer_usb_connected) return SPLIT_ROLE_MASTER;
    if (!own_usb_connected &&  peer_usb_connected) return SPLIT_ROLE_SLAVE;

    // Priority 3: BLE host connection.
    // The device with an active BLE connection owns the wireless output path.
    if ( own_ble_connected_bitmap && !peer_ble_connected_bitmap) return SPLIT_ROLE_MASTER;
    if (!own_ble_connected_bitmap &&  peer_ble_connected_bitmap) return SPLIT_ROLE_SLAVE;

    // Priority 4: Last persisted role.
    // Drives role continuity after a reconnect without requiring an explicit swap.
    //
    // The last_role enum has THREE values (MASTER, SLAVE, NONE). A single pair of
    // checks like "own==SLAVE && peer!=SLAVE → SLAVE" is NOT sufficient:
    // if A has own=SLAVE, B has own=NONE, A returns SLAVE at this check — but B
    // doesn't match "own==SLAVE" so B falls through to the MAC tiebreaker and
    // could ALSO return SLAVE. The explicit mirror checks below close that gap:
    // every case where one side returns here also triggers a return on the other.
    if (own_last_role  == SPLIT_ROLE_MASTER && peer_last_role != SPLIT_ROLE_MASTER) return SPLIT_ROLE_MASTER;
    if (peer_last_role == SPLIT_ROLE_MASTER && own_last_role  != SPLIT_ROLE_MASTER) return SPLIT_ROLE_SLAVE;
    if (own_last_role  == SPLIT_ROLE_SLAVE  && peer_last_role != SPLIT_ROLE_SLAVE)  return SPLIT_ROLE_SLAVE;
    if (peer_last_role == SPLIT_ROLE_SLAVE  && own_last_role  != SPLIT_ROLE_SLAVE)  return SPLIT_ROLE_MASTER;

    // Priority 5: Higher MAC address → MASTER (fully deterministic tiebreaker).
    // Reached only when both sides have identical state at every priority above
    // (both have the same last_role, same connectivity). MAC addresses are unique
    // so this never ties — one side always wins MASTER, the other SLAVE.
    int cmp = memcmp(own_mac, peer_mac, 6);
    split_role_t role = (cmp > 0) ? SPLIT_ROLE_MASTER : SPLIT_ROLE_SLAVE;

    ESP_LOGD(TAG, "role decided by MAC tiebreaker: %s "
             "(own_usb=%u peer_usb=%u "
             "own_ble_bm=0x%02X peer_ble_bm=0x%02X own_last=%u peer_last=%u)",
             role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE",
             own_usb_connected, peer_usb_connected,
             own_ble_connected_bitmap, peer_ble_connected_bitmap,
             (uint8_t)own_last_role, (uint8_t)peer_last_role);
    return role;
}

esp_err_t split_role_on_negotiate(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6],
                                   uint8_t own_usb_connected,
                                   uint16_t own_ble_connected_bitmap,
                                   uint8_t own_has_unsynced_ble,
                                   split_role_t own_last_role,
                                   split_role_t *out_role)
{
    if (len < sizeof(split_role_negotiate_payload_t)) return ESP_ERR_INVALID_SIZE;
    if (!out_role) return ESP_ERR_INVALID_ARG;

    const split_role_negotiate_payload_t *p =
        (const split_role_negotiate_payload_t *)payload;

    ESP_LOGD(TAG, "ROLE_NEGOTIATE from " MACSTR
             " usb=%u ble_bm=0x%02X last_role=%u",
             MAC2STR(src_mac),
             p->usb_connected, p->ble_connected_bitmap, p->last_role);

    *out_role = split_role_decide(
        own_mac, src_mac,
        own_usb_connected,  own_ble_connected_bitmap,  own_has_unsynced_ble, own_last_role,
        p->usb_connected,   p->ble_connected_bitmap,   p->has_unsynced_ble, (split_role_t)p->last_role);

    return ESP_OK;
}

void split_role_on_swap_req(split_role_t current_role, split_role_t *out_new_role)
{
    *out_new_role = flip_role(current_role);
}

void split_role_on_swap_ack(split_role_t current_role, split_role_t *out_new_role)
{
    *out_new_role = flip_role(current_role);
}
