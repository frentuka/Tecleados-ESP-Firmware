idf_component_register(
    SRCS "comm_crc.c" "comm_dispatch.c" "comm_rx.c" "comm_tx.c"
         "comm_send.c" "comm_transport.c" "comm_session.c" "comm_module.c"
    INCLUDE_DIRS "." "include" "../../components/utils"
    REQUIRES freertos esp_timer
)
