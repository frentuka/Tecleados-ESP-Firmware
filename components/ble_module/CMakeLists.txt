idf_component_register(
    SRCS "blemod.c" "ble_hid_service.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_timer driver freertos bt nvs_flash esp_event keyboard
)