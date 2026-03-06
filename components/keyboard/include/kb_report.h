#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

bool kb_hid_ready(void);
esp_err_t kb_send_report(const uint8_t *report);
esp_err_t kb_send_consumer_report(uint16_t media_keycode);