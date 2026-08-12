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

#include "comm_transport.h"
/** @brief Registered with comm_module as the MODULE_SPLIT callback. */
bool split_usb_callback(comm_transport_t source, uint8_t *data, uint16_t data_len);
