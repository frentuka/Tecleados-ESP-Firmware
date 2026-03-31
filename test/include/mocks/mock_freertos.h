/**
 * @file mock_freertos.h
 * @brief Mock FreeRTOS primitives for host testing.
 *
 * Provides semaphore state tracking, queue simulation with force-full/empty
 * controls, tick simulation, and task stubs.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- Critical sections (no-op on host) ---- */

typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux)   (void)(mux)
#define portEXIT_CRITICAL(mux)    (void)(mux)
#define taskENTER_CRITICAL(mux)   (void)(mux)
#define taskEXIT_CRITICAL(mux)    (void)(mux)
#define taskYIELD()

/* ---- Task ---- */

#define vTaskDelay(ticks) do { _mock_tick_count += (ticks); } while (0)

static inline BaseType_t xTaskCreateWithCaps(void (*fn)(void*), const char *name,
                                              uint32_t stack, void *arg, int prio,
                                              TaskHandle_t *out, uint32_t caps) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)caps;
    if (out) *out = (TaskHandle_t)1;
    return pdPASS;
}

static inline BaseType_t xTaskCreate(void (*fn)(void*), const char *name,
                                     uint32_t stack, void *arg, int prio,
                                     TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    if (out) *out = (TaskHandle_t)1;
    return pdPASS;
}

static inline void vTaskDelete(TaskHandle_t task) { (void)task; }

/* Task notification stubs (used by kb_matrix ISR pattern) */
static inline void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) {
    (void)task; if (woken) *woken = pdFALSE;
}
static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) {
    (void)clear; (void)timeout; return 1;
}
static inline BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    (void)task; return pdPASS;
}
#define portYIELD_FROM_ISR(x) (void)(x)

/* ---- Semaphore (with state tracking) ---- */

#define MOCK_MAX_SEMAPHORES 16

typedef struct {
    bool active;
    bool taken;
    bool is_mutex;
    bool force_fail;    /* test control: make next Take fail */
} mock_sem_state_t;

static mock_sem_state_t _mock_sems[MOCK_MAX_SEMAPHORES];
static int _mock_sem_count = 0;

static inline SemaphoreHandle_t _mock_sem_alloc(bool is_mutex) {
    if (_mock_sem_count >= MOCK_MAX_SEMAPHORES) return NULL;
    int idx = _mock_sem_count++;
    _mock_sems[idx].active = true;
    _mock_sems[idx].taken = false;
    _mock_sems[idx].is_mutex = is_mutex;
    _mock_sems[idx].force_fail = false;
    return (SemaphoreHandle_t)(uintptr_t)(idx + 1);
}

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return _mock_sem_alloc(true);
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return _mock_sem_alloc(false);
}

static inline SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t init) {
    (void)max; (void)init;
    return _mock_sem_alloc(false);
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) {
    int idx = (int)(uintptr_t)sem - 1;
    if (idx < 0 || idx >= _mock_sem_count) return pdFAIL;
    mock_sem_state_t *s = &_mock_sems[idx];

    if (s->force_fail) {
        s->force_fail = false;
        return pdFAIL;
    }
    if (s->taken && timeout == 0) return pdFAIL;
    s->taken = true;
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    int idx = (int)(uintptr_t)sem - 1;
    if (idx < 0 || idx >= _mock_sem_count) return pdFAIL;
    _mock_sems[idx].taken = false;
    return pdTRUE;
}

/* Test helpers for semaphores */
static inline void mock_sem_set_force_fail(SemaphoreHandle_t sem, bool fail) {
    int idx = (int)(uintptr_t)sem - 1;
    if (idx >= 0 && idx < _mock_sem_count) _mock_sems[idx].force_fail = fail;
}

static inline bool mock_sem_is_taken(SemaphoreHandle_t sem) {
    int idx = (int)(uintptr_t)sem - 1;
    if (idx < 0 || idx >= _mock_sem_count) return false;
    return _mock_sems[idx].taken;
}

/* ---- Queue (with force-full/empty controls) ---- */

#define MOCK_QUEUE_MAX 64

typedef struct {
    uint8_t buf[MOCK_QUEUE_MAX * 256];
    size_t  item_size;
    int     head;
    int     tail;
    int     count;
    int     capacity;
    bool    force_full;     /* test control: xQueueSend always fails */
    bool    force_empty;    /* test control: xQueueReceive always fails */
} mock_queue_t;

static mock_queue_t _mock_queues[16];
static int _mock_queue_count = 0;

static inline QueueHandle_t xQueueCreate(int length, size_t item_size) {
    if (_mock_queue_count >= 16) return NULL;
    mock_queue_t *q = &_mock_queues[_mock_queue_count];
    memset(q, 0, sizeof(mock_queue_t));
    q->item_size = item_size;
    q->capacity = length < MOCK_QUEUE_MAX ? length : MOCK_QUEUE_MAX;
    return (QueueHandle_t)(uintptr_t)(++_mock_queue_count);
}

static inline BaseType_t xQueueSend(QueueHandle_t handle, const void *item, TickType_t timeout) {
    (void)timeout;
    int idx = (int)(uintptr_t)handle - 1;
    if (idx < 0 || idx >= _mock_queue_count) return pdFAIL;
    mock_queue_t *q = &_mock_queues[idx];
    if (q->force_full || q->count >= q->capacity) return pdFAIL;
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
    if (q->force_empty || q->count == 0) return pdFAIL;
    memcpy(item, q->buf + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return pdTRUE;
}

static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle) {
    int idx = (int)(uintptr_t)handle - 1;
    if (idx < 0 || idx >= _mock_queue_count) return 0;
    return (UBaseType_t)_mock_queues[idx].count;
}

static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t handle) {
    int idx = (int)(uintptr_t)handle - 1;
    if (idx < 0 || idx >= _mock_queue_count) return 0;
    return (UBaseType_t)(_mock_queues[idx].capacity - _mock_queues[idx].count);
}

static inline BaseType_t xQueueReset(QueueHandle_t handle) {
    int idx = (int)(uintptr_t)handle - 1;
    if (idx < 0 || idx >= _mock_queue_count) return pdFAIL;
    _mock_queues[idx].head = 0;
    _mock_queues[idx].tail = 0;
    _mock_queues[idx].count = 0;
    return pdPASS;
}

/* Test helpers for queues */
static inline void mock_queue_set_force_full(QueueHandle_t handle, bool force) {
    int idx = (int)(uintptr_t)handle - 1;
    if (idx >= 0 && idx < _mock_queue_count) _mock_queues[idx].force_full = force;
}

static inline void mock_queue_set_force_empty(QueueHandle_t handle, bool force) {
    int idx = (int)(uintptr_t)handle - 1;
    if (idx >= 0 && idx < _mock_queue_count) _mock_queues[idx].force_empty = force;
}

/* ---- Global reset ---- */

static inline void mock_freertos_reset(void) {
    _mock_tick_count = 0;
    _mock_queue_count = 0;
    _mock_sem_count = 0;
    memset(_mock_queues, 0, sizeof(_mock_queues));
    memset(_mock_sems, 0, sizeof(_mock_sems));
}
