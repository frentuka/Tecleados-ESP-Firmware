#include "split_pair.h"
#include "split_protocol.h"
#include "split_transport.h"
#include "cfgmod.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"

#define TAG "SPLIT_PAIR"

#define NVS_PAIR_KEY "pair"

/* =========================================================================
 * State
 * ========================================================================= */

static split_pair_data_t   s_pair   = {0};
static split_pair_phase_t  s_phase  = PAIR_PHASE_IDLE;
static split_crypto_ecdh_t s_ecdh   = NULL;   // ephemeral ECDH handle (initiator path)
static uint64_t            s_seq    = 0;       // pairing-phase tx sequence counter

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static void ecdh_cleanup(void)
{
    if (s_ecdh) {
        split_crypto_ecdh_free(s_ecdh);
        s_ecdh = NULL;
    }
}

// Reset pairing data to a clean unpaired state with default channel.
static void reset_pair_data(void)
{
    memset(&s_pair, 0, sizeof(s_pair));
    s_pair.channel = 1;
}

// Allocate an ephemeral ECDH keypair and extract the public key.
// On failure the context is freed and *out_ctx is set to NULL.
static esp_err_t generate_ephemeral_keypair(split_crypto_ecdh_t *out_ctx,
                                             uint8_t out_pub[SPLIT_CRYPTO_PUBKEY_SIZE])
{
    esp_err_t ret = split_crypto_ecdh_start(out_ctx);
    if (ret != ESP_OK) return ret;

    ret = split_crypto_ecdh_get_public(*out_ctx, out_pub);
    if (ret != ESP_OK) {
        split_crypto_ecdh_free(*out_ctx);
        *out_ctx = NULL;
    }
    return ret;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

esp_err_t split_pair_init(void)
{
    size_t len = sizeof(s_pair);
    esp_err_t ret = cfgmod_read_storage(CFGMOD_KIND_SPLIT, NVS_PAIR_KEY, &s_pair, &len);

    if (ret == ESP_ERR_NOT_FOUND || len != sizeof(s_pair)) {
        reset_pair_data();
        ESP_LOGI(TAG, "no pairing data in NVS");
        return ESP_OK;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS read error (%s), starting unpaired", esp_err_to_name(ret));
        reset_pair_data();
        return ESP_OK;
    }

    if (s_pair.paired) {
        ESP_LOGI(TAG, "loaded pairing data, peer=" MACSTR, MAC2STR(s_pair.peer_mac));
    } else {
        ESP_LOGI(TAG, "NVS record found but not paired");
    }
    return ESP_OK;
}

esp_err_t split_pair_start(void)
{
    ecdh_cleanup();
    s_phase = PAIR_PHASE_BROADCASTING;
    ESP_LOGI(TAG, "pairing started (broadcasting DISCOVERY)");
    return ESP_OK;
}

void split_pair_cancel(void)
{
    ecdh_cleanup();
    s_phase = PAIR_PHASE_IDLE;
    ESP_LOGI(TAG, "pairing cancelled");
}

size_t split_pair_build_discovery(uint8_t *out, size_t out_max,
                                   const uint8_t own_mac[6],
                                   uint8_t preferred_role)
{
    if (!out || out_max < sizeof(split_discovery_payload_t)) return 0;

    split_discovery_payload_t *p = (split_discovery_payload_t *)out;
    p->version          = SPLIT_FRAME_VERSION;
    p->preferred_role   = preferred_role;
    p->firmware_version = 0x0001;
    memcpy(p->device_id, own_mac, 6);
    return sizeof(split_discovery_payload_t);
}

esp_err_t split_pair_on_discovery(const uint8_t *src_mac,
                                   const uint8_t *payload, size_t len,
                                   const uint8_t own_mac[6])
{
    if (s_phase != PAIR_PHASE_BROADCASTING) return ESP_ERR_INVALID_STATE;
    if (len < sizeof(split_discovery_payload_t)) return ESP_ERR_INVALID_SIZE;

    const split_discovery_payload_t *disc = (const split_discovery_payload_t *)payload;

    if (disc->version != SPLIT_FRAME_VERSION) {
        ESP_LOGW(TAG, "discovery: version mismatch peer=%u us=%u",
                 disc->version, SPLIT_FRAME_VERSION);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "discovery from " MACSTR " (role_pref=%u)",
             MAC2STR(src_mac), disc->preferred_role);

    ecdh_cleanup();
    uint8_t our_pub[SPLIT_CRYPTO_PUBKEY_SIZE];
    esp_err_t ret = generate_ephemeral_keypair(&s_ecdh, our_pub);
    if (ret != ESP_OK) return ret;

    split_transport_add_peer(src_mac, 0);

    split_pair_payload_t req = {0};
    memcpy(req.device_id,  own_mac,  6);
    memcpy(req.public_key, our_pub, SPLIT_CRYPTO_PUBKEY_SIZE);

    ret = split_transport_send(src_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_PAIR_REQUEST,
                               s_seq++, (const uint8_t *)&req, sizeof(req));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send PAIR_REQUEST failed: %s", esp_err_to_name(ret));
        ecdh_cleanup();
        return ret;
    }

    // Remember peer MAC; ECDH handle kept alive until we receive PAIR_RESPONSE.
    memcpy(s_pair.peer_mac, src_mac, 6);
    s_phase = PAIR_PHASE_SENT_REQUEST;
    ESP_LOGI(TAG, "PAIR_REQUEST sent to " MACSTR, MAC2STR(src_mac));
    return ESP_OK;
}

