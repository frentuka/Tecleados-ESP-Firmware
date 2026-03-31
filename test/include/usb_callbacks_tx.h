/**
 * @file usb_callbacks_tx.h
 * @brief Shim — TX callback declarations for host testing.
 *
 * statusmod.c includes this header but doesn't call any TX functions
 * directly (it uses send_payload from usb_send.h). This shim just
 * provides the constants that may be referenced.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MAX_TX_BUF_SIZE 21500
#define TX_TIMEOUT_MS 5000
