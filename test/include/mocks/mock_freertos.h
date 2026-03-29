/**
 * @file mock_freertos.h
 * @brief Mock FreeRTOS primitives for host testing.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/* ---- Types ---- */

typedef uint32_t TickType_t;
typedef void*    TaskHandle_t;
typedef void*    SemaphoreHandle_t;
typedef void*    QueueHandle_t;
typedef int      BaseType_t;
typedef unsigned UBaseType_t;

#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   1
#define pdFAIL   0
#define portMAX_DELAY 0xFFFFFFFF

/* ---- Tick simulation ---- */

static TickType_t _mock_tick_count = 0;

static inline TickType_t xTaskGetTickCount(void) {
    return _mock_tick_count;
}

static inline void mock_tick_set(TickType_t ticks) {
    _mock_tick_count = ticks;
}

static inline void mock_tick_advance(TickType_t delta) {
    _mock_tick_count += delta;
}

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(t)  ((uint32_t)(t))
#define configTICK_RATE_HZ 1000

/* ---- Task ---- */

#define vTaskDelay(ticks) do { _mock_tick_count += (ticks); } while (0)

static inline BaseType_t xTaskCreateWithCaps(void (*fn)(void*), const char *name,
                                              uint32_t stack, void *arg, int prio,
                                              TaskHandle_t *out, uint32_t caps) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)caps;
    if (out) *out = (TaskHandle_t)1;
    return pdPASS;
}

/* ---- Semaphore ---- */

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)1;
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (SemaphoreHandle_t)1;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) {
    (void)sem; (void)timeout;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    (void)sem;
    return pdTRUE;
}

/* ---- Queue ---- */

/* Simple mock queue: stores up to 64 items */
#define MOCK_QUEUE_MAX 64

typedef struct {
    uint8_t buf[MOCK_QUEUE_MAX * 256];
    size_t  item_size;
    int     head;
    int     tail;
    int     count;
    int     capacity;
} mock_queue_t;

static mock_queue_t _mock_queues[16];
static int _mock_queue_count = 0;

static inline QueueHandle_t xQueueCreate(int length, size_t item_size) {
    if (_mock_queue_count >= 16) return NULL;
    mock_queue_t *q = &_mock_queues[_mock_queue_count];
    q->item_size = item_size;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->capacity = length < MOCK_QUEUE_MAX ? length : MOCK_QUEUE_MAX;
    return (QueueHandle_t)(uintptr_t)(++_mock_queue_count);
}

static inline BaseType_t xQueueSend(QueueHandle_t handle, const void *item, TickType_t timeout) {
    (void)timeout;
    int idx = (int)(uintptr_t)handle - 1;
    if (idx < 0 || idx >= _mock_queue_count) return pdFAIL;
    mock_queue_t *q = &_mock_queues[idx];
    if (q->count >= q->capacity) return pdFAIL;
    memcpy(q->buf + q->tail * q->item_size, item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return pdTRUE;
}

static inline BaseType_t xQueueReceive(QueueHandle_t handle, void *item, TickType_t timeout) {
    (void)timeout;
    int idx = (int)(uintptr_t)handle - 1;
    if (idx < 0 || idx >= _mock_queue_count) return pdFAIL;
    mock_queue_t *q = &_mock_queues[idx];
    if (q->count == 0) return pdFAIL;
    memcpy(item, q->buf + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return pdTRUE;
}

static inline void mock_freertos_reset(void) {
    _mock_tick_count = 0;
    _mock_queue_count = 0;
    memset(_mock_queues, 0, sizeof(_mock_queues));
}
