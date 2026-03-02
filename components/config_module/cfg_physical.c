#include "cfg_physical.h"
#include "cJSON.h"
#include "cfgmod.h"
#include <string.h>
#include "esp_log.h"

#define TAG "cfg_physical"

// Default physical layout (65% layout) - Format: [row, col, w100, h100, x100, y100]
static const char DEFAULT_PHYS_JSON[] = "{\"rows\":6,\"cols\":18,\"layout\":[[0,0,100,100,0,0,0,1,100,100,100,0,0,2,100,100,200,0,0,3,100,100,300,0,0,4,100,100,400,0,0,5,100,100,500,0,0,6,100,100,600,0,0,7,100,100,700,0,0,8,100,100,800,0,0,9,100,100,900,0,0,10,100,100,1000,0,0,11,100,100,1100,0,0,12,100,100,1200,0,0,13,200,100,1300,0,0,14,100,100,1500,0],[1,0,150,100,0,100,1,1,100,100,150,100,1,2,100,100,250,100,1,3,100,100,350,100,1,4,100,100,450,100,1,5,100,100,550,100,1,6,100,100,650,100,1,7,100,100,750,100,1,8,100,100,850,100,1,9,100,100,950,100,1,10,100,100,1050,100,1,11,100,100,1150,100,1,12,100,100,1250,100,1,13,150,100,1350,100,1,14,100,100,1500,100],[2,0,175,100,0,200,2,1,100,100,175,200,2,2,100,100,275,200,2,3,100,100,375,200,2,4,100,100,475,200,2,5,100,100,575,200,2,6,100,100,675,200,2,7,100,100,775,200,2,8,100,100,875,200,2,9,100,100,975,200,2,10,100,100,1075,200,2,11,100,100,1175,200,2,12,225,100,1275,200,2,14,100,100,1500,200],[3,0,225,100,0,300,3,2,100,100,225,300,3,3,100,100,325,300,3,4,100,100,425,300,3,5,100,100,525,300,3,6,100,100,625,300,3,7,100,100,725,300,3,8,100,100,825,300,3,9,100,100,925,300,3,10,100,100,1025,300,3,11,100,100,1125,300,3,12,175,100,1225,300,3,13,100,100,1400,300,3,14,100,100,1500,300],[4,0,125,100,0,400,4,1,125,100,125,400,4,2,125,100,250,400,4,5,625,100,375,400,4,9,100,100,1000,400,4,10,100,100,1100,400,4,11,100,100,1200,400,4,12,100,100,1300,400,4,13,100,100,1400,400,4,14,100,100,1500,400]]}";

// The "struct" for physical layout is just a raw JSON string buffer in this context,
// but since cfgmod uses it for direct blob storage if not registered,
// we'll just handle it as a raw string kind.

static void phys_default(void *out_struct) {
    char *s = (char *)out_struct;
    strcpy(s, DEFAULT_PHYS_JSON);
}

static bool phys_deserialize(cJSON *root, void *out_struct) {
    char *s = (char *)out_struct;
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) return false;
    
    // We assume the caller allocated enough space (sizeof(DEFAULT_PHYS_JSON)*2 or similar)
    // Actually, cfgmod_get_config for registered kinds uses a struct.
    // For physical layout, it's a bit of a hack because it's variable length.
    // But since we are registering it as a kind, we must provide a struct size.
    // Let's use 4096 as a safe buffer size for the "struct".
    
    strncpy(s, json_str, 4095);
    s[4095] = '\0';
    free(json_str);
    return true;
}

static cJSON *phys_serialize(const void *in_struct) {
    const char *s = (const char *)in_struct;
    return cJSON_Parse(s);
}

void cfg_physical_register(void) {
    // Registering with a 4096 byte buffer size.
    cfgmod_register_kind(CFGMOD_KIND_PHYSICAL, phys_default, phys_deserialize,
                         phys_serialize, NULL, 4096);
}
