#include "split_bench.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "split_session.h"
#include "split_transport.h"
#include "split_protocol.h"

#define TAG "SPLIT_BN"

#define BENCH_PROBES      10
#define BENCH_PROBE_MS    50
#define BENCH_SETTLE_MS  1500   // Wait after connect before first probe (must exceed config sync duration)
#define BENCH_TIMEOUT_MS (BENCH_PROBE_MS * (BENCH_PROBES + 6))

typedef struct {
    bool       active;
    uint8_t    probes_sent;
    uint8_t    pongs_received;
    uint32_t   rtt_us[BENCH_PROBES];
    TickType_t last_probe_tx;
    TickType_t started_at;
    uint32_t   result_min_us;
    uint32_t   result_avg_us;
    uint32_t   result_max_us;
    uint8_t    result_lost;
} bench_t;

static bench_t s_bench = {0};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

void split_bench_start(void)
{
    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.active        = true;
    s_bench.started_at    = xTaskGetTickCount();
    s_bench.last_probe_tx = s_bench.started_at - pdMS_TO_TICKS(BENCH_PROBE_MS);
    ESP_LOGI(TAG, "RTT benchmark started (%d probes, %d ms apart)", BENCH_PROBES, BENCH_PROBE_MS);
}

static void bench_finish(void)
{
    s_bench.active = false;

    uint32_t min_us = UINT32_MAX, max_us = 0, sum_us = 0;
    uint8_t  valid  = 0;

    for (int i = 0; i < BENCH_PROBES; i++) {
        if (s_bench.rtt_us[i] == 0) continue;
        if (s_bench.rtt_us[i] < min_us) min_us = s_bench.rtt_us[i];
        if (s_bench.rtt_us[i] > max_us) max_us = s_bench.rtt_us[i];
        sum_us += s_bench.rtt_us[i];
        valid++;
    }

    if (valid == 0) {
        ESP_LOGW(TAG, "RTT benchmark: no pongs received (all %d timed out)", BENCH_PROBES);
        return;
    }

    s_bench.result_min_us = min_us;
    s_bench.result_avg_us = sum_us / valid;
    s_bench.result_max_us = max_us;
    s_bench.result_lost   = BENCH_PROBES - valid;

    ESP_LOGI(TAG, "RTT benchmark (%u/%u): min=%lu us  avg=%lu us  max=%lu us  lost=%u",
             valid, BENCH_PROBES,
             (unsigned long)min_us,
             (unsigned long)s_bench.result_avg_us,
             (unsigned long)max_us,
             s_bench.result_lost);

    if (s_bench.result_lost > 0) {
        ESP_LOGW(TAG, "  %u probe(s) lost — check for radio contention or BLE coexistence",
                 s_bench.result_lost);
    }
}

/* =========================================================================
 * Tick — master side
 * ========================================================================= */

void split_bench_tick(TickType_t now)
{
    if (!s_bench.active) return;

    TickType_t elapsed = now - s_bench.started_at;

    if (elapsed >= pdMS_TO_TICKS(BENCH_SETTLE_MS + BENCH_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "RTT benchmark timed out (%u/%u pongs)",
                 s_bench.pongs_received, s_bench.probes_sent);
        bench_finish();
        return;
    }

    bool settle_done = elapsed >= pdMS_TO_TICKS(BENCH_SETTLE_MS);
    bool more_probes = s_bench.probes_sent < BENCH_PROBES;
    bool probe_due   = (now - s_bench.last_probe_tx) >= pdMS_TO_TICKS(BENCH_PROBE_MS);

    if (!settle_done || !more_probes || !probe_due) return;

    split_ping_payload_t ping = {
        .probe_idx = s_bench.probes_sent,
        .sent_us   = (uint32_t)esp_timer_get_time(),
    };
    if (split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                             SPLIT_MSG_PING, split_session_next_seq(),
                             (const uint8_t *)&ping, sizeof(ping)) == ESP_OK) {
        ESP_LOGD(TAG, "PING #%u sent", ping.probe_idx);
        s_bench.probes_sent++;
    }
    s_bench.last_probe_tx = now;
}

/* =========================================================================
 * Inbound handlers
 * ========================================================================= */

void split_bench_handle_ping(const uint8_t *peer_mac,
                             const uint8_t *payload, size_t len)
{
    split_transport_send(peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_PONG,
                         split_session_next_seq(), payload, len);
}

void split_bench_handle_pong(const uint8_t *payload, size_t len)
{
    if (!s_bench.active || len < sizeof(split_ping_payload_t)) return;

    const split_ping_payload_t *p = (const split_ping_payload_t *)payload;
    if (p->probe_idx >= BENCH_PROBES) return;

    uint32_t rtt = (uint32_t)esp_timer_get_time() - p->sent_us;
    s_bench.rtt_us[p->probe_idx] = rtt;
    s_bench.pongs_received++;
    ESP_LOGD(TAG, "PONG #%u: RTT = %lu us", p->probe_idx, (unsigned long)rtt);

    if (s_bench.pongs_received >= BENCH_PROBES) {
        bench_finish();
    }
}

/* =========================================================================
 * Introspection
 * ========================================================================= */

bool split_bench_is_active(void) { return s_bench.active; }

size_t split_bench_format_json(char *out, size_t out_max)
{
    if (!out || out_max == 0) return 0;
    int n = snprintf(out, out_max,
                     "{\"active\":%s,\"min\":%lu,\"avg\":%lu,\"max\":%lu,\"lost\":%u}",
                     s_bench.active ? "true" : "false",
                     (unsigned long)s_bench.result_min_us,
                     (unsigned long)s_bench.result_avg_us,
                     (unsigned long)s_bench.result_max_us,
                     s_bench.result_lost);
    if (n < 0 || (size_t)n >= out_max) return 0;
    return (size_t)n;
}
