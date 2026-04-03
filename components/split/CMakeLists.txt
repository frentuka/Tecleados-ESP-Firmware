idf_component_register(
    SRCS "split_config_sync.c" "split_sync.c" "split_role.c" "split_pair.c" "split_crypto.c" "splitmod.c" "split_transport.c" "split_protocol.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_wifi esp_event freertos event_bus config_module keyboard mbedtls usb_module battery_module
)
