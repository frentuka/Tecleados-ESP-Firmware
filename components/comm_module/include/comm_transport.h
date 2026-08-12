#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMM_TRANSPORT_NONE = -1,
    COMM_TRANSPORT_USB = 0,
    COMM_TRANSPORT_BLE,
    COMM_TRANSPORT_COUNT
} comm_transport_t;

/** Sentinel: broadcast to ALL connected transports.
 *  Used by event-driven callers (e.g., unsolicited status pushes) that need
 *  to reach every active configurator, regardless of transport. */
#define COMM_TRANSPORT_BROADCAST ((comm_transport_t)0x7F)

typedef struct {
    /** Send a single packet up to max_packet_size.
     *  The packet is already CRC-stamped. Returns true on success. */
    bool (*send_packet)(const uint8_t *packet, uint16_t len);

    /** Returns true if this transport is physically connected and ready. */
    bool (*is_ready)(void);

    /** Returns the maximum packet size (including 5-byte protocol overhead) 
     *  supported by this transport currently. */
    uint16_t (*get_max_packet_size)(void);
} comm_transport_ops_t;

/** Register a transport's operations. Called once during init. */
void comm_transport_register(comm_transport_t id,
                             const comm_transport_ops_t *ops);

/** Get the operations for a specific transport. Returns NULL if not registered. */
const comm_transport_ops_t *comm_transport_get(comm_transport_t id);

/** Called by a transport driver when it receives a raw packet.
 *  Enqueues the packet along with the source transport ID. */
void comm_transport_receive_packet(comm_transport_t source,
                                   const uint8_t *packet, uint16_t len);

/** Mark a transport as having an active configurator connection. */
void comm_transport_set_connected(comm_transport_t id, bool connected);

/** Check if a specific transport has an active configurator connection. */
bool comm_transport_is_connected(comm_transport_t id);

/** Check if ANY transport has an active configurator connection. */
bool comm_transport_any_connected(void);
