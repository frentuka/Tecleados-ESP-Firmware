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
 *
 * Poll-rate measurement:
 *   After the RTT probes finish, MASTER sends SPLIT_MSG_POLL_RATE_REQ
 *   to the SLAVE.  SLAVE replies with SPLIT_MSG_POLL_RATE_RESP containing
 *   its kb_manager scan-rate snapshot.  MASTER reads its own snapshot
 *   locally.  Both are included in split_bench_format_json().
 * ========================================================================= */

/** @brief Begin a new benchmark run. Clears prior RTT results and triggers
 *         a fresh poll-rate snapshot request to the slave. */
void split_bench_start(void);

/** @brief Per-tick driver, runs probes from the connected MASTER side. */
void split_bench_tick(TickType_t now);

/** @brief SLAVE: echo an inbound PING back as PONG. */
void split_bench_handle_ping(const uint8_t *peer_mac,
                             const uint8_t *payload, size_t len);

/** @brief MASTER: record a PONG and finish the run once all probes are in. */
void split_bench_handle_pong(const uint8_t *payload, size_t len);

/**
 * @brief MASTER: send SPLIT_MSG_POLL_RATE_REQ to the SLAVE to fetch its
 *        scan-rate snapshot.  Reads local snapshot from kb_manager immediately.
 *        May be called any time the link is CONNECTED; not just during a bench run.
 */
void split_bench_request_poll_rate(void);

/**
 * @brief MASTER: store the SLAVE's poll-rate snapshot received via
 *        SPLIT_MSG_POLL_RATE_RESP.
 */
void split_bench_handle_poll_rate_resp(const uint8_t *payload, size_t len);

/** @brief True while probes are still being sent / awaited. */
bool split_bench_is_active(void);

/**
 * @brief Format the latest results as JSON into @p out.
 *
 * JSON fields:
 *   active, min, avg, max, lost           — RTT benchmark results (us)
 *   local_scan_hz, local_peak_hz          — master-half poll-rate
 *   remote_scan_hz, remote_peak_hz        — slave-half poll-rate (0 if unknown)
 *
 * @return Number of bytes written (excluding NUL), or 0 on error.
 */
size_t split_bench_format_json(char *out, size_t out_max);
