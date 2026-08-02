idf_component_register(
    SRCS "cfg_combos.c" "cfg_custom_keys.c" "cfg_ble.c" "cfg_layouts.c" "cfg_macros.c" "cfg_physical.c" "cfg_system.c" "cfgmod.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_tinyusb driver freertos nvs_flash usb_module ble_module cjson event_bus comm_module
)
