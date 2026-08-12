#include "cfg_physical.h"

#include "cfgmod.h"
#include "cfg_storage_keys.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

#define TAG "cfg_physical"
#define CFG_PHYSICAL_LAYOUT_ID_BUFSIZE 32

// Default physical layout ID
static const char DEFAULT_PHYS_LAYOUT_ID[] = "stag-65p";

// The "struct" for physical layout is just a raw string buffer in this context.

static void phys_default(void *out_struct) {
    char *s = (char *)out_struct;
    strncpy(s, DEFAULT_PHYS_LAYOUT_ID, CFG_PHYSICAL_LAYOUT_ID_BUFSIZE - 1);
    s[CFG_PHYSICAL_LAYOUT_ID_BUFSIZE - 1] = '\0';
}

void cfg_physical_register(void) {
    // Registering with a 32 byte buffer size.
    cfgmod_register_kind(CFGMOD_KIND_PHYSICAL, phys_default, NULL, CFG_PHYSICAL_LAYOUT_ID_BUFSIZE);

    // Initialize physical layout ID in NVS if it is empty or has mismatched length
    char temp_buf[32];
    size_t len = sizeof(temp_buf);
    esp_err_t err = cfgmod_read_storage(CFGMOD_KIND_PHYSICAL, CFG_ST_PHYSICAL_LAYOUT, temp_buf, &len);
    
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Initializing default physical layout ID in NVS");
        // Store only the actual string length (plus null terminator)
        size_t id_len = strlen(DEFAULT_PHYS_LAYOUT_ID) + 1;
        cfgmod_write_storage(CFGMOD_KIND_PHYSICAL, CFG_ST_PHYSICAL_LAYOUT, DEFAULT_PHYS_LAYOUT_ID, id_len);
    }
}
