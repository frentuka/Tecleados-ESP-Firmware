#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "comm_defs.h"
#include "comm_transport.h"

void comm_dispatch_init(void);
void comm_register_callback(comm_module_id_t callback_module, comm_data_callback_t callback);
bool comm_execute_callback(comm_transport_t source, comm_module_id_t callback_module, uint8_t const *data, uint16_t data_len);
comm_transport_t comm_get_current_source(void);
void comm_dispatch_enqueue(comm_transport_t source, const uint8_t *packet, uint16_t len);
