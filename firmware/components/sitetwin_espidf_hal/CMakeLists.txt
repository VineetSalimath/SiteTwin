idf_component_register(SRCS "src/espidf_i2c_bus.c" "src/espidf_onewire_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c onewire_bus sitetwin_sensor_runtime)
