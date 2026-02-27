idf_component_register(
    SRCS "kb_macro.c" "kb_layout.c" "kb_manager.c" "kb_matrix.c" "kb_report.c" "kb_state.c"
    INCLUDE_DIRS "." "include"
    REQUIRES esp_timer driver freertos tinyusb usb_module ble_module rgb_module
)