idf_component_register(SRCS "src/espidf_final_pcb_board.c"
                            "src/espidf_i2c_bus.c" "src/espidf_onewire_bus.c"
                            "src/espidf_shared_i2c_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_adc esp_driver_gpio esp_driver_i2c onewire_bus
                                sitetwin_sensor_runtime)
