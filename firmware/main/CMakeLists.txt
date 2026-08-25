idf_component_register(SRCS "app_main.c" "control_persistence.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer nvs_flash sitetwin_core
                                sitetwin_sensor_runtime sitetwin_sensors sitetwin_espidf_hal
                                sitetwin_espidf_actuation)

if(SITETWIN_DEVICE_ROLE STREQUAL "gateway")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SITETWIN_GATEWAY_ROLE_BUILD=1
        SITETWIN_POD_PROFILE_ACTIVITY_BUILD=0
        SITETWIN_POD_PROFILE_EQUIPMENT_BUILD=0
        SITETWIN_POD_PROFILE_FINAL_PCB_BUILD=0)
else()
    if(NOT DEFINED SITETWIN_POD_PROFILE)
        set(SITETWIN_POD_PROFILE "environment")
    endif()

    if(SITETWIN_POD_PROFILE STREQUAL "activity")
        set(_sitetwin_activity 1)
        set(_sitetwin_equipment 0)
        set(_sitetwin_final_pcb 0)
    elseif(SITETWIN_POD_PROFILE STREQUAL "equipment")
        set(_sitetwin_activity 0)
        set(_sitetwin_equipment 1)
        set(_sitetwin_final_pcb 0)
    elseif(SITETWIN_POD_PROFILE STREQUAL "environment")
        set(_sitetwin_activity 0)
        set(_sitetwin_equipment 0)
        set(_sitetwin_final_pcb 0)
    elseif(SITETWIN_POD_PROFILE STREQUAL "final_pcb")
        set(_sitetwin_activity 0)
        set(_sitetwin_equipment 0)
        set(_sitetwin_final_pcb 1)
        if(NOT DEFINED SITETWIN_FINAL_PCB_POD_ID OR SITETWIN_FINAL_PCB_POD_ID STREQUAL "")
            message(FATAL_ERROR
                "SITETWIN_POD_PROFILE=final_pcb requires -D SITETWIN_FINAL_PCB_POD_ID=<this "
                "board's real pod id, e.g. POD_67C3>. Each physical final_pcb board needs "
                "its own real identity at build time -- there is no safe shared default. "
                "A single hardcoded value shared across all final_pcb boards previously "
                "broke command-ack and control-event routing for every board except the "
                "one it happened to literally match (see project decision log).")
        endif()
    else()
        message(FATAL_ERROR
            "Unknown SITETWIN_POD_PROFILE='${SITETWIN_POD_PROFILE}'. "
            "Choose environment, activity, equipment, or final_pcb.")
    endif()

    message(STATUS "SiteTwin pod profile: ${SITETWIN_POD_PROFILE}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        SITETWIN_GATEWAY_ROLE_BUILD=0
        SITETWIN_POD_PROFILE_ACTIVITY_BUILD=${_sitetwin_activity}
        SITETWIN_POD_PROFILE_EQUIPMENT_BUILD=${_sitetwin_equipment}
        SITETWIN_POD_PROFILE_FINAL_PCB_BUILD=${_sitetwin_final_pcb})
    if(_sitetwin_final_pcb)
        message(STATUS "SiteTwin final_pcb pod id: ${SITETWIN_FINAL_PCB_POD_ID}")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            "SITETWIN_FINAL_PCB_POD_ID=\"${SITETWIN_FINAL_PCB_POD_ID}\"")
    endif()
endif()