/**
 * @file mock_esp_event.h
 * @brief Mock ESP-IDF event system for host testing.
 *
 * Tracks posted events and registered handlers so tests can verify
 * event-driven behavior without a real event loop.
 */
#pragma once

#include "mock_esp.h"
#include <string.h>

/* ---- esp_event types ---- */

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base,
                                     int32_t event_id, void *data);

#define ESP_EVENT_DECLARE_BASE(name) extern const char *name
#define ESP_EVENT_DEFINE_BASE(name)  const char *name = #name
#define ESP_EVENT_ANY_ID -1

/* ---- Posted event recording ---- */

#define MOCK_MAX_EVENTS 128

typedef struct {
    esp_event_base_t base;
    int32_t          event_id;
    uint8_t          data[256];
    size_t           data_size;
} mock_posted_event_t;

static mock_posted_event_t _mock_events[MOCK_MAX_EVENTS];
static int _mock_event_count = 0;

static inline esp_err_t esp_event_post(esp_event_base_t base, int32_t event_id,
                                        const void *data, size_t data_size,
                                        uint32_t ticks_to_wait) {
    (void)ticks_to_wait;
    if (_mock_event_count >= MOCK_MAX_EVENTS) return ESP_FAIL;
    mock_posted_event_t *e = &_mock_events[_mock_event_count++];
    e->base = base;
    e->event_id = event_id;
    e->data_size = data_size < sizeof(e->data) ? data_size : sizeof(e->data);
    if (data && e->data_size > 0) {
        memcpy(e->data, data, e->data_size);
    }
    return ESP_OK;
}

/* ---- Handler registration recording ---- */

#define MOCK_MAX_HANDLERS 64

typedef struct {
    esp_event_base_t    base;
    int32_t             event_id;
    esp_event_handler_t handler;
    void               *arg;
} mock_handler_reg_t;

static mock_handler_reg_t _mock_handlers[MOCK_MAX_HANDLERS];
static int _mock_handler_count = 0;

static inline esp_err_t esp_event_handler_register(esp_event_base_t base,
                                                    int32_t event_id,
                                                    esp_event_handler_t handler,
                                                    void *arg) {
    if (_mock_handler_count >= MOCK_MAX_HANDLERS) return ESP_FAIL;
    mock_handler_reg_t *h = &_mock_handlers[_mock_handler_count++];
    h->base = base;
    h->event_id = event_id;
    h->handler = handler;
    h->arg = arg;
    return ESP_OK;
}

static inline esp_err_t esp_event_loop_create_default(void) {
    return ESP_OK;
}

/* ---- Test helpers ---- */

static inline void mock_events_reset(void) {
    _mock_event_count = 0;
    _mock_handler_count = 0;
}

static inline int mock_event_count(void) {
    return _mock_event_count;
}

static inline const mock_posted_event_t *mock_event_get(int index) {
    if (index < 0 || index >= _mock_event_count) return NULL;
    return &_mock_events[index];
}

/** Find the last posted event matching base + event_id. Returns NULL if not found. */
static inline const mock_posted_event_t *mock_event_find_last(esp_event_base_t base,
                                                                int32_t event_id) {
    for (int i = _mock_event_count - 1; i >= 0; i--) {
        if (_mock_events[i].base == base && _mock_events[i].event_id == event_id) {
            return &_mock_events[i];
        }
    }
    return NULL;
}

/** Count events matching base + event_id. */
static inline int mock_event_count_matching(esp_event_base_t base, int32_t event_id) {
    int n = 0;
    for (int i = 0; i < _mock_event_count; i++) {
        if (_mock_events[i].base == base && _mock_events[i].event_id == event_id) n++;
    }
    return n;
}

/** Dispatch all pending events to matching registered handlers. */
static inline void mock_event_dispatch_all(void) {
    for (int e = 0; e < _mock_event_count; e++) {
        for (int h = 0; h < _mock_handler_count; h++) {
            if (_mock_handlers[h].base == _mock_events[e].base &&
                (_mock_handlers[h].event_id == ESP_EVENT_ANY_ID ||
                 _mock_handlers[h].event_id == _mock_events[e].event_id)) {
                _mock_handlers[h].handler(
                    _mock_handlers[h].arg,
                    _mock_events[e].base,
                    _mock_events[e].event_id,
                    _mock_events[e].data_size > 0 ? _mock_events[e].data : NULL
                );
            }
        }
    }
}
