#pragma once

#include <stdbool.h>
#include <stdint.h>

/* =========================================================================
 * Split USB configurator callback.
 *
 * Handles commands coming from the host-side configurator through
 * MODULE_SPLIT: pairing control, role swap, remote-matrix query, and the RTT
 * benchmark controls. JSON responses for queries are built here.
 * ========================================================================= */

/** @brief Registered with usbmod as the MODULE_SPLIT callback. */
bool split_usb_callback(uint8_t *data, uint16_t data_len);
