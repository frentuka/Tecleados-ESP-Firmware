// ESP-IDF 5.5.2 — Steps 1 and 2 (ESP32-S3)
// Build as a normal app_main() project.
// Wire:
//  Step 1: BTN pin -> momentary button -> GND
//  Step 2: COL pin -> switch -> ROW pin
//          (ROW uses internal pull-up; COL is driven HIGH/LOW)

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "kb_matrix.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_err.h"
#include "soc/gpio_periph.h"

typedef struct {
	uint8_t index;
	gpio_num_t gpio;
} kb_gpio_t;

static const char *TAG = "kb_matrix";

static const kb_gpio_t k_rows[] = {
	GPIO_ROWS
};

static const kb_gpio_t k_cols[] = {
	GPIO_COLS
};

static uint64_t kb_build_pin_mask(const kb_gpio_t *pins, size_t count) {
	uint64_t mask = 0;
	for (size_t i = 0; i < count; ++i) {
		mask |= (1ULL << pins[i].gpio);
	}
	return mask;
}

static bool kb_validate_pins(const kb_gpio_t *pins, size_t count, bool output_required) {
	bool ok = true;
	for (size_t i = 0; i < count; ++i) {
		gpio_num_t gpio = pins[i].gpio;
		if (!GPIO_IS_VALID_GPIO(gpio)) {
			ESP_LOGE(TAG, "Invalid GPIO: %d", (int)gpio);
			ok = false;
			continue;
		}
		if (output_required && !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
			ESP_LOGE(TAG, "GPIO not output-capable: %d", (int)gpio);
			ok = false;
		}
	}
	return ok;
}

/*
    scanning
*/
static inline void kb_set_bit(uint8_t *bitmap, size_t bit_index) {
	bitmap[bit_index >> 3] |= (uint8_t)(1U << (bit_index & 7U));
}

void scan(uint8_t *out_matrix_bitmap) {
	const size_t row_count = sizeof(k_rows) / sizeof(k_rows[0]);
	const size_t col_count = sizeof(k_cols) / sizeof(k_cols[0]);
	const size_t total_bits = row_count * col_count;
	const size_t total_bytes = (total_bits + 7U) / 8U;

	memset(out_matrix_bitmap, 0, total_bytes);

	for (size_t c = 0; c < col_count; ++c) {
		gpio_set_level(k_cols[c].gpio, 0);
		taskYIELD();

		for (size_t r = 0; r < row_count; ++r) {
			int level = gpio_get_level(k_rows[r].gpio);
			if (level == 0) {
				size_t bit_index = (k_rows[r].index * col_count) + k_cols[c].index;
				kb_set_bit(out_matrix_bitmap, bit_index);
			}
		}

		gpio_set_level(k_cols[c].gpio, 1);
	}
}

/*
    init
*/
void kb_matrix_gpio_init(void) {
	ESP_LOGI(TAG, "Initializing GPIO matrix");

	const size_t row_count = sizeof(k_rows) / sizeof(k_rows[0]);
	const size_t col_count = sizeof(k_cols) / sizeof(k_cols[0]);

	if (!kb_validate_pins(k_rows, row_count, false) ||
		!kb_validate_pins(k_cols, col_count, true)) {
		ESP_LOGE(TAG, "GPIO selection invalid; aborting matrix init");
		return;
	}

	ESP_LOGI(TAG, "GPIO pins validated");

	ESP_LOGI(TAG, "Configuring row GPIOs");
	for (size_t i = 0; i < row_count; ++i) {
		gpio_config_t row_cfg = {
			.pin_bit_mask = (1ULL << k_rows[i].gpio),
			.mode = GPIO_MODE_INPUT,
			.pull_up_en = GPIO_PULLUP_ENABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type = GPIO_INTR_DISABLE,
		};
		ESP_LOGI(TAG, "Configuring row GPIO %d", (int)k_rows[i].gpio);
		esp_err_t row_err = gpio_config(&row_cfg);
		if (row_err != ESP_OK) {
			ESP_LOGE(TAG, "Row GPIO %d config failed: %s", (int)k_rows[i].gpio, esp_err_to_name(row_err));
			return;
		}
	}

	ESP_LOGI(TAG, "Configuring col GPIOs");
	for (size_t i = 0; i < col_count; ++i) {
		gpio_config_t col_cfg = {
			.pin_bit_mask = (1ULL << k_cols[i].gpio),
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type = GPIO_INTR_DISABLE,
		};
		ESP_LOGI(TAG, "Configuring col GPIO %d", (int)k_cols[i].gpio);
		esp_err_t col_err = gpio_config(&col_cfg);
		if (col_err != ESP_OK) {
			ESP_LOGE(TAG, "Col GPIO %d config failed: %s", (int)k_cols[i].gpio, esp_err_to_name(col_err));
			return;
		}
	}

	ESP_LOGI(TAG, "GPIO pins configured");

	for (size_t i = 0; i < col_count; ++i) {
		gpio_set_level(k_cols[i].gpio, 1);
	}

	ESP_LOGI(TAG, "Columns level set to HIGH");
}

static TaskHandle_t s_task_to_notify = NULL;

static void IRAM_ATTR kb_matrix_isr_handler(void *arg) {
	if (s_task_to_notify) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		vTaskNotifyGiveFromISR(s_task_to_notify, &xHigherPriorityTaskWoken);
		if (xHigherPriorityTaskWoken) {
			portYIELD_FROM_ISR();
		}
	}
}

void kb_matrix_init_isr(TaskHandle_t task_to_notify) {
	s_task_to_notify = task_to_notify;
	const size_t row_count = sizeof(k_rows) / sizeof(k_rows[0]);
	for (size_t i = 0; i < row_count; ++i) {
		gpio_isr_handler_add(k_rows[i].gpio, kb_matrix_isr_handler, (void *)k_rows[i].gpio);
	}
}

void kb_matrix_set_interrupts_enabled(bool enabled) {
	const size_t row_count = sizeof(k_rows) / sizeof(k_rows[0]);
	const size_t col_count = sizeof(k_cols) / sizeof(k_cols[0]);

	if (enabled) {
		// To detect ANY key press, drive ALL columns LOW
		for (size_t i = 0; i < col_count; ++i) {
			gpio_set_level(k_cols[i].gpio, 0);
		}
		// Enable interrupts on rows (falling edge since they are pulled up and columns are LOW)
		for (size_t i = 0; i < row_count; ++i) {
			gpio_set_intr_type(k_rows[i].gpio, GPIO_INTR_NEGEDGE);
			gpio_intr_enable(k_rows[i].gpio);
		}
	} else {
		// Disable interrupts on rows
		for (size_t i = 0; i < row_count; ++i) {
			gpio_intr_disable(k_rows[i].gpio);
			gpio_set_intr_type(k_rows[i].gpio, GPIO_INTR_DISABLE);
		}
		// Drive columns back to HIGH (default scanning state)
		for (size_t i = 0; i < col_count; ++i) {
			gpio_set_level(k_cols[i].gpio, 1);
		}
	}
}