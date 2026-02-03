idf_component_register(
    SRCS "cfg_layouts.c" "cfgmod.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_tinyusb driver freertos nvs_flash
)