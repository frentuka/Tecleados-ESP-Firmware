/**
 * @file usbmod.h
 * @brief Shim — declares USB module functions implemented in mock_tinyusb.h.
 *
 * Replaces the production usbmod.h which pulls in usb_defs.h (C23 syntax)
 * and tinyusb.h. Only the functions actually called by linked production
 * code are declared here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mocks/mock_tinyusb.h"
