#include "split_bench.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "kb_manager.h"

#include "split_session.h"
#include "split_transport.h"
#include "split_protocol.h"

#define TAG "SPLIT_BN"

/* ── Bench parameters ──────────────────────────────────────────────────── */
#define BENCH_PROBES         20          /* RTT probe count                 */
#define BENCH_PROBE_MS       50          /* Inter-probe spacing (ms)        */
#define BENCH_POLL_DWELL_MS  2000        /* Poll-rate measurement window    */
#define BENCH_SETTLE_MS      0           /* No extra settle for manual runs */
#define BENCH_TIMEOUT_MS     (BENCH_PROBE_MS * (BENCH_PROBES + 6))

/* ── Phase state machine ───────────────────────────────────────────────── */
typedef enum {
    BENCH_PHASE_IDLE = 0,
    BENCH_PHASE_RTT,         /* Sending/awaiting PING/PONG probes          */
    BENCH_PHASE_POLL_DWELL,  /* 2-second dwell for poll-rate measurement   */
} bench_phase_t;

typedef struct {
    bench_phase_t phase;
    uint8_t    probes_sent;
    uint8_t    pongs_received;
    uint32_t   rtt_us[BENCH_PROBES];
    TickType_t phase_started_at;
    TickType_t last_probe_tx;

    /* RTT results */
    uint32_t   result_min_us;
    uint32_t   result_avg_us;
    uint32_t   result_max_us;
    uint8_t    result_lost;
    uint8_t    result_sent;   /* = BENCH_PROBES (for display as "lost/sent") */

    /* Poll-rate results (local = master half, remote = slave half) */
    uint32_t   local_scan_hz;
    uint32_t   local_floor_scan_hz;
    uint32_t   local_peak_scan_hz;
    uint32_t   remote_scan_hz;
    uint32_t   remote_floor_scan_hz;
    uint32_t   remote_peak_scan_hz;
} bench_t;

static bench_t s_bench = {0};

/* ── Helpers ───────────────────────────────────────────────────────────── */
static inline bool bench_active(void) { return s_bench.phase != BENCH_PHASE_IDLE; }

static void snap_local(void)
{
    kb_poll_rate_snapshot_t snap = {0};
    kb_manager_get_poll_rate(&snap);
    s_bench.local_scan_hz       = snap.scan_hz;
    s_bench.local_floor_scan_hz = snap.floor_scan_hz;
    s_bench.local_peak_scan_hz  = snap.peak_scan_hz;
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

void split_bench_start(void)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED ||
        split_session_get_role()  != SPLIT_ROLE_MASTER) {
        ESP_LOGW(TAG, "bench start ignored: not master/connected (state=%u role=%u)",
                 (unsigned)split_session_get_state(), (unsigned)split_session_get_role());
        return;
    }

    /* Preserve previous poll-rate data across re-runs. */
    uint32_t pls = s_bench.local_scan_hz,  plf = s_bench.local_floor_scan_hz,  plp = s_bench.local_peak_scan_hz;
    uint32_t prs = s_bench.remote_scan_hz, prf = s_bench.remote_floor_scan_hz, prp = s_bench.remote_peak_scan_hz;

    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.phase           = BENCH_PHASE_RTT;
    s_bench.result_sent     = BENCH_PROBES;
    s_bench.phase_started_at = xTaskGetTickCount();
    s_bench.last_probe_tx   = s_bench.phase_started_at - pdMS_TO_TICKS(BENCH_PROBE_MS);

    s_bench.local_scan_hz        = pls; s_bench.local_floor_scan_hz  = plf; s_bench.local_peak_scan_hz   = plp;
    s_bench.remote_scan_hz       = prs; s_bench.remote_floor_scan_hz = prf; s_bench.remote_peak_scan_hz  = prp;

    snap_local(); /* Overwrite with fresh snapshot immediately */

    ESP_LOGI(TAG, "RTT benchmark started (%d probes, %d ms apart; poll dwell %d ms)",
             BENCH_PROBES, BENCH_PROBE_MS, BENCH_POLL_DWELL_MS);
}

