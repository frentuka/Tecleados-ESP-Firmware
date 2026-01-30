#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_log.h"

const uint16_t PRINT_BYTES_AS_CHARS_MAX = 128;

static inline void print_bytes_as_chars(const char *tag, const uint8_t *data, size_t len) {
    size_t start = 0;
    while (start < len) {
        size_t buf_idx = 0;
        char buf[PRINT_BYTES_AS_CHARS_MAX];
        for (size_t i = start; i < len && buf_idx < PRINT_BYTES_AS_CHARS_MAX - 1; ++i) {
            char c = (char)data[i];
            if (c == '\n') {
                buf[buf_idx] = '\0';
                ESP_LOGI(tag, "Chars: %s", buf);
                buf_idx = 0;
                start = i + 1;
                // Print empty line if two consecutive \n
            } else {
                buf[buf_idx++] = c;
            }
        }
        // If buffer has content and we didn't end on a newline, print it
        if (buf_idx > 0) {
            buf[buf_idx] = '\0';
            ESP_LOGI(tag, "Chars: %s", buf);
            start += buf_idx;
        } else if (start < len && (char)data[start] == '\n') {
            // Handle trailing newline at end of data
            ESP_LOGI(tag, "Chars: ");
            start++;
        }
    }
}