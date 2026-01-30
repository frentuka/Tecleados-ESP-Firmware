idf_component_register(
    SRCS "usb_callbacks_rx.c" "usb_crc.c" "usb_callbacks.c" "usbmod.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES esp_tinyusb driver freertos keyboard config_module
)