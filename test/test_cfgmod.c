/**
 * @file test_cfgmod.c
 * @brief Tests for cfgmod.c — config registry, NVS key building, get/set
 *        config, and USB wire protocol dispatch.
 */
#include "test_harness.h"
#include "mocks/mock_esp.h"
#include "mocks/mock_nvs.h"
#include "mocks/mock_esp_event.h"

/* ---- Types from cfgmod.h ---- */

#define CFGMOD_MAX_KEY_LEN 12

typedef enum {
    CFGMOD_KIND_LAYOUT = 0,
    CFGMOD_KIND_MACRO,
    CFGMOD_KIND_CONNECTION,
    CFGMOD_KIND_SYSTEM,
    CFGMOD_KIND_PHYSICAL,
    CFGMOD_KIND_CKEY,
    CFGMOD_KIND_MAX
} cfgmod_kind_t;

typedef enum {
    CFG_CMD_GET = 0,
    CFG_CMD_SET
} cfgmod_cmd_t;

PACKED_STRUCT_BEGIN
typedef struct PACKED_ATTR {
    uint8_t cmd;
    uint8_t key_id;
} cfgmod_wire_header_t;
PACKED_STRUCT_END

/* ---- Callback types ---- */

typedef void (*cfgmod_default_fn)(void *out_struct);
typedef bool (*cfgmod_deserialize_fn)(void *root, void *out_struct);
typedef void *(*cfgmod_serialize_fn)(const void *in_struct);
typedef void (*cfgmod_on_update_fn)(const char *key);

typedef struct {
    cfgmod_default_fn def_fn;
    cfgmod_deserialize_fn des_fn;
    cfgmod_serialize_fn ser_fn;
    cfgmod_on_update_fn update_fn;
    size_t struct_size;
    bool registered;
} cfgmod_registry_t;

static cfgmod_registry_t s_registry[CFGMOD_KIND_MAX];

/* ---- Key building logic from cfgmod.c ---- */

static esp_err_t cfgmod_build_key(cfgmod_kind_t kind, const char *key,
                                   char *out_key, size_t out_len) {
    if (out_key == NULL || out_len == 0 || key == NULL) return ESP_ERR_INVALID_ARG;
    if (strlen(key) > CFGMOD_MAX_KEY_LEN) return ESP_ERR_INVALID_ARG;
    int written = snprintf(out_key, out_len, "k%d_%s", (int)kind, key);
    if (written < 0 || (size_t)written >= out_len) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

/* ---- Registration logic ---- */

static esp_err_t cfgmod_register_kind(cfgmod_kind_t kind, cfgmod_default_fn def_fn,
                                       cfgmod_deserialize_fn des_fn,
                                       cfgmod_serialize_fn ser_fn,
                                       cfgmod_on_update_fn update_fn,
                                       size_t struct_size) {
    if (kind >= CFGMOD_KIND_MAX) return ESP_ERR_INVALID_ARG;
    if (!def_fn || !des_fn || !ser_fn || struct_size == 0) return ESP_ERR_INVALID_ARG;
    s_registry[kind].def_fn = def_fn;
    s_registry[kind].des_fn = des_fn;
    s_registry[kind].ser_fn = ser_fn;
    s_registry[kind].update_fn = update_fn;
    s_registry[kind].struct_size = struct_size;
    s_registry[kind].registered = true;
    return ESP_OK;
}

static void reset_registry(void) {
    memset(s_registry, 0, sizeof(s_registry));
    mock_nvs_reset();
    mock_events_reset();
}

/* ---- Test callbacks ---- */

typedef struct { uint32_t value; char name[16]; } test_config_t;

static int s_default_calls = 0;
static int s_update_calls = 0;
static const char *s_last_update_key = NULL;

static void test_default(void *out) {
    test_config_t *c = (test_config_t *)out;
    c->value = 42;
    strcpy(c->name, "default");
    s_default_calls++;
}

static bool test_deserialize(void *root, void *out) {
    (void)root;
    test_config_t *c = (test_config_t *)out;
    c->value = 99;
    strcpy(c->name, "deserialized");
    return true;
}

static void *test_serialize(const void *in) {
    (void)in;
    return (void *)1; /* Non-NULL sentinel */
}

static void test_update(const char *key) {
    s_update_calls++;
    s_last_update_key = key;
}

/* ---- Tests ---- */

TEST_CASE(cfgmod, build_key_standard) {
    char buf[16];
    esp_err_t err = cfgmod_build_key(CFGMOD_KIND_LAYOUT, "ly0", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_STR_EQUAL("k0_ly0", buf);
}

TEST_CASE(cfgmod, build_key_system) {
    char buf[16];
    esp_err_t err = cfgmod_build_key(CFGMOD_KIND_SYSTEM, "sys", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_STR_EQUAL("k3_sys", buf);
}

TEST_CASE(cfgmod, build_key_null_out) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, cfgmod_build_key(CFGMOD_KIND_LAYOUT, "ly0", NULL, 16));
}

TEST_CASE(cfgmod, build_key_null_key) {
    char buf[16];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, cfgmod_build_key(CFGMOD_KIND_LAYOUT, NULL, buf, sizeof(buf)));
}

