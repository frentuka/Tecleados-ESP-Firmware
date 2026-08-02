#pragma once

#include "comm_defs.h"
#include "comm_transport.h"
#include "comm_dispatch.h"
#include "comm_session.h"

void comm_init(void);
void comm_register_module(comm_module_id_t module_id, comm_data_callback_t callback);
bool comm_send_message(comm_transport_t target, comm_module_id_t module_id, const uint8_t *payload, uint16_t len);
