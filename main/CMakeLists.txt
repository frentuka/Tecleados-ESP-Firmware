idf_component_register(SRCS "ble_controller.c" "main.c"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS "."
                       REQUIRES keyboard button_module rgb_module usb_module config_module ble_module status_module event_bus)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)