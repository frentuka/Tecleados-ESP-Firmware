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
                                uint8_t own_pref,
                                uint8_t peer_pref,
                                uint8_t own_usb_connected,
                                uint8_t own_ble_connected,
                                split_role_t own_last_role,
                                uint8_t peer_usb_connected,
                                uint8_t peer_ble_connected,
                                split_role_t peer_last_role)
{
    // Priority 1: explicit user preference from pair config.
    // If one side has an uncontested explicit preference it wins immediately.
    bool own_wants_master  = (own_pref  == SPLIT_ROLE_MASTER);
    bool own_wants_slave   = (own_pref  == SPLIT_ROLE_SLAVE);
    bool peer_wants_master = (peer_pref == SPLIT_ROLE_MASTER);
    bool peer_wants_slave  = (peer_pref == SPLIT_ROLE_SLAVE);

    if (own_wants_master && !peer_wants_master) return SPLIT_ROLE_MASTER;
    if (own_wants_slave  && !peer_wants_slave)  return SPLIT_ROLE_SLAVE;

    // Priority 2: USB host connection.
    // The device with an active USB connection owns the wired output path.
    if ( own_usb_connected && !peer_usb_connected) return SPLIT_ROLE_MASTER;
    if (!own_usb_connected &&  peer_usb_connected) return SPLIT_ROLE_SLAVE;

    // Priority 3: BLE host connection.
    // The device with an active BLE connection owns the wireless output path.
    if ( own_ble_connected && !peer_ble_connected) return SPLIT_ROLE_MASTER;
    if (!own_ble_connected &&  peer_ble_connected) return SPLIT_ROLE_SLAVE;

    // Priority 4: last persisted role.
    // After a role swap or clean boot the recorded role drives continuity,
    // preventing spurious inversions when the split link drops and reconnects.
    if (own_last_role == SPLIT_ROLE_MASTER && peer_last_role != SPLIT_ROLE_MASTER)
        return SPLIT_ROLE_MASTER;
    if (own_last_role == SPLIT_ROLE_SLAVE  && peer_last_role != SPLIT_ROLE_SLAVE)
        return SPLIT_ROLE_SLAVE;

    // Priority 5: higher MAC address → MASTER (fully deterministic tiebreaker).
    int cmp = memcmp(own_mac, peer_mac, 6);
    split_role_t role = (cmp > 0) ? SPLIT_ROLE_MASTER : SPLIT_ROLE_SLAVE;

    ESP_LOGD(TAG, "role decided by MAC tiebreaker: %s "
             "(own_pref=%u peer_pref=%u own_usb=%u peer_usb=%u "
             "own_ble=%u peer_ble=%u own_last=%u peer_last=%u)",
             role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE",
             own_pref, peer_pref,
             own_usb_connected, peer_usb_connected,
             own_ble_connected, peer_ble_connected,
             (uint8_t)own_last_role, (uint8_t)peer_last_role);
    return role;
}

esp_err_t split_role_on_negotiate(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6],
                                   uint8_t own_pref,
                                   uint8_t own_usb_connected,
                                   uint8_t own_ble_connected,
                                   split_role_t own_last_role,
                                   split_role_t *out_role)
{
    if (len < sizeof(split_role_negotiate_payload_t)) return ESP_ERR_INVALID_SIZE;
    if (!out_role) return ESP_ERR_INVALID_ARG;

    const split_role_negotiate_payload_t *p =
        (const split_role_negotiate_payload_t *)payload;

    ESP_LOGD(TAG, "ROLE_NEGOTIATE from " MACSTR
             " proposed=%u usb=%u ble=%u last_role=%u",
             MAC2STR(src_mac),
             p->proposed_role, p->usb_connected,
             p->ble_connected, p->last_role);

    *out_role = split_role_decide(
        own_mac, src_mac,
        own_pref,           p->proposed_role,
        own_usb_connected,  own_ble_connected,  own_last_role,
        p->usb_connected,   p->ble_connected,   (split_role_t)p->last_role);

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
