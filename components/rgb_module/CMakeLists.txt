idf_component_register(
    SRCS "rgb.c"
    INCLUDE_DIRS "."
    REQUIRES driver led_strip event_bus
)