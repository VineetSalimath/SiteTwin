idf_component_register(SRCS "src/espidf_actuation.c" "src/espidf_shared_alarm_output.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver sitetwin_core sitetwin_sensor_runtime)