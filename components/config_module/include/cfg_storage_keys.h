#pragma once

#define CFG_ST_HELLO "hello" // testing purposes only
#define CFG_ST_PHYSICAL_LAYOUT "physical"

#define CFG_ST_LAYER_0 "ly0"
#define CFG_ST_LAYER_1 "ly1"
#define CFG_ST_LAYER_2 "ly2"
#define CFG_ST_LAYER_3 "ly3"

#define CFG_ST_SHAKESPEARE "shakespeare" // testing purposes only

typedef enum usb_msg_cmd_kind: uint8_t {
	CMD_KIND_LAYOUT = 0,
	CMD_KIND_MACRO,
    CMD_KIND_CONNECTION,
	CMD_KIND_SYSTEM
} usb_msg_cmd_kind_t;