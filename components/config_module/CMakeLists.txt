idf_component_register(
    SRCS "cfg_ble.c" "cfg_layouts.c" "cfg_macros.c" "cfg_physical.c" "cfg_system.c" "cfgmod.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_tinyusb driver freertos nvs_flash json usb_module ble_module
    PRIV_REQUIRES keyboard
)
