#include "split_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"

#include "kb_manager.h"
#include "blemod.h"
#include "cfg_ble.h"
#include "cfg_system.h"

#include "split_session.h"
#include "split_transport.h"
#include "split_protocol.h"
#include "split_sync.h"

#define TAG "SPLIT_BR"

/* Command IDs exchanged over SPLIT_MSG_BLE_CMD and the MODULE_BLE USB bus.
 * Keep in sync with the host-side configurator. */
#define BLE_USB_CMD_TOGGLE_ROUTING 0x01
#define BLE_USB_CMD_PAIR           0x02
#define BLE_USB_CMD_CONNECT        0x03
#define BLE_USB_CMD_TOGGLE_CONN    0x04

/* =========================================================================
 * Matrix forwarding — registered as the kb_manager matrix_cb when SLAVE
 * =========================================================================
 * Every slave-side matrix change is serialised into a KEY_STATE_FULL packet.
 * ESP-NOW is fire-and-forget; a dropped delta would corrupt the master's
 * view forever, so we always send the full 14-byte bitmap.
 * ========================================================================= */

#if CONFIG_PM_ENABLE
#include "esp_pm.h"
static void pm_apply_active(void)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz       = 240,
        .min_freq_mhz       = 240,
        .light_sleep_enable = false,
    };
    (void)esp_pm_configure(&cfg);
}
#else
static inline void pm_apply_active(void) {}
#endif

static void on_matrix_change(const uint8_t *matrix, size_t len, uint8_t layer)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED ||
        split_session_get_role()  != SPLIT_ROLE_SLAVE) {
        return;
    }

#if CONFIG_PM_ENABLE
    bool any_pressed = false;
    for (size_t i = 0; i < (len < SPLIT_MATRIX_BYTES ? len : SPLIT_MATRIX_BYTES); i++) {
        if (matrix[i]) { any_pressed = true; break; }
    }
    if (any_pressed) pm_apply_active();
#else
    (void)len;
#endif

    split_sync_send_full_state(split_session_peer_mac(), matrix, layer,
                               split_session_next_seq());
}

/* =========================================================================
 * BLE routing — suspend BLE on the slave so the master owns the host link
 * ========================================================================= */

static void apply_ble_routing_for_role(split_role_t role)
{
    bool should_suspend = (role == SPLIT_ROLE_SLAVE);
    if (ble_hid_is_suspended() == should_suspend) return;

    ESP_LOGI(TAG, "BLE routing → %s (role=%u)",
             should_suspend ? "SUSPENDED" : "RESUMED", (unsigned)role);

    if (!should_suspend) {
        // Bonds are already pre-warmed by config sync.  Skip directed ADV —
        // it reduces radio congestion during the initial heavy config sync.
        ble_hid_skip_directed_adv();
    }
    ble_hid_set_suspended(should_suspend);
}

/* =========================================================================
 * Keyboard-manager routing for the current role.
 *
 * SLAVE : capture the local matrix, stop producing HID reports, flush a clean
 *         initial matrix so the new master starts without ghost keys.
 * MASTER: drop the slave callback, resume HID, zero the remote matrix, and
 *         auto-populate ble_shared_addr on the first master promotion so
 *         both halves advertise the same address after a role swap.
 * ========================================================================= */

static void populate_ble_shared_addr_if_empty(void)
{
    cfg_system_t sys;
    if (cfg_system_get(&sys) != ESP_OK) return;

    for (int i = 0; i < 6; i++) {
        if (sys.ble_shared_addr[i]) return;   // already populated
    }
    esp_read_mac(sys.ble_shared_addr, ESP_MAC_BT);
    cfg_system_set(&sys);
    ESP_LOGI(TAG, "Auto-set ble_shared_addr from own BT MAC: " MACSTR,
             MAC2STR(sys.ble_shared_addr));
}