/* Called when all RTT probes are done (or timed out). Transitions to dwell. */
static void rtt_finish(void)
{
    uint32_t min_us = UINT32_MAX, max_us = 0, sum_us = 0;
    uint8_t  valid  = 0;

    for (int i = 0; i < BENCH_PROBES; i++) {
        if (s_bench.rtt_us[i] == 0) continue;
        if (s_bench.rtt_us[i] < min_us) min_us = s_bench.rtt_us[i];
        if (s_bench.rtt_us[i] > max_us) max_us = s_bench.rtt_us[i];
        sum_us += s_bench.rtt_us[i];
        valid++;
    }

    s_bench.result_lost = BENCH_PROBES - valid;
    if (valid > 0) {
        s_bench.result_min_us = min_us / 2;
        s_bench.result_avg_us = (sum_us / valid) / 2;
        s_bench.result_max_us = max_us / 2;
        ESP_LOGI(TAG, "Delay (RTT/2) (%u/%u): min=%lu us  avg=%lu us  max=%lu us  lost=%u",
                 valid, BENCH_PROBES,
                 (unsigned long)s_bench.result_min_us, (unsigned long)s_bench.result_avg_us,
                 (unsigned long)s_bench.result_max_us, s_bench.result_lost);
    } else {
        ESP_LOGW(TAG, "RTT: no pongs received");
    }

    /* Transition to poll-rate dwell phase. */
    s_bench.phase            = BENCH_PHASE_POLL_DWELL;
    s_bench.phase_started_at = xTaskGetTickCount();
    kb_manager_set_force_active(true);
    ESP_LOGI(TAG, "Starting %d ms poll-rate dwell...", BENCH_POLL_DWELL_MS);
}

/* Called after the poll-rate dwell completes. Snapshot + request slave data. */
static void poll_dwell_finish(void)
{
    s_bench.phase = BENCH_PHASE_IDLE;
    kb_manager_set_force_active(false);

    snap_local();
    ESP_LOGI(TAG, "Poll-rate (local): scan=%lu Hz  floor=%lu Hz  peak=%lu Hz",
             (unsigned long)s_bench.local_scan_hz,
             (unsigned long)s_bench.local_floor_scan_hz,
             (unsigned long)s_bench.local_peak_scan_hz);

    split_bench_request_poll_rate();
}

/* ── Tick ──────────────────────────────────────────────────────────────── */

void split_bench_tick(TickType_t now)
{
    switch (s_bench.phase) {
    case BENCH_PHASE_RTT: {
        TickType_t elapsed = now - s_bench.phase_started_at;

        /* Hard timeout guard */
        if (elapsed >= pdMS_TO_TICKS(BENCH_SETTLE_MS + BENCH_TIMEOUT_MS)) {
            ESP_LOGW(TAG, "RTT timed out (%u/%u pongs)", s_bench.pongs_received, s_bench.probes_sent);
            rtt_finish();
            break;
        }

        bool more_probes = s_bench.probes_sent < BENCH_PROBES;
        bool probe_due   = (now - s_bench.last_probe_tx) >= pdMS_TO_TICKS(BENCH_PROBE_MS);

        if (!more_probes || !probe_due) break;

        split_ping_payload_t ping = {
            .probe_idx = s_bench.probes_sent,
            .sent_us   = (uint32_t)esp_timer_get_time(),
        };
        if (split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                                 SPLIT_MSG_PING, split_session_next_seq(),
                                 (const uint8_t *)&ping, sizeof(ping)) == ESP_OK) {
            s_bench.probes_sent++;
        }
        s_bench.last_probe_tx = now;
        break;
    }
    case BENCH_PHASE_POLL_DWELL: {
        TickType_t elapsed = now - s_bench.phase_started_at;
        if (elapsed >= pdMS_TO_TICKS(BENCH_POLL_DWELL_MS)) {
            poll_dwell_finish();
        }
        break;
    }
    default:
        break;
    }
}

/* ── Inbound handlers ──────────────────────────────────────────────────── */

