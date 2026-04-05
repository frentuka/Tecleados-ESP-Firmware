#include "split_role.h"
#include "split_protocol.h"

#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"

#define TAG "SPLIT_ROLE"

static split_role_t flip_role(split_role_t current)
{
    return (current == SPLIT_ROLE_MASTER) ? SPLIT_ROLE_SLAVE : SPLIT_ROLE_MASTER;
}

split_role_t split_role_decide(const uint8_t own_mac[6],
                                const uint8_t peer_mac[6],
                                uint8_t own_pref,
                                uint8_t peer_pref)
{
    bool own_wants_master  = (own_pref  == SPLIT_ROLE_MASTER);
    bool own_wants_slave   = (own_pref  == SPLIT_ROLE_SLAVE);
    bool peer_wants_master = (peer_pref == SPLIT_ROLE_MASTER);
    bool peer_wants_slave  = (peer_pref == SPLIT_ROLE_SLAVE);

    // Unambiguous explicit preference wins.
    if (own_wants_master && !peer_wants_master) return SPLIT_ROLE_MASTER;
    if (own_wants_slave  && !peer_wants_slave)  return SPLIT_ROLE_SLAVE;

    // Tiebreaker: higher MAC = MASTER.
    int cmp = memcmp(own_mac, peer_mac, 6);
    split_role_t role = (cmp > 0) ? SPLIT_ROLE_MASTER : SPLIT_ROLE_SLAVE;

    ESP_LOGD(TAG, "role decided by MAC: %s (own_pref=%u peer_pref=%u)",
             role == SPLIT_ROLE_MASTER ? "MASTER" : "SLAVE", own_pref, peer_pref);
    return role;
}

esp_err_t split_role_on_negotiate(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6],
                                   uint8_t own_pref,
                                   split_role_t *out_role)
{
    if (len < sizeof(split_role_payload_t)) return ESP_ERR_INVALID_SIZE;
    if (!out_role) return ESP_ERR_INVALID_ARG;

    const split_role_payload_t *p = (const split_role_payload_t *)payload;

    ESP_LOGD(TAG, "ROLE_NEGOTIATE from " MACSTR " proposed_role=%u",
             MAC2STR(src_mac), p->proposed_role);

    *out_role = split_role_decide(own_mac, src_mac, own_pref, p->proposed_role);
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
