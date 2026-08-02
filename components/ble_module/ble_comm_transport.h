#pragma once

#include <stdbool.h>
#include <stdint.h>

void ble_comm_transport_init(void);
void ble_comm_set_conn_handle(uint16_t conn_handle);
void ble_comm_set_subscribed(uint16_t conn_handle, bool subscribed);
void ble_comm_set_mtu_subscribed(uint16_t conn_handle, bool subscribed);
void ble_comm_on_disconnect(uint16_t conn_handle);
void ble_comm_on_mtu_change(uint16_t conn_handle, uint16_t mtu);
void ble_comm_reset_state(void);
uint16_t ble_comm_get_max_packet_size(void);
