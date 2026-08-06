idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer nvs_flash sitetwin_core
                                sitetwin_sensor_runtime sitetwin_sensors sitetwin_espidf_hal)

if(SITETWIN_DEVICE_ROLE STREQUAL "gateway")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SITETWIN_GATEWAY_ROLE_BUILD=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SITETWIN_GATEWAY_ROLE_BUILD=0)
endif()