void split_bench_handle_ping(const uint8_t *peer_mac,
                             const uint8_t *payload, size_t len)
{
    split_transport_send(peer_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_PONG,
                         split_session_next_seq(), payload, len);
}

void split_bench_handle_pong(const uint8_t *payload, size_t len)
{
    if (s_bench.phase != BENCH_PHASE_RTT || len < sizeof(split_ping_payload_t)) return;

    const split_ping_payload_t *p = (const split_ping_payload_t *)payload;
    if (p->probe_idx >= BENCH_PROBES) return;

    uint32_t rtt = (uint32_t)esp_timer_get_time() - p->sent_us;
    s_bench.rtt_us[p->probe_idx] = rtt;
    s_bench.pongs_received++;
    ESP_LOGD(TAG, "PONG #%u: RTT = %lu us", p->probe_idx, (unsigned long)rtt);

    if (s_bench.pongs_received >= BENCH_PROBES) {
        rtt_finish();
    }
}

/* ── Poll-rate measurement ─────────────────────────────────────────────── */

void split_bench_request_poll_rate(void)
{
    if (split_session_get_state() != SPLIT_STATE_CONNECTED ||
        split_session_get_role()  != SPLIT_ROLE_MASTER) {
        return;
    }

    snap_local();

    split_transport_send(split_session_peer_mac(), SPLIT_PROTO_SPLIT,
                         SPLIT_MSG_POLL_RATE_REQ, split_session_next_seq(),
                         NULL, 0);

    ESP_LOGD(TAG, "POLL_RATE_REQ sent (local: scan=%lu Hz  floor=%lu Hz  peak=%lu Hz)",
             (unsigned long)s_bench.local_scan_hz,
             (unsigned long)s_bench.local_floor_scan_hz,
             (unsigned long)s_bench.local_peak_scan_hz);
}

void split_bench_handle_poll_rate_resp(const uint8_t *payload, size_t len)
{
    if (len < sizeof(split_poll_rate_payload_t)) return;
    const split_poll_rate_payload_t *p = (const split_poll_rate_payload_t *)payload;

    s_bench.remote_scan_hz       = p->scan_hz;
    s_bench.remote_floor_scan_hz = p->floor_scan_hz;
    s_bench.remote_peak_scan_hz  = p->peak_scan_hz;

    ESP_LOGI(TAG, "POLL_RATE_RESP: remote scan=%lu Hz  floor=%lu Hz  peak=%lu Hz",
             (unsigned long)p->scan_hz,
             (unsigned long)p->floor_scan_hz,
             (unsigned long)p->peak_scan_hz);
}

/* ── Introspection ─────────────────────────────────────────────────────── */

bool split_bench_is_active(void) { return bench_active(); }

size_t split_bench_format_json(char *out, size_t out_max)
{
    if (!out || out_max == 0) return 0;
    int n = snprintf(out, out_max,
                     "{\"active\":%s"
                     ",\"min\":%lu,\"avg\":%lu,\"max\":%lu"
                     ",\"lost\":%u,\"sent\":%u"
                     ",\"local_scan_hz\":%lu,\"local_floor_hz\":%lu,\"local_peak_hz\":%lu"
                     ",\"remote_scan_hz\":%lu,\"remote_floor_hz\":%lu,\"remote_peak_hz\":%lu}",
                     bench_active() ? "true" : "false",
                     (unsigned long)s_bench.result_min_us,
                     (unsigned long)s_bench.result_avg_us,
                     (unsigned long)s_bench.result_max_us,
                     s_bench.result_lost,
                     s_bench.result_sent,
                     (unsigned long)s_bench.local_scan_hz,
                     (unsigned long)s_bench.local_floor_scan_hz,
                     (unsigned long)s_bench.local_peak_scan_hz,
                     (unsigned long)s_bench.remote_scan_hz,
                     (unsigned long)s_bench.remote_floor_scan_hz,
                     (unsigned long)s_bench.remote_peak_scan_hz);
    if (n < 0 || (size_t)n >= out_max) return 0;
    return (size_t)n;
}
