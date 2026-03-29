/**
 * @file mock_esp.h
 * @brief Mock ESP-IDF base types and logging — replaces esp_err.h, esp_log.h,
 *        esp_timer.h, esp_heap_caps.h for host testing.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- esp_err.h ---- */

typedef int esp_err_t;

#define ESP_OK              0
#define ESP_FAIL           -1
#define ESP_ERR_NO_MEM      0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND   0x105
#define ESP_ERR_NVS_NO_FREE_PAGES 0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1101
#define ESP_ERR_NVS_NOT_FOUND 0x1102

/* ---- esp_log.h ---- */

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

#define ESP_LOGE(tag, fmt, ...) (void)0
#define ESP_LOGW(tag, fmt, ...) (void)0
#define ESP_LOGI(tag, fmt, ...) (void)0
#define ESP_LOGD(tag, fmt, ...) (void)0
#define ESP_LOGV(tag, fmt, ...) (void)0

/* ---- esp_timer.h ---- */

static int64_t _mock_timer_us = 0;

static inline int64_t esp_timer_get_time(void) {
    return _mock_timer_us;
}

static inline void mock_timer_set(int64_t us) {
    _mock_timer_us = us;
}

static inline void mock_timer_advance(int64_t delta_us) {
    _mock_timer_us += delta_us;
}

/* ---- esp_heap_caps.h ---- */

#define MALLOC_CAP_SPIRAM   (1 << 0)
#define MALLOC_CAP_INTERNAL (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

/* ---- strlcpy (not available on all platforms) ---- */

#ifndef strlcpy
static inline size_t strlcpy(char *dst, const char *src, size_t siz) {
    size_t len = strlen(src);
    if (siz > 0) {
        size_t cp = len < siz - 1 ? len : siz - 1;
        memcpy(dst, src, cp);
        dst[cp] = '\0';
    }
    return len;
}
#endif