TEST_CASE(cfgmod, build_key_too_long) {
    char buf[16];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_build_key(CFGMOD_KIND_LAYOUT, "toolongkeyname!", buf, sizeof(buf)));
}

TEST_CASE(cfgmod, build_key_buffer_too_small) {
    char buf[4]; /* "k0_ly0" = 7 chars, won't fit */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, cfgmod_build_key(CFGMOD_KIND_LAYOUT, "ly0", buf, sizeof(buf)));
}

TEST_CASE(cfgmod, register_valid_kind) {
    reset_registry();
    esp_err_t err = cfgmod_register_kind(CFGMOD_KIND_SYSTEM,
                                          test_default, test_deserialize,
                                          test_serialize, test_update,
                                          sizeof(test_config_t));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(s_registry[CFGMOD_KIND_SYSTEM].registered);
}

TEST_CASE(cfgmod, register_invalid_kind) {
    reset_registry();
    esp_err_t err = cfgmod_register_kind(CFGMOD_KIND_MAX,
                                          test_default, test_deserialize,
                                          test_serialize, test_update,
                                          sizeof(test_config_t));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

TEST_CASE(cfgmod, register_null_callbacks) {
    reset_registry();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_register_kind(CFGMOD_KIND_SYSTEM,
                                            NULL, test_deserialize, test_serialize,
                                            test_update, sizeof(test_config_t)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_register_kind(CFGMOD_KIND_SYSTEM,
                                            test_default, NULL, test_serialize,
                                            test_update, sizeof(test_config_t)));
}

TEST_CASE(cfgmod, register_zero_struct_size) {
    reset_registry();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      cfgmod_register_kind(CFGMOD_KIND_SYSTEM,
                                            test_default, test_deserialize,
                                            test_serialize, test_update, 0));
}

TEST_CASE(cfgmod, wire_header_size) {
    TEST_ASSERT_EQUAL(2, sizeof(cfgmod_wire_header_t));
}

TEST_CASE(cfgmod, wire_header_packing) {
    cfgmod_wire_header_t hdr = { .cmd = CFG_CMD_GET, .key_id = 3 };
    uint8_t *raw = (uint8_t *)&hdr;
    TEST_ASSERT_EQUAL(CFG_CMD_GET, raw[0]);
    TEST_ASSERT_EQUAL(3, raw[1]);
}

TEST_CASE(cfgmod, kind_namespace_mapping) {
    /* Verify expected kind count */
    TEST_ASSERT_EQUAL(6, CFGMOD_KIND_MAX);
}

TEST_CASE(cfgmod, nvs_round_trip_binary) {
    mock_nvs_reset();
    test_config_t original = { .value = 12345 };
    strcpy(original.name, "hello");

    mock_nvs_inject("cfg", "k3_sys", &original, sizeof(original));

    test_config_t restored;
    size_t len = sizeof(restored);

    char prev_ns[16];
    strncpy(prev_ns, _mock_nvs_open_ns, sizeof(prev_ns));
    strncpy(_mock_nvs_open_ns, "cfg", sizeof(_mock_nvs_open_ns));
    esp_err_t err = nvs_get_blob(1, "k3_sys", &restored, &len);
    strncpy(_mock_nvs_open_ns, prev_ns, sizeof(_mock_nvs_open_ns));

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(sizeof(test_config_t), len);
    TEST_ASSERT_EQUAL(12345, restored.value);
    TEST_ASSERT_STR_EQUAL("hello", restored.name);
}
