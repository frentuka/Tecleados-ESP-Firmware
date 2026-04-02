// C bridge for the Rust split component.
// The actual implementation lives in src/lib.rs.
#include "splitmod.h"

#define TAG "Split"

void splitmod_init(void)
{
    ESP_LOGI(TAG, "Hello world!!!!");
}