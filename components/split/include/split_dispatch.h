#pragma once

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Split dispatch — protocol-level message routing.
 *
 * Owns the `on_split_message` receive callback and the per-message-type
 * handlers. Every handler is short: input-validate, update split_session,
 * and delegate role / matrix / BLE / bench side-effects to the appropriate
 * submodule.
 * ========================================================================= */

/**
 * @brief Receive callback registered with split_transport for SPLIT_PROTO_SPLIT.
 *        Runs in the WiFi task context.
 */
void split_dispatch_on_message(const uint8_t *src_mac,
                                uint8_t type, uint16_t seq,
                                const uint8_t *payload, size_t len,
                                const uint8_t *mic);