static void apply_kb_routing_for_role(split_role_t role)
{
    if (role == SPLIT_ROLE_SLAVE) {
        kb_manager_set_matrix_cb(on_matrix_change);
        kb_manager_set_remote_matrix(NULL);
        kb_manager_set_paused(true);

        uint8_t zero[SPLIT_MATRIX_BYTES] = {0};
        split_sync_send_full_state(split_session_peer_mac(), zero, 0,
                                   split_session_next_seq());
    } else {
        kb_manager_set_matrix_cb(NULL);
        kb_manager_set_paused(false);
        uint8_t zero[SPLIT_MATRIX_BYTES] = {0};
        kb_manager_set_remote_matrix(zero);
        populate_ble_shared_addr_if_empty();
    }
}

/* =========================================================================
 * Public routing API
 * ========================================================================= */

void split_bridge_apply_routing_for_role(split_role_t role)
{
    apply_kb_routing_for_role(role);
    // Becoming MASTER means taking over the host-facing output path — leave
    // any slave-side light sleep behind and run at full clock.
    if (role == SPLIT_ROLE_MASTER) pm_apply_active();
    apply_ble_routing_for_role(role);
}

void split_bridge_reset_routing_standalone(void)
{
    split_sync_clear_remote_matrix();
    kb_manager_set_remote_matrix(NULL);
    kb_manager_set_matrix_cb(NULL);
    kb_manager_set_paused(false);
    apply_ble_routing_for_role(SPLIT_ROLE_NONE);
    kb_manager_set_scan_divisor(1);
}

/* =========================================================================
 * BLE proxy — commands from the configurator, status push to slave
 * ========================================================================= */

void split_bridge_execute_ble_cmd(uint8_t cmd, uint8_t arg)
{
    switch (cmd) {
    case BLE_USB_CMD_TOGGLE_ROUTING:
        ble_hid_set_routing_active(!ble_hid_is_routing_active());
        break;
    case BLE_USB_CMD_PAIR:
        ble_hid_profile_pair(arg);
        break;
    case BLE_USB_CMD_CONNECT:
        ble_hid_profile_connect_and_select(arg);
        break;
    case BLE_USB_CMD_TOGGLE_CONN:
        ble_hid_profile_toggle_connection(arg);
        break;
    default:
        ESP_LOGW(TAG, "unknown BLE cmd 0x%02X", cmd);
        break;
    }
}

void split_bridge_send_ble_status_to_slave(void)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED ||
        split_session_get_role()  != SPLIT_ROLE_MASTER) {
        return;
    }

    const cfg_ble_state_t *st = cfg_ble_get_state();
    split_ble_status_payload_t p = {
        .routing_active   = st->ble_routing_enabled ? 1 : 0,
        .selected_profile = (uint8_t)st->selected_profile,
        .connected_bitmap = ble_hid_get_connected_profiles_bitmap(),
        .pairing_profile  = (int8_t)ble_hid_get_pairing_profile(),
    };
    split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                         SPLIT_MSG_BLE_STATUS, split_session_next_seq(),
                         (const uint8_t *)&p, sizeof(p));
}

void split_bridge_on_ble_event(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data)
{
    (void)arg; (void)base; (void)event_id; (void)data;
    split_bridge_send_ble_status_to_slave();
}

bool split_bridge_ble_usb_callback(uint8_t *data, uint16_t data_len)
{
    if (!data || data_len < 1) return false;
    uint8_t cmd = data[0];
    uint8_t arg = (data_len >= 2) ? data[1] : 0;

    if (split_session_get_role()  == SPLIT_ROLE_SLAVE &&
        split_session_get_state() == SPLIT_STATE_CONNECTED) {
        split_ble_cmd_payload_t payload = {.cmd = cmd, .arg = arg};
        esp_err_t ret = split_transport_send(
            split_session_peer_mac(), SPLIT_PROTO_SPLIT, SPLIT_MSG_BLE_CMD,
            split_session_next_seq(),
            (const uint8_t *)&payload, sizeof(payload));
        return ret == ESP_OK;
    }

    split_bridge_execute_ble_cmd(cmd, arg);
    return true;
}
