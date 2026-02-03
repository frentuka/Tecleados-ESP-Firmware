idf_component_register(
    SRCS "usbmod.c" "usb_send.c" "usb_callbacks.c" "usb_callbacks_rx.c" "usb_callbacks_tx.c" "usb_crc.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES esp_tinyusb driver freertos keyboard config_module
)