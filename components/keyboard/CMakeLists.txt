idf_component_register(
    SRCS "kb_layout.c" "kb_manager.c" "kb_matrix.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_timer driver freertos tinyusb usb_module
)