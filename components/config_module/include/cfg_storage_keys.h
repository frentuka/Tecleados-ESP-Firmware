#pragma once

#define CFG_ST_HELLO "hello" // testing purposes only

#define CFG_ST_LAYOUT1 "layout1"
#define CFG_ST_LAYOUT2 "layout2"
#define CFG_ST_LAYOUT3 "layout3"
#define CFG_ST_LAYOUT4 "layout4"

#define CFG_ST_SHAKESPEARE "shakespeare" // testing purposes only

typedef enum usb_msg_cmd_kind: uint8_t {
	CMD_KIND_LAYOUT = 0,
	CMD_KIND_MACRO,
    CMD_KIND_CONNECTION,
	CMD_KIND_SYSTEM
} usb_msg_cmd_kind_t;