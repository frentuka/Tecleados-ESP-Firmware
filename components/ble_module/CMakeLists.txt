idf_component_register(
    SRCS "blemod.c" "ble_hid_service.c" "ble_comm_service.c" "ble_comm_transport.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_timer driver freertos bt nvs_flash esp_event keyboard event_bus battery_module comm_module
)