#pragma once

#include <stdint.h>
#include "host/ble_hs.h"

extern const ble_uuid128_t comm_svc_uuid;

/**
 * @brief Initialize and register the COMM GATT service.
 */
void ble_comm_svc_register(void);

/**
 * @brief Get the attribute handle for the COMM TX characteristic.
 * @return uint16_t Attribute handle, or 0 if not registered.
 */
uint16_t ble_comm_get_tx_handle(void);

/**
 * @brief Get the attribute handle for the COMM MTU characteristic.
 * @return uint16_t Attribute handle, or 0 if not registered.
 */
uint16_t ble_comm_get_mtu_handle(void);
