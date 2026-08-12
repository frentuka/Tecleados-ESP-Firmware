#include "comm_session.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "comm_session";

static comm_transport_t s_active_transport = COMM_TRANSPORT_NONE;
static SemaphoreHandle_t s_session_mutex = NULL;

void comm_session_init(void) {
    if (s_session_mutex == NULL) {
        s_session_mutex = xSemaphoreCreateMutex();
    }
}

bool comm_session_try_lock(comm_transport_t transport) {
    if (s_session_mutex == NULL) return false;

    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_active_transport == COMM_TRANSPORT_NONE) {
            s_active_transport = transport;
            xSemaphoreGive(s_session_mutex);
            return true;
        } else if (s_active_transport == transport) {
            // Already holds it
            xSemaphoreGive(s_session_mutex);
            return true;
        } else {
            // Another transport holds it
            ESP_LOGW(TAG, "Lock denied for %d. Held by %d", transport, s_active_transport);
            xSemaphoreGive(s_session_mutex);
            return false;
        }
    }
    return false;
}

void comm_session_unlock(void) {
    if (s_session_mutex == NULL) return;

    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_active_transport != COMM_TRANSPORT_NONE) {
            s_active_transport = COMM_TRANSPORT_NONE;
        }
        xSemaphoreGive(s_session_mutex);
    }
}

comm_transport_t comm_session_get_active(void) {
    comm_transport_t active = COMM_TRANSPORT_NONE;
    if (s_session_mutex != NULL) {
        if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) == pdTRUE) {
            active = s_active_transport;
            xSemaphoreGive(s_session_mutex);
        }
    }
    return active;
}
