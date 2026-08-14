idf_component_register(SRCS "app_main.c" "control_persistence.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer nvs_flash sitetwin_core
                                sitetwin_sensor_runtime sitetwin_sensors sitetwin_espidf_hal)

if(SITETWIN_DEVICE_ROLE STREQUAL "gateway")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SITETWIN_GATEWAY_ROLE_BUILD=1
        SITETWIN_POD_PROFILE_ACTIVITY_BUILD=0
        SITETWIN_POD_PROFILE_EQUIPMENT_BUILD=0)
else()
    if(NOT DEFINED SITETWIN_POD_PROFILE)
        set(SITETWIN_POD_PROFILE "environment")
    endif()

    if(SITETWIN_POD_PROFILE STREQUAL "activity")
        set(_sitetwin_activity 1)
        set(_sitetwin_equipment 0)
    elseif(SITETWIN_POD_PROFILE STREQUAL "equipment")
        set(_sitetwin_activity 0)
        set(_sitetwin_equipment 1)
    elseif(SITETWIN_POD_PROFILE STREQUAL "environment")
        set(_sitetwin_activity 0)
        set(_sitetwin_equipment 0)
    else()
        message(FATAL_ERROR
            "Unknown SITETWIN_POD_PROFILE='${SITETWIN_POD_PROFILE}'. "
            "Choose environment, activity, or equipment.")
    endif()

    message(STATUS "SiteTwin pod profile: ${SITETWIN_POD_PROFILE}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SITETWIN_GATEWAY_ROLE_BUILD=0
        SITETWIN_POD_PROFILE_ACTIVITY_BUILD=${_sitetwin_activity}
        SITETWIN_POD_PROFILE_EQUIPMENT_BUILD=${_sitetwin_equipment})
endif()
