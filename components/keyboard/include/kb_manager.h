#pragma once

#include <stdint.h>
#include <stdbool.h>

void kb_manager_set_paused(bool paused);

void kb_manager_start(void);

// NKRO test helper: send one key mapped from (row,col)
void kb_manager_test_nkro_keypress(uint8_t row, uint8_t col);

/**
 * @brief Register a callback invoked after each debounce cycle with the current
 *        stable matrix and active layer. Used by the split module (SLAVE role) to
 *        detect changes and send deltas to the master.
 *
 * Pass NULL to unregister. The callback runs in the kb_manager task context.
 * Thread-safe; may be called from any task.
 *
 * @param cb  Callback(matrix, KB_MATRIX_BITMAP_BYTES, active_layer), or NULL.
 */
void kb_manager_set_matrix_cb(void (*cb)(const uint8_t *matrix, size_t len,
                                          uint8_t layer));

/**
 * @brief Set scan-rate divisor for battery-aware power saving.
 *
 * Divides the nominal scan rate (1200 Hz) by @p divisor. Use 1 for full speed,
 * 2 for half speed (600 Hz), 4 for quarter speed (300 Hz), etc.
 * The value 0 is treated as 1 (full speed). Thread-safe.
 *
 * @param divisor  Rate divisor (1 = full speed, 2 = half, 4 = quarter, …)
 */
void kb_manager_set_scan_divisor(uint8_t divisor);

/**
 * @brief Set (or clear) the remote half's key matrix for split-keyboard MASTER mode.
 *
 * When non-NULL, the bitmap is OR'd into the local debounced matrix on every scan,
 * so the master processes remote keys through the same layout/macro pipeline.
 * Pass NULL to clear (e.g., on disconnect or when this device is the slave).
 *
 * Thread-safe; may be called from any task.
 *
 * @param bitmap  KB_MATRIX_BITMAP_BYTES byte array, or NULL to zero the remote matrix.
 */
void kb_manager_set_remote_matrix(const uint8_t *bitmap);

/**
 * @brief Snapshot of the keyboard scanner's achieved polling rate.
 *
 * Updated once per second by the kb_manager task.  Read atomically by
 * kb_manager_get_poll_rate() using the module-level spinlock.
 */
typedef struct {
    uint32_t scan_hz;         /**< Average matrix scans per second over the last 1-second window. */
    uint32_t floor_scan_hz;   /**< Floor scan rate: 1 / max_scan_interval (Hz). Worst-case in window. */
    uint32_t peak_scan_hz;    /**< Peak scan rate: 1 / min_scan_interval (Hz). Best-case in window. */
    uint32_t peak_report_hz;  /**< Peak HID report rate (Hz). Zero on slave half. */
} kb_poll_rate_snapshot_t;

/**
 * @brief Read the latest polling-rate snapshot.
 *
 * Thread-safe; may be called from any task context.
 *
 * @param[out] out  Filled with the most recent 1-second averages.
 *                  Both fields are 0 until the first full second window elapses.
 */
void kb_manager_get_poll_rate(kb_poll_rate_snapshot_t *out);

/**
 * @brief Force the scan task to run at maximum rate (bypass idle interrupt sleep).
 *
 * When true the scan loop busy-waits (1ms ticks) instead of sleeping until a
 * key-press interrupt fires.  Use during poll-rate benchmarks so the measured
 * rate reflects full-speed scanning, not the idle rate (~10 Hz).
 * Must be called from a task context; safe to call concurrently.
 *
 * @param active  true = force full-rate; false = return to normal idle sleep.
 */
void kb_manager_set_force_active(bool active);