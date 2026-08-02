idf_component_register(
    SRCS "gateway-wifi.c" "gateway_pipeline.c" "test_loop.c" "console_commands.c" "uart_link.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif nvs_flash mqtt esp-tls esp_event log sitetwin_core console esp_timer driver
)