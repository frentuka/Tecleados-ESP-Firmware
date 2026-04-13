#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

/* =========================================================================
 * RTT benchmark — master-driven round-trip measurement.
 *
 * Flow:  MASTER sends PING probes; SLAVE echoes them as PONG. MASTER
 *        computes RTT = now_us - sent_us when each PONG returns and
 *        summarises min / avg / max / lost at the end of the run.
 *
 * The benchmark is started explicitly (configurator or auto-start on
 * connect) and ticked from split_task while the link is up.
 * ========================================================================= */

/** @brief Begin a new benchmark run. Clears prior results. */
void split_bench_start(void);

/** @brief Per-tick driver, runs probes from the connected MASTER side. */
void split_bench_tick(TickType_t now);

/** @brief SLAVE: echo an inbound PING back as PONG. */
void split_bench_handle_ping(const uint8_t *peer_mac,
                             const uint8_t *payload, size_t len);

/** @brief MASTER: record a PONG and finish the run once all probes are in. */
void split_bench_handle_pong(const uint8_t *payload, size_t len);

/** @brief True while probes are still being sent / awaited. */
bool split_bench_is_active(void);

/**
 * @brief Format the latest results as JSON into @p out.
 * @return Number of bytes written (excluding NUL), or 0 on error.
 */
size_t split_bench_format_json(char *out, size_t out_max);