esp_err_t split_pair_on_pair_request(const uint8_t *src_mac,
                                      const uint8_t *payload, size_t len,
                                      const uint8_t own_mac[6])
{
    if (s_phase == PAIR_PHASE_SENT_REQUEST) {
        // Race: both sides sent PAIR_REQUEST simultaneously.
        // Device with lower MAC waits for its PAIR_RESPONSE; higher MAC becomes responder.
        if (memcmp(own_mac, src_mac, 6) < 0) {
            ESP_LOGD(TAG, "race: lower MAC, waiting for our PAIR_RESPONSE");
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(TAG, "race: higher MAC → becoming responder");
        ecdh_cleanup();
    } else if (s_phase != PAIR_PHASE_BROADCASTING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (len < sizeof(split_pair_payload_t)) return ESP_ERR_INVALID_SIZE;
    const split_pair_payload_t *req = (const split_pair_payload_t *)payload;

    split_crypto_ecdh_t tmp = NULL;
    uint8_t our_pub[SPLIT_CRYPTO_PUBKEY_SIZE];
    esp_err_t ret = generate_ephemeral_keypair(&tmp, our_pub);
    if (ret != ESP_OK) return ret;

    uint8_t key[SPLIT_CRYPTO_KEY_SIZE];
    ret = split_crypto_ecdh_derive_key(tmp, req->public_key, key);
    split_crypto_ecdh_free(tmp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "key derivation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memcpy(s_pair.peer_mac,   src_mac, 6);
    memcpy(s_pair.shared_key, key, SPLIT_CRYPTO_KEY_SIZE);
    s_pair.channel = 1;
    s_pair.paired  = 1;

    split_transport_add_peer(src_mac, 0);

    split_pair_payload_t rsp = {0};
    memcpy(rsp.device_id,  own_mac,  6);
    memcpy(rsp.public_key, our_pub, SPLIT_CRYPTO_PUBKEY_SIZE);

    ret = split_transport_send(src_mac, SPLIT_PROTO_SPLIT, SPLIT_MSG_PAIR_RESPONSE,
                               s_seq++, (const uint8_t *)&rsp, sizeof(rsp));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send PAIR_RESPONSE failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_err_t save_ret = split_pair_save();
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(save_ret));
    }

    s_phase = PAIR_PHASE_COMPLETE;
    ESP_LOGI(TAG, "pairing complete (responder), peer=" MACSTR, MAC2STR(src_mac));
    return ESP_OK;
}

esp_err_t split_pair_on_pair_response(const uint8_t *src_mac,
                                       const uint8_t *payload, size_t len)
{
    if (s_phase != PAIR_PHASE_SENT_REQUEST) return ESP_ERR_INVALID_STATE;
    if (!s_ecdh) return ESP_ERR_INVALID_STATE;
    if (len < sizeof(split_pair_payload_t)) return ESP_ERR_INVALID_SIZE;

    if (memcmp(src_mac, s_pair.peer_mac, 6) != 0) {
        ESP_LOGW(TAG, "PAIR_RESPONSE from unexpected MAC " MACSTR, MAC2STR(src_mac));
        return ESP_ERR_INVALID_ARG;
    }

    const split_pair_payload_t *rsp = (const split_pair_payload_t *)payload;

    uint8_t key[SPLIT_CRYPTO_KEY_SIZE];
    esp_err_t ret = split_crypto_ecdh_derive_key(s_ecdh, rsp->public_key, key);
    ecdh_cleanup();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "key derivation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memcpy(s_pair.shared_key, key, SPLIT_CRYPTO_KEY_SIZE);
    s_pair.channel = 1;
    s_pair.paired  = 1;

    esp_err_t save_ret = split_pair_save();
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(save_ret));
    }

    s_phase = PAIR_PHASE_COMPLETE;
    ESP_LOGI(TAG, "pairing complete (initiator), peer=" MACSTR, MAC2STR(src_mac));
    return ESP_OK;
}

esp_err_t split_pair_save(void)
{
    return cfgmod_write_storage(CFGMOD_KIND_SPLIT, NVS_PAIR_KEY,
                                &s_pair, sizeof(s_pair));
}

esp_err_t split_pair_clear(void)
{
    ecdh_cleanup();
    reset_pair_data();
    s_phase = PAIR_PHASE_IDLE;
    return cfgmod_write_storage(CFGMOD_KIND_SPLIT, NVS_PAIR_KEY,
                                &s_pair, sizeof(s_pair));
}

bool split_pair_get_data(split_pair_data_t *out)
{
    if (!s_pair.paired) return false;
    if (out) *out = s_pair;
    return true;
}

bool split_pair_is_paired(void)          { return s_pair.paired != 0; }
split_pair_phase_t split_pair_get_phase(void) { return s_phase; }
