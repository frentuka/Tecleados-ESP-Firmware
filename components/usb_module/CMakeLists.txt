idf_component_register(
    SRCS "usbmod.c" "usb_callbacks.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES esp_tinyusb driver freertos keyboard comm_module
)