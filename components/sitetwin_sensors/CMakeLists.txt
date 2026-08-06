idf_component_register(SRCS "src/sht41.c"
                            "src/scd41.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sitetwin_core sitetwin_sensor_runtime)
