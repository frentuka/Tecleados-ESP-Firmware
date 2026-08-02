#pragma once

#include <stdbool.h>
#include "comm_transport.h"

/** Initialize the session manager mutex. */
void comm_session_init(void);

/** 
 * Attempt to acquire the blast session lock for a specific transport.
 * Called when a FIRST packet with remaining > 0 arrives (multi-packet transfer).
 * Returns true if the lock was acquired or already held by this transport.
 * Returns false if another transport currently holds the lock (blast in progress).
 */
bool comm_session_try_lock(comm_transport_t transport);

/** 
 * Release the blast session lock.
 * Called on blast completion, timeout, disconnect, or abort.
 */
void comm_session_unlock(void);

/** 
 * Get the transport currently holding the blast lock.
 * Returns COMM_TRANSPORT_NONE if no blast is active.
 */
comm_transport_t comm_session_get_active(void);
