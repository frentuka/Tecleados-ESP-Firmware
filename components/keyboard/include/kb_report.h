#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t kb_send_report(const uint8_t *report);